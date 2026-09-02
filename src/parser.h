#ifndef MD_PARSER_H
#define MD_PARSER_H

#include <glib.h>

/**
 * Parse raw Markdown text and emit corresponding HTML.
 *
 * The parser is pure C: it depends only on GLib and must never include GTK
 * headers. This keeps it unit-testable without a display.
 *
 * @param markdown  (transfer none) Raw Markdown source.
 * @param error     (out, optional) GError on failure.
 * @return          (transfer full) Newly allocated HTML string, or NULL on error.
 */
gchar *md_parser_parse(const gchar *markdown, GError **error);

#endif /* MD_PARSER_H */