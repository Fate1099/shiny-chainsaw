#include "parser.h"
#include <stdio.h>
#include <string.h>

#define FENCE "````````````````````````````````"

typedef struct
{
    gchar* markdown;
    gchar* html;
    guint example;
    guint start_line;
    guint end_line;
    gchar* section;
} Example;

static void example_free(Example* ex)
{
    g_free(ex->markdown);
    g_free(ex->html);
    g_free(ex->section);
    g_free(ex);
}

typedef struct
{
    GString* markdown;
    GString* html;
    guint example_number;
    guint start_line;
    guint end_line;
    gchar* section;
    gint state;
    gboolean disabled;
} ParseCtx;

static void replace_arrow(GString* s, const gchar* line)
{
    const gchar* p = line;
    const gchar* arrow = strstr(p, "\xe2\x86\x92");
    while (arrow != NULL)
    {
        g_string_append_len(s, p, (gsize)(arrow - p));
        g_string_append_c(s, '\t');
        p = arrow + strlen("\xe2\x86\x92");
        arrow = strstr(p, "\xe2\x86\x92");
    }
    g_string_append(s, p);
}

static void begin_example(ParseCtx* ctx, const gchar* line)
{
    ctx->state = 1;
    ctx->disabled = (strstr(line, "disabled") != NULL);
    if (ctx->markdown != NULL)
        g_string_free(ctx->markdown, TRUE);
    if (ctx->html != NULL)
        g_string_free(ctx->html, TRUE);
    ctx->markdown = g_string_new(NULL);
    ctx->html = g_string_new(NULL);
    ctx->start_line = 0;
}

static Example* close_example(ParseCtx* ctx, guint line_number)
{
    Example* ex = g_new0(Example, 1);
    ex->example = ctx->example_number;
    ex->start_line = ctx->start_line;
    ex->end_line = line_number;
    ex->section = g_strdup(ctx->section != NULL ? ctx->section : "");
    ex->markdown = ctx->markdown != NULL ? g_string_free(ctx->markdown, FALSE) : g_strdup("");
    ex->html = ctx->html != NULL ? g_string_free(ctx->html, FALSE) : g_strdup("");
    ctx->markdown = NULL;
    ctx->html = NULL;
    ctx->state = 0;
    return ex;
}

static gboolean is_header(const gchar* line)
{
    const gchar* p = line;
    while (*p == '#')
        p++;
    return p > line && *p == ' ';
}

