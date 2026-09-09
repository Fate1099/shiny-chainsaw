#ifndef MD_RENDER_H
#define MD_RENDER_H

#include <gtk/gtk.h>

#include "parser.h"

typedef struct _SoupSession SoupSession;

G_BEGIN_DECLS

/**
 * Options controlling how a document is rendered into a text view.
 */
typedef struct
{
    GtkTextView* view;     /* (transfer none) target text view; buffer is cleared */
    SoupSession* session;  /* (transfer none) HTTP session for remote images; may be NULL */
    const gchar* base_dir; /* (transfer none) dir for relative image paths; may be NULL */
    guint max_width;       /* image display cap in px; 0 = fit to the viewport width */
} MdRenderOptions;

/**
 * Render a parsed Markdown AST into the view's buffer using syntax-aware
 * Pango tags and embedded widgets (headings, bold/italic, code, lists,
 * blockquotes, links, images).
 *
 * This is the GTK-side renderer; it lives separately from parser.c so the
 * parser stays pure C.
 *
 * @param doc     (transfer none) Root document node from md_parse().
 * @param options (transfer none) Render target and options.
 */
void md_render_to_buffer(const MdNode* doc, const MdRenderOptions* options);

G_END_DECLS

#endif /* MD_RENDER_H */