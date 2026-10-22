#include "render.h"

#include <assert.h>
#include <stdio.h>

/* Smoke test for the GTK renderer: it exercises the buffer text that the
 * app actually shows, which the parser-only suites never touch. Requires a
 * display connection; skips cleanly when none is available. */

static gchar* buffer_text(GtkTextBuffer* buffer)
{
    GtkTextIter start, end;
    gtk_text_buffer_get_start_iter(buffer, &start);
    gtk_text_buffer_get_end_iter(buffer, &end);
    return gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
}

static gboolean has_text(GtkTextBuffer* buffer, const gchar* needle)
{
    GtkTextIter start, end;
    gtk_text_buffer_get_start_iter(buffer, &start);
    gtk_text_buffer_get_end_iter(buffer, &end);
    gchar* text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    gboolean found = strstr(text, needle) != NULL;
    g_free(text);
    return found;
}

static gboolean tag_at(GtkTextBuffer* buffer, const gchar* tag_name, gint offset)
{
    GtkTextTag* tag = gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(buffer), tag_name);
    if (tag == NULL)
        return FALSE;
    GtkTextIter iter;
    gtk_text_buffer_get_iter_at_offset(buffer, &iter, offset);
    GSList* tags = gtk_text_iter_get_tags(&iter);
    gboolean found = FALSE;
    for (GSList* l = tags; l != NULL; l = l->next)
    {
        if (l->data == tag)
        {
            found = TRUE;
            break;
        }
    }
    g_slist_free(tags);
    return found;
}

static MdNode* parse_gfm(const gchar* markdown)
{
    MdOptions opts = md_options_default();
    md_options_set(&opts, MD_FEATURE_GFM_TABLES, TRUE);
    md_options_set(&opts, MD_FEATURE_GFM_STRIKETHROUGH, TRUE);
    md_options_set(&opts, MD_FEATURE_GFM_TASKLISTS, TRUE);
    md_options_set(&opts, MD_FEATURE_GFM_AUTOLINKS, TRUE);
    md_options_set(&opts, MD_FEATURE_GFM_RAW_HTML, TRUE);
    return md_parse(markdown, &opts, NULL);
}

int main(void)
{
    if (!gtk_init_check(NULL, NULL))
    {
        printf("test_render: skipped (no display connection)\n");
        return 0;
    }

    GtkWidget* widget = gtk_text_view_new();
    GtkTextView* view = GTK_TEXT_VIEW(widget);
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(view);

    const gchar* markdown = "# Heading\n\n"
                            "A paragraph with <b>bold tag</b> and <!-- a comment -->.\n\n"
                            "<div class=\"box\">\n  raw block\n</div>\n\n"
                            "~~gone~~ and https://example.com\n\n"
                            "| a | b |\n|:--|--:|\n| 1 | 2 |\n\n"
                            "- [x] done\n- [ ] open\n\n"
                            "```\nfenced\n```\n\n"
                            "> quoted\n";

    MdNode* doc = parse_gfm(markdown);
    g_return_val_if_fail(doc != NULL, 1);

    MdRenderOptions render = {view, NULL, NULL, 0};
    md_render_to_buffer(doc, &render);

    g_assert(has_text(buffer, "Heading"));
    g_assert(tag_at(buffer, "md-heading1", 0));

    /* Raw HTML survives the render path instead of vanishing. */
    g_assert(has_text(buffer, "<b>bold tag</b>"));
    g_assert(has_text(buffer, "<!-- a comment -->"));
    g_assert(has_text(buffer, "<div class=\"box\">"));
    g_assert(has_text(buffer, "raw block"));

    g_assert(has_text(buffer, "gone"));
    g_assert(has_text(buffer, "https://example.com"));
    g_assert(has_text(buffer, "| a |"));
    g_assert(has_text(buffer, "|---|---|"));
    g_assert(has_text(buffer, "| 1 |"));
    g_assert(has_text(buffer, "  2|"));
    g_assert(has_text(buffer, "☑ done"));
    g_assert(has_text(buffer, "☐ open"));
    g_assert(has_text(buffer, "fenced"));
    g_assert(has_text(buffer, "quoted"));

    /* Trailing blank lines are trimmed. */
    gchar* text = buffer_text(buffer);
    g_assert(g_str_has_suffix(text, "quoted"));
    g_free(text);

    /* Re-rendering must not duplicate tags or leave stale content. */
    MdNode* second = parse_gfm("only **one** line");
    md_render_to_buffer(second, &render);
    text = buffer_text(buffer);
    g_assert(strcmp(text, "only one line") == 0);
    g_free(text);

    md_node_free(doc);
    md_node_free(second);
    gtk_widget_destroy(widget);

    printf("test_render: passed\n");
    return 0;
}
