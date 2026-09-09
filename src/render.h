#ifndef MD_RENDER_H
#define MD_RENDER_H

#include <gtk/gtk.h>

#include "parser.h"

G_BEGIN_DECLS

/**
 * Render a parsed Markdown AST into a GtkTextBuffer using syntax-aware
 * Pango tags (headings, bold/italic, code, lists, blockquotes, links).
 *
 * This is the GTK-side renderer; it lives separately from parser.c so the
 * parser stays pure C.
 *
 * @param doc     (transfer none) Root document node from md_parse().
 * @param buffer  (transfer none) Target buffer; existing content is cleared.
 */
void md_render_to_buffer(const MdNode* doc, GtkTextBuffer* buffer);

G_END_DECLS

#endif /* MD_RENDER_H */