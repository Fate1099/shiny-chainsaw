/* Robustness suite: malformed, truncated, and adversarial input.
 *
 * These cases are not about spec conformance; they are about the parser not
 * reading past the end of a string, not leaking, and not aborting on input no
 * sane document contains. Every input must parse to non-NULL HTML. Run under
 * valgrind or ASAN to catch the out-of-bounds reads these inputs used to
 * trigger. */
#include "parser.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static MdOptions gfm_all(void)
{
    MdOptions opts = md_options_default();
    md_options_set(&opts, MD_FEATURE_GFM_TABLES, TRUE);
    md_options_set(&opts, MD_FEATURE_GFM_STRIKETHROUGH, TRUE);
    md_options_set(&opts, MD_FEATURE_GFM_TASKLISTS, TRUE);
    md_options_set(&opts, MD_FEATURE_GFM_AUTOLINKS, TRUE);
    md_options_set(&opts, MD_FEATURE_GFM_RAW_HTML, TRUE);
    return opts;
}

static gchar* parse_all(const char* input)
{
    MdOptions opts = gfm_all();
    GError* error = NULL;
    MdNode* doc = md_parse(input, &opts, &error);
    assert(doc != NULL);
    assert(error == NULL);
    gchar* html = md_render_html(doc);
    md_node_free(doc);
    return html;
}

/* Repeats `c` n times, as a newly allocated string. */
static gchar* repeat(char c, int n)
{
    gchar* s = g_new(gchar, n + 1);
    for (int i = 0; i < n; i++)
        s[i] = c;
    s[n] = '\0';
    return s;
}

/* Repeats a block n times. */
static gchar* repeat_str(const char* block, int n)
{
    GString* s = g_string_new(NULL);
    for (int i = 0; i < n; i++)
        g_string_append(s, block);
    return g_string_free(s, FALSE);
}

static void check(const char* name, const char* input)
{
    gchar* html = parse_all(input);
    if (html == NULL)
    {
        fprintf(stderr, "robustness: %s produced no output\n", name);
        abort();
    }
    g_free(html);
}

int main(void)
{
    /* NULL input must report a GError, not abort through g_return*. */
    {
        MdOptions opts = md_options_default();
        GError* error = NULL;
        MdNode* doc = md_parse(NULL, &opts, &error);
        assert(doc == NULL);
        assert(error != NULL);
        assert(error->domain == g_quark_from_static_string(MD_PARSER_ERROR_DOMAIN));
        assert(error->code == MD_PARSER_ERROR_INVALID_INPUT);
        g_clear_error(&error);
    }

    /* Truncated constructs: every one used to read one character too far. */
    static const char* truncated[] = {
        "- [",      "- []",       "- [x",
        "[",        "![",         "[]",
        "[a](",     "[a](b",      "[a](b\\",
        "[a](b\\)", "[a](\"",     "[a](b\"c",
        "[a](<b",   "[a]:",       "[a]: <",
        "[a]: <>",  "[a]: (",     "`",
        "``",       "````",       "````````````````````````````````",
        "#",        "#######",    "*a**",
        "**a*",     "***a**",     "~~a~",
        "~a",       "> ",         ">",
        ">>",       "|",          "|:",
        "|:|",      "|-|",        "||",
        "<div",     "<script>",   "<script",
        "<!--",     "<!-- a -->", "\r",
        "\r\n",     "\n\n\n\n\n", " ",
        "\t",       "\t\t\t",
    };
    for (guint i = 0; i < G_N_ELEMENTS(truncated); i++)
        check("truncated", truncated[i]);

    /* Underflow: a lone ':' in a delimiter row. */
    {
        gchar* html = parse_all("|a|\n|:|\n|b|\n");
        assert(strstr(html, "<td align=\"left\">") == NULL);
        g_free(html);
    }

    /* A line shorter than the container indent. */
    check("short lazy continuation", ">  a\n b\n");
    check("short html block line", "<div>\n  a\n b\n");

    /* Very wide input: no fixed-size buffers anywhere. */
    {
        gchar* long_line = repeat('x', 100000);
        check("100k character line", long_line);
        gchar* indent = repeat(' ', 100000);
        check("100k indent", indent);
        gchar* deep_list = repeat_str("- ", 2000);
        check("2000 nested list items", deep_list);
        gchar* deep_quote = repeat_str("> ", 2000);
        check("2000 nested blockquotes", deep_quote);
        gchar* emphasis = repeat('*', 50000);
        check("50k emphasis markers", emphasis);
        g_free(long_line);
        g_free(indent);
        g_free(deep_list);
        g_free(deep_quote);
        g_free(emphasis);
    }

    /* Wide tables: the delimiter row must not be capped. */
    {
        GString* table = g_string_new(NULL);
        for (int c = 0; c < 400; c++)
            g_string_append_printf(table, "|c%d", c);
        g_string_append(table, "|\n");
        for (int c = 0; c < 400; c++)
            g_string_append(table, "|:-");
        g_string_append(table, "|\n");
        for (int c = 0; c < 400; c++)
            g_string_append(table, "|x");
        g_string_append(table, "|\n");
        check("400 column table", table->str);
        g_string_free(table, TRUE);
    }

    /* Invalid UTF-8 must not crash the UTF-8 walking code. */
    {
        static const char* invalid_utf8 = "\x80\x81\x82 ß\xc3\x28 [a](b)\n\xc3";
        check("invalid utf-8", invalid_utf8);
    }

    /* Entities and escapes in odd places. */
    check("entity without semicolon", "&amp &not; &#xZZ; &#;");
    check("escape at end of line", "a\\\n");
    check("backslash before punctuation", "a\\\\\\*b\\`c\\[d\\]");

    printf("test_robustness: %u malformed inputs parsed without crashing\n",
           (guint)G_N_ELEMENTS(truncated));
    return 0;
}
