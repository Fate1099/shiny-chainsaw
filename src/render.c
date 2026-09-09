#include "render.h"

typedef struct
{
    GtkTextBuffer* buffer;
    GtkTextTag* heading[6];
    GtkTextTag* bold;
    GtkTextTag* italic;
    GtkTextTag* mono;
    GtkTextTag* codeblock;
    GtkTextTag* link;
    GtkTextTag* blockquote;
    GtkTextTag* item;
    gboolean tight;
} RenderCtx;

/* ------------------------------------------------------------------ */
/* Tag setup                                                           */
/* ------------------------------------------------------------------ */

/* Named tags are reused across renders so re-opening files does not grow
 * the buffer's tag table with duplicates. */
static GtkTextTag* get_tag(GtkTextBuffer* buffer, const gchar* name)
{
    return gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(buffer), name);
}

static void create_tags(RenderCtx* ctx)
{
    GtkTextBuffer* b = ctx->buffer;

    const double scales[6] = {2.0, 1.6, 1.35, 1.15, 1.0, 0.9};
    for (int i = 0; i < 6; i++)
    {
        gchar name[16];
        g_snprintf(name, sizeof(name), "md-heading%d", i + 1);
        ctx->heading[i] = get_tag(b, name);
        if (ctx->heading[i] == NULL)
            ctx->heading[i] = gtk_text_buffer_create_tag(b, name, "weight", PANGO_WEIGHT_BOLD,
                                                         "scale", scales[i], NULL);
    }

    ctx->bold = get_tag(b, "md-bold");
    if (ctx->bold == NULL)
        ctx->bold = gtk_text_buffer_create_tag(b, "md-bold", "weight", PANGO_WEIGHT_BOLD, NULL);

    ctx->italic = get_tag(b, "md-italic");
    if (ctx->italic == NULL)
        ctx->italic = gtk_text_buffer_create_tag(b, "md-italic", "style", PANGO_STYLE_ITALIC, NULL);

    ctx->mono = get_tag(b, "md-mono");
    if (ctx->mono == NULL)
        ctx->mono = gtk_text_buffer_create_tag(b, "md-mono", "family", "monospace", NULL);

    ctx->link = get_tag(b, "md-link");
    if (ctx->link == NULL)
        ctx->link = gtk_text_buffer_create_tag(b, "md-link", "underline", PANGO_UNDERLINE_SINGLE,
                                               "foreground", "#1a66cc", NULL);

    ctx->blockquote = get_tag(b, "md-blockquote");
    if (ctx->blockquote == NULL)
        ctx->blockquote =
            gtk_text_buffer_create_tag(b, "md-blockquote", "left-margin", 24, "right-margin", 24,
                                       "style", PANGO_STYLE_ITALIC, NULL);

    ctx->item = get_tag(b, "md-item");
    if (ctx->item == NULL)
        ctx->item = gtk_text_buffer_create_tag(b, "md-item", "left-margin", 20, NULL);

    ctx->codeblock = get_tag(b, "md-codeblock");
    if (ctx->codeblock == NULL)
        ctx->codeblock = gtk_text_buffer_create_tag(b, "md-codeblock", "family", "monospace", NULL);
}

/* ------------------------------------------------------------------ */
/* Insert helpers                                                      */
/* ------------------------------------------------------------------ */

static void insert_text(RenderCtx* ctx, const char* text, GtkTextTag* tag)
{
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(ctx->buffer, &end);
    if (tag != NULL)
        gtk_text_buffer_insert_with_tags(ctx->buffer, &end, text, -1, tag, NULL);
    else
        gtk_text_buffer_insert(ctx->buffer, &end, text, -1);
}

/* Apply `tag` from a captured char offset to the current end. Offsets are
 * stable across inserts, unlike raw iterators. */
static void apply_tag_from(RenderCtx* ctx, GtkTextTag* tag, gint start_offset)
{
    GtkTextIter start, end;
    gtk_text_buffer_get_iter_at_offset(ctx->buffer, &start, start_offset);
    gtk_text_buffer_get_end_iter(ctx->buffer, &end);
    gtk_text_buffer_apply_tag(ctx->buffer, tag, &start, &end);
}

static void render_block(const MdNode* node, RenderCtx* ctx);
static void render_inline_children(const MdNode* node, RenderCtx* ctx);

/* Render inline children, then apply `tag` across the whole range. */
static void render_inline_tagged(const MdNode* node, GtkTextTag* tag, RenderCtx* ctx)
{
    gint start = gtk_text_buffer_get_char_count(ctx->buffer);
    render_inline_children(node, ctx);
    apply_tag_from(ctx, tag, start);
}

/* ------------------------------------------------------------------ */
/* Inline                                                              */
/* ------------------------------------------------------------------ */

