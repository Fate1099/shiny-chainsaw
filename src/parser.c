#include "parser.h"

gchar *
md_parser_parse(const gchar *markdown, GError **error)
{
    g_return_val_if_fail(markdown != NULL, NULL);

    if (*markdown == '\0')
        return g_strdup("");

    /* TODO: real block + inline parsing. For now, escape and wrap in <p>. */
    gchar *escaped = g_markup_escape_text(markdown, -1);
    gchar *html = g_strdup_printf("<p>%s</p>", escaped);
    g_free(escaped);

    return html;
}