static void print_diff(const gchar* expected, const gchar* actual)
{
    gchar** exp = g_strsplit(expected, "\n", -1);
    gchar** got = g_strsplit(actual, "\n", -1);
    gsize ne = 0;
    gsize ng = 0;
    while (exp[ne] != NULL)
        ne++;
    while (got[ng] != NULL)
        ng++;

    gsize* dp = g_new0(gsize, (ng + 1) * (ne + 1));
    for (gsize i = ne; i-- > 0;)
    {
        for (gsize j = ng; j-- > 0;)
        {
            if (g_strcmp0(exp[i], got[j]) == 0)
                dp[j * (ne + 1) + i] = dp[(j + 1) * (ne + 1) + (i + 1)] + 1;
            else
                dp[j * (ne + 1) + i] = MAX(dp[(j + 1) * (ne + 1) + i], dp[j * (ne + 1) + (i + 1)]);
        }
    }

    gsize i = 0;
    gsize j = 0;
    while (i < ne && j < ng)
    {
        if (g_strcmp0(exp[i], got[j]) == 0)
        {
            i++;
            j++;
        }
        else if (dp[(j + 1) * (ne + 1) + i] >= dp[j * (ne + 1) + (i + 1)])
        {
            fprintf(stderr, "- %s\n", exp[i]);
            i++;
        }
        else
        {
            fprintf(stderr, "+ %s\n", got[j]);
            j++;
        }
    }
    while (i < ne)
    {
        fprintf(stderr, "- %s\n", exp[i]);
        i++;
    }
    while (j < ng)
    {
        fprintf(stderr, "+ %s\n", got[j]);
        j++;
    }

    g_free(dp);
    g_strfreev(exp);
    g_strfreev(got);
}

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        g_printerr("usage: spec_tests SPEC_FILE {{commonmark|gfm}}\n");
        return 2;
    }

    gchar* buf = NULL;
    gsize len = 0;
    if (!g_file_get_contents(argv[1], &buf, &len, NULL))
        return 2;

    ParseCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.section = g_strdup("");
    ctx.state = 0;

    GPtrArray* tests = g_ptr_array_new_with_free_func((GDestroyNotify)example_free);
    guint start_line = 0;
    const gchar* pos = buf;
    guint line_number = 0;
    while (pos < buf + len)
    {
        const gchar* nl = strchr(pos, '\n');
        gsize line_len = (nl != NULL) ? (gsize)(nl - pos) : (gsize)(buf + len - pos);
        line_number++;

        gchar* line = g_strndup(pos, line_len);
        gchar* stripped = g_strstrip(line); /* in-place */
        if (ctx.state == 1 || ctx.state == 2)
        {
            gboolean matched = FALSE;
            if (g_str_has_prefix(stripped, FENCE " example"))
            {
                begin_example(&ctx, stripped);
                matched = TRUE;
            }
            else if (ctx.state == 2 && g_strcmp0(stripped, FENCE) == 0)
            {
                ctx.example_number++;
                if (!ctx.disabled)
                {
                    Example* ex = close_example(&ctx, line_number);
                    g_ptr_array_add(tests, ex);
                }
                ctx.state = 0;
                matched = TRUE;
            }
            else if (ctx.state == 1 && g_strcmp0(stripped, ".") == 0)
            {
                ctx.state = 2;
                matched = TRUE;
            }
            if (!matched)
            {
                if (ctx.state == 1)
                {
                    if (ctx.start_line == 0)
                        ctx.start_line = line_number - 1;
                    replace_arrow(ctx.markdown, line);
                }
                else if (ctx.state == 2)
                {
                    replace_arrow(ctx.html, line);
                }
            }
        }
        else if (g_str_has_prefix(stripped, FENCE " example"))
        {
            begin_example(&ctx, stripped);
        }
        else if (is_header(stripped))
        {
            g_free(ctx.section);
            ctx.section = g_strdup(stripped + strspn(stripped, "#") + 1);
        }
        g_free(line);
        pos = pos + line_len + 1;
        (void)start_line;
    }

    MdOptions opts = md_options_default();
    if (strcmp(argv[2], "gfm") == 0)
    {
        md_options_set(&opts, MD_FEATURE_GFM_TABLES, TRUE);
        md_options_set(&opts, MD_FEATURE_GFM_STRIKETHROUGH, TRUE);
        md_options_set(&opts, MD_FEATURE_GFM_TASKLISTS, TRUE);
        md_options_set(&opts, MD_FEATURE_GFM_AUTOLINKS, TRUE);
    }

    guint passed = 0;
    guint failed = 0;
    for (guint i = 0; i < tests->len; i++)
    {
        Example* ex = (Example*)g_ptr_array_index(tests, i);
        GError* error = NULL;
        MdNode* doc = md_parse(ex->markdown, &opts, &error);
        if (doc == NULL)
        {
            fprintf(stderr, "Example %u (%s, lines %u-%u): PARSE ERROR %s\n", ex->example,
                    ex->section, ex->start_line, ex->end_line, error != NULL ? error->message : "");
            g_clear_error(&error);
            failed++;
            continue;
        }
        gchar* actual = md_render_html(doc);
        md_node_free(doc);
        if (strcmp(actual, ex->html) == 0)
        {
            passed++;
        }
        else
        {
            failed++;
            fprintf(stderr, "Example %u (%s, lines %u-%u):\n", ex->example, ex->section,
                    ex->start_line, ex->end_line);
            print_diff(ex->html, actual);
            fprintf(stderr, "\n");
        }
        g_free(actual);
    }

    guint total = passed + failed;
    g_print("%s: %u/%u passed (%.1f%%)\n", argv[1], passed, total,
            100.0 * (double)passed / (double)total);
    if (failed > 0)
        g_printerr("%s: %u failures\n", argv[1], failed);

    g_ptr_array_unref(tests);
    g_free(ctx.section);
    g_free(buf);
    return failed > 0;
}