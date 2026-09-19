#ifndef MD_PARSER_H
#define MD_PARSER_H

#include <glib.h>

G_BEGIN_DECLS

/* ------------------------------------------------------------------ */
/* Feature flags                                                       */
/* ------------------------------------------------------------------ */

typedef enum
{
    MD_FEATURE_GFM_TABLES = 1 << 0,
    MD_FEATURE_GFM_STRIKETHROUGH = 1 << 1,
    MD_FEATURE_GFM_TASKLISTS = 1 << 2,
    MD_FEATURE_GFM_AUTOLINKS = 1 << 3,
    MD_FEATURE_EXTRA_DEFINITION_LISTS = 1 << 4,
    MD_FEATURE_EXTRA_FOOTNOTES = 1 << 5,
    MD_FEATURE_EXTRA_ABBREVIATIONS = 1 << 6,
} MdFeature;

typedef struct
{
    guint flags;
} MdOptions;

/* ------------------------------------------------------------------ */
/* AST                                                                 */
/* ------------------------------------------------------------------ */

typedef enum
{
    MD_NODE_DOCUMENT,
    /* block-level */
    MD_NODE_HEADING,
    MD_NODE_PARAGRAPH,
    MD_NODE_BLOCKQUOTE,
    MD_NODE_LIST,
    MD_NODE_ITEM,
    MD_NODE_CODE_BLOCK,
    MD_NODE_THEMATIC_BREAK,
    MD_NODE_TABLE,
    MD_NODE_TABLE_ROW,
    MD_NODE_TABLE_CELL,
    /* inline-level */
    MD_NODE_TEXT,
    MD_NODE_EMPHASIS,
    MD_NODE_STRONG,
    MD_NODE_CODE_SPAN,
    MD_NODE_LINK,
    MD_NODE_IMAGE,
} MdNodeKind;

typedef enum
{
    MD_LIST_UNORDERED,
    MD_LIST_ORDERED,
} MdListType;

typedef enum
{
    MD_ALIGN_NONE,
    MD_ALIGN_LEFT,
    MD_ALIGN_CENTER,
    MD_ALIGN_RIGHT,
} MdAlign;

typedef struct MdNode
{
    MdNodeKind kind;
    struct MdNode* parent;
    GPtrArray* children; /* of MdNode*; NULL for leaves */

    /* literals (owned; NULL unless relevant) */
    char* literal; /* text, code content, link/image URL */
    char* title;   /* link/image title */

    /* attributes by kind */
    int level; /* heading 1..6 */
    MdListType list_type;
    int start;       /* ordered list start number */
    gboolean tight;  /* list: no blank lines between items */
    MdAlign align;   /* table cell: column alignment */
    gboolean header; /* table row: true for the header row */
} MdNode;

/* ------------------------------------------------------------------ */
/* Options helpers                                                     */
/* ------------------------------------------------------------------ */

MdOptions md_options_default(void);
void md_options_set(MdOptions* options, MdFeature feature, gboolean enabled);
gboolean md_options_has(const MdOptions* options, MdFeature feature);

/* ------------------------------------------------------------------ */
/* Parsing                                                             */
/* ------------------------------------------------------------------ */

/**
 * Parse raw Markdown into an AST.
 *
 * Pure C: depends only on GLib, never includes GTK headers.
 *
 * @param markdown  (transfer none) Raw Markdown source.
 * @param options   (transfer none) Feature flags; NULL for defaults.
 * @param error     (out, optional) GError on failure.
 * @return          (transfer full) Root document node, or NULL on error.
 *                  Free with md_node_free().
 */
MdNode* md_parse(const gchar* markdown, const MdOptions* options, GError** error);

/**
 * Render an AST to HTML.
 *
 * @param node      (transfer none) Root document node from md_parse().
 * @return          (transfer full) Newly allocated HTML string.
 */
gchar* md_render_html(const MdNode* node);

/**
 * Convenience: parse + render in one step.
 */
gchar* md_parser_parse(const gchar* markdown, const MdOptions* options, GError** error);

void md_node_free(MdNode* node);

G_END_DECLS

#endif /* MD_PARSER_H */