static void render_inline_children(const MdNode* node, RenderCtx* ctx)
{
    for (guint i = 0; i < node->children->len; i++)
    {
        const MdNode* child = g_ptr_array_index(node->children, i);
        switch (child->kind)
        {
        case MD_NODE_TEXT:
            insert_text(ctx, child->literal, NULL);
            break;
        case MD_NODE_EMPHASIS:
            render_inline_tagged(child, ctx->italic, ctx);
            break;
        case MD_NODE_STRONG:
            render_inline_tagged(child, ctx->bold, ctx);
            break;
        case MD_NODE_CODE_SPAN:
            insert_text(ctx, child->literal, ctx->mono);
            break;
        case MD_NODE_LINK:
            render_inline_tagged(child, ctx->link, ctx);
            break;
        case MD_NODE_IMAGE:
        {
            GString* alt = g_string_new("[");
            for (guint j = 0; j < child->children->len; j++)
            {
                const MdNode* c2 = g_ptr_array_index(child->children, j);
                if (c2->kind == MD_NODE_TEXT)
                    g_string_append(alt, c2->literal);
            }
            g_string_append(alt, "]");
            insert_text(ctx, alt->str, NULL);
            g_string_free(alt, TRUE);
            break;
        }
        default:
            break;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Blocks                                                              */
/* ------------------------------------------------------------------ */

static void render_list_item(const MdNode* item, const char* marker, RenderCtx* ctx)
{
    gboolean prev_tight = ctx->tight;

    gint start = gtk_text_buffer_get_char_count(ctx->buffer);
    insert_text(ctx, marker, NULL);

    /* A single tight paragraph renders inline; otherwise render blocks. */
    if (item->children->len == 1 &&
        ((const MdNode*)g_ptr_array_index(item->children, 0))->kind == MD_NODE_PARAGRAPH &&
        prev_tight)
    {
        const MdNode* para = g_ptr_array_index(item->children, 0);
        gint item_start = gtk_text_buffer_get_char_count(ctx->buffer);
        render_inline_children(para, ctx);
        apply_tag_from(ctx, ctx->item, item_start);
        insert_text(ctx, "\n", NULL);
        return;
    }

    for (guint i = 0; i < item->children->len; i++)
        render_block(g_ptr_array_index(item->children, i), ctx);

    apply_tag_from(ctx, ctx->item, start);
}

static void render_list(const MdNode* node, RenderCtx* ctx)
{
    gboolean prev_tight = ctx->tight;
    ctx->tight = node->tight;
    gboolean ordered = node->list_type == MD_LIST_ORDERED;

    for (guint i = 0; i < node->children->len; i++)
    {
        const MdNode* item = g_ptr_array_index(node->children, i);
        gchar* marker = NULL;
        if (ordered)
            marker = g_strdup_printf("%d. ", node->start + (int)i);
        else
            marker = g_strdup("- ");
        render_list_item(item, marker, ctx);
        g_free(marker);
    }
    if (node->tight)
        insert_text(ctx, "\n", NULL); /* complete the blank line after a tight list */
    ctx->tight = prev_tight;
}

static void render_block(const MdNode* node, RenderCtx* ctx)
{
    switch (node->kind)
    {
    case MD_NODE_HEADING:
        render_inline_tagged(node, ctx->heading[node->level - 1], ctx);
        insert_text(ctx, "\n\n", NULL);
        break;

    case MD_NODE_PARAGRAPH:
        render_inline_children(node, ctx);
        insert_text(ctx, "\n\n", NULL);
        break;

    case MD_NODE_BLOCKQUOTE:
    {
        gint start = gtk_text_buffer_get_char_count(ctx->buffer);
        for (guint i = 0; i < node->children->len; i++)
            render_block(g_ptr_array_index(node->children, i), ctx);
        apply_tag_from(ctx, ctx->blockquote, start);
        break;
    }

    case MD_NODE_LIST:
        render_list(node, ctx);
        break;

    case MD_NODE_CODE_BLOCK:
        insert_text(ctx, node->literal, ctx->codeblock);
        insert_text(ctx, "\n\n", NULL);
        break;

    case MD_NODE_THEMATIC_BREAK:
        insert_text(ctx, "────────────────────────────", NULL);
        insert_text(ctx, "\n\n", NULL);
        break;

    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void md_render_to_buffer(const MdNode* doc, GtkTextBuffer* buffer)
{
    g_return_if_fail(doc != NULL);
    g_return_if_fail(buffer != NULL);

    RenderCtx ctx = {0};
    ctx.buffer = buffer;
    ctx.tight = FALSE;
    create_tags(&ctx);

    gtk_text_buffer_set_text(buffer, "", -1);
    for (guint i = 0; i < doc->children->len; i++)
        render_block(g_ptr_array_index(doc->children, i), &ctx);

    /* Drop trailing blank lines left by the last block. */
    GtkTextIter buf_end;
    gtk_text_buffer_get_end_iter(buffer, &buf_end);
    GtkTextIter end = buf_end;
    GtkTextIter cur = buf_end;
    while (gtk_text_iter_backward_char(&cur))
    {
        gunichar c = gtk_text_iter_get_char(&cur);
        if (c == '\n' || c == ' ' || c == '\t')
            end = cur;
        else
            break;
    }
    if (!gtk_text_iter_equal(&end, &buf_end))
        gtk_text_buffer_delete(buffer, &end, &buf_end);
}