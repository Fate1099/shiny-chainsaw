#include "render.h"

#include <libsoup/soup.h>

#include <string.h>

typedef struct
{
    GtkTextBuffer* buffer;
    GtkTextView* view;
    SoupSession* session; /* borrowed; NULL disables remote image loading */
    const gchar* base_dir;
    guint max_width;
    GtkTextTag* heading[6];
    GtkTextTag* bold;
    GtkTextTag* italic;
    GtkTextTag* mono;
    GtkTextTag* codeblock;
    GtkTextTag* link;
    GtkTextTag* blockquote;
    GtkTextTag* item;
    GtkTextTag* table;
    gboolean tight;
    gint list_depth; /* nesting depth of the list being rendered */
} RenderCtx;

/* Spaces added per list nesting level (top level = 0). */
#define MD_LIST_INDENT 4

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

    ctx->table = get_tag(b, "md-table");
    if (ctx->table == NULL)
        ctx->table = gtk_text_buffer_create_tag(b, "md-table", "family", "monospace", "background",
                                                "#f6f8fa", NULL);
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
static void render_table(const MdNode* node, RenderCtx* ctx);

/* Render inline children, then apply `tag` across the whole range. */
static void render_inline_tagged(const MdNode* node, GtkTextTag* tag, RenderCtx* ctx)
{
    gint start = gtk_text_buffer_get_char_count(ctx->buffer);
    render_inline_children(node, ctx);
    apply_tag_from(ctx, tag, start);
}

/* ------------------------------------------------------------------ */
/* Images                                                              */
/* ------------------------------------------------------------------ */

typedef struct
{
    GtkImage* image;      /* owned ref until the load settles */
    GtkWidget* widget;    /* owned ref: the child anchored in the view */
    GtkTextView* view;    /* borrowed */
    SoupSession* session; /* borrowed */
    GCancellable* cancel; /* owned ref */
    GPtrArray* loads;     /* borrowed: the view's in-flight cancellables */
    guint max_width;
    const gchar* base_dir; /* borrowed */
    gchar* src;            /* owned */
} MdImageLoad;

/* Per-view list of in-flight image loads, so a re-render can cancel them. */
static GPtrArray* view_loads(GtkTextView* view)
{
    gpointer p = g_object_get_data(G_OBJECT(view), "md-image-loads");
    if (p == NULL)
    {
        p = g_ptr_array_new_with_free_func(g_object_unref);
        g_object_set_data_full(G_OBJECT(view), "md-image-loads", p,
                               (GDestroyNotify)g_ptr_array_unref);
    }
    return p;
}

static void cancel_pending_loads(GtkTextView* view)
{
    GPtrArray* loads = g_object_get_data(G_OBJECT(view), "md-image-loads");
    if (loads != NULL)
    {
        for (guint i = 0; i < loads->len; i++)
            g_cancellable_cancel(g_ptr_array_index(loads, i));
        g_ptr_array_set_size(loads, 0);
    }
}

static void md_image_load_free(MdImageLoad* data)
{
    g_object_unref(data->image);
    g_object_unref(data->widget);
    g_object_unref(data->cancel);
    g_free(data->src);
    g_free(data);
}

static GdkPixbuf* decode_pixbuf(const guchar* bytes, gsize len)
{
    GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
    GError* error = NULL;
    if (!gdk_pixbuf_loader_write(loader, bytes, len, &error))
    {
        g_clear_error(&error);
        g_object_unref(loader);
        return NULL;
    }
    if (!gdk_pixbuf_loader_close(loader, &error))
    {
        g_clear_error(&error);
        g_object_unref(loader);
        return NULL;
    }
    GdkPixbuf* pixbuf = gdk_pixbuf_loader_get_pixbuf(loader); /* borrowed */
    if (pixbuf != NULL)
        g_object_ref(pixbuf);
    g_object_unref(loader);
    return pixbuf;
}

/* Scale down (never up) to the cap, or to the viewport width when max_width
 * is 0. Queried at load time so it tracks the window as images arrive. */
static GdkPixbuf* scale_to_fit(MdImageLoad* data, GdkPixbuf* pixbuf)
{
    gint w = gdk_pixbuf_get_width(pixbuf);
    gint h = gdk_pixbuf_get_height(pixbuf);
    if (w <= 0 || h <= 0)
        return NULL;

    guint maxw = data->max_width;
    if (maxw == 0)
    {
        gint avail = gtk_widget_get_allocated_width(GTK_WIDGET(data->view)) - 80;
        if (avail > 0)
            maxw = (guint)avail;
        else
            maxw = 800; /* view not laid out yet; avoid embedding a giant image */
    }
    if (w <= (gint)maxw)
        return NULL;
    gint nw = (gint)maxw;
    return gdk_pixbuf_scale_simple(pixbuf, nw, (h * nw) / w, GDK_INTERP_BILINEAR);
}

static void image_finished(MdImageLoad* data, GBytes* bytes)
{
    g_ptr_array_remove(data->loads, data->cancel);

    GdkPixbuf* pixbuf = NULL;
    if (bytes != NULL)
    {
        gsize len = 0;
        const guchar* ptr = g_bytes_get_data(bytes, &len);
        pixbuf = decode_pixbuf(ptr, len);
        g_bytes_unref(bytes);
    }

    if (pixbuf != NULL)
    {
        GdkPixbuf* scaled = scale_to_fit(data, pixbuf);
        gtk_image_set_from_pixbuf(data->image, scaled != NULL ? scaled : pixbuf);
        if (scaled != NULL)
            g_object_unref(scaled);
        g_object_unref(pixbuf);
    }
    else
    {
        gtk_image_set_from_icon_name(data->image, "image-missing", GTK_ICON_SIZE_DIALOG);
    }

    /* The anchor widget's size changed since it was inserted; queueing a
     * resize on the anchor child (not the view) forces the text view to
     * recompute its layout so subsequent lines/images don't overlap. */
    gtk_widget_queue_resize(data->widget);

    md_image_load_free(data);
}

static void soup_image_loaded(GObject* source, GAsyncResult* res, gpointer user_data)
{
    (void)source;
    MdImageLoad* data = user_data;
    if (g_cancellable_is_cancelled(data->cancel))
    {
        md_image_load_free(data);
        return;
    }
    GError* error = NULL;
    GBytes* bytes = soup_session_send_and_read_finish(data->session, res, &error);
    if (error != NULL)
        g_clear_error(&error);
    image_finished(data, bytes);
}

static void file_image_loaded(GObject* source, GAsyncResult* res, gpointer user_data)
{
    GFile* file = G_FILE(source);
    MdImageLoad* data = user_data;
    if (g_cancellable_is_cancelled(data->cancel))
    {
        g_object_unref(file);
        md_image_load_free(data);
        return;
    }
    gchar* contents = NULL;
    gsize len = 0;
    GError* error = NULL;
    GBytes* bytes = NULL;
    if (g_file_load_contents_finish(file, res, &contents, &len, NULL, &error))
        bytes = g_bytes_new_take(contents, len);
    else
        g_clear_error(&error);
    g_object_unref(file);
    image_finished(data, bytes);
}

static void start_image_load(MdImageLoad* data)
{
    const gchar* src = data->src;

    if (g_str_has_prefix(src, "http://") || g_str_has_prefix(src, "https://"))
    {
        if (data->session == NULL)
        {
            image_finished(data, NULL);
            return;
        }
        SoupMessage* msg = soup_message_new("GET", src);
        if (msg == NULL)
        {
            image_finished(data, NULL);
            return;
        }
        g_ptr_array_add(data->loads, g_object_ref(data->cancel));
        soup_session_send_and_read_async(data->session, msg, G_PRIORITY_DEFAULT, data->cancel,
                                         soup_image_loaded, data);
        g_object_unref(msg);
        return;
    }

    gchar* path = NULL;
    if (g_str_has_prefix(src, "file://"))
        path = g_filename_from_uri(src, NULL, NULL);
    else if (g_path_is_absolute(src))
        path = g_strdup(src);
    else if (data->base_dir != NULL && *data->base_dir != '\0')
        path = g_build_filename(data->base_dir, src, NULL);
    else
        path = g_strdup(src);

    GFile* file = (path != NULL) ? g_file_new_for_path(path) : NULL;
    g_free(path);
    if (file == NULL)
    {
        image_finished(data, NULL);
        return;
    }
    g_ptr_array_add(data->loads, g_object_ref(data->cancel));
    g_file_load_contents_async(file, data->cancel, file_image_loaded, data);
    /* the GFile is unref'd by the callback */
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
            GString* alt = g_string_new(NULL);
            for (guint j = 0; j < child->children->len; j++)
            {
                const MdNode* c2 = g_ptr_array_index(child->children, j);
                if (c2->kind == MD_NODE_TEXT)
                    g_string_append(alt, c2->literal);
            }

            GtkWidget* image = gtk_image_new();
            gtk_widget_set_tooltip_text(image, alt->str);

            /* Alt text is shown as a caption under the image. */
            GtkWidget* box = image;
            if (alt->len > 0)
            {
                GtkWidget* caption = gtk_label_new(alt->str);
                gtk_label_set_justify(GTK_LABEL(caption), GTK_JUSTIFY_CENTER);
                gtk_label_set_line_wrap(GTK_LABEL(caption), TRUE);
                gtk_label_set_xalign(GTK_LABEL(caption), 0.5);
                PangoAttrList* attrs = pango_attr_list_new();
                pango_attr_list_insert(attrs, pango_attr_style_new(PANGO_STYLE_ITALIC));
                pango_attr_list_insert(attrs, pango_attr_size_new(9 * PANGO_SCALE));
                gtk_label_set_attributes(GTK_LABEL(caption), attrs);
                pango_attr_list_unref(attrs);

                box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
                gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);
                gtk_box_pack_start(GTK_BOX(box), caption, FALSE, FALSE, 0);
                gtk_widget_show(caption);
            }
            gtk_widget_show(image);
            gtk_widget_show(box);

            GtkTextIter end;
            gtk_text_buffer_get_end_iter(ctx->buffer, &end);
            GtkTextChildAnchor* anchor = gtk_text_buffer_create_child_anchor(ctx->buffer, &end);
            gtk_text_view_add_child_at_anchor(GTK_TEXT_VIEW(ctx->view), box, anchor);

            MdImageLoad* data = g_new0(MdImageLoad, 1);
            data->image = GTK_IMAGE(g_object_ref(image));
            data->widget = g_object_ref(box);
            data->view = GTK_TEXT_VIEW(ctx->view);
            data->session = ctx->session;
            data->cancel = g_cancellable_new();
            data->loads = view_loads(GTK_TEXT_VIEW(ctx->view));
            data->max_width = ctx->max_width;
            data->base_dir = ctx->base_dir;
            data->src = g_strdup(child->literal);
            start_image_load(data);

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

    gchar* pad = g_strnfill((gsize)(ctx->list_depth * MD_LIST_INDENT), ' ');
    gint start = gtk_text_buffer_get_char_count(ctx->buffer);
    insert_text(ctx, pad, NULL);
    g_free(pad);
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

/* GFM tables are drawn as an aligned monospace block with pipes, dashes,
 * and per-column alignment padding. Cells keep their inline tags (bold,
 * links, code) because the block-wide table tag only sets family/background. */
static gint table_cell_width(const MdNode* cell)
{
    gint w = 0;
    for (guint i = 0; i < cell->children->len; i++)
    {
        const MdNode* c = g_ptr_array_index(cell->children, i);
        if (c->kind == MD_NODE_TEXT)
            w += (gint)strlen(c->literal);
    }
    return w;
}

static void render_table(const MdNode* node, RenderCtx* ctx)
{
    gint start = gtk_text_buffer_get_char_count(ctx->buffer);

    gint ncols = 0;
    if (node->children->len > 0)
        ncols = (gint)((const MdNode*)g_ptr_array_index(node->children, 0))->children->len;
    if (ncols <= 0)
        return;

    gint* widths = g_new0(gint, (gsize)ncols);
    for (guint r = 0; r < node->children->len; r++)
    {
        const MdNode* row = g_ptr_array_index(node->children, r);
        for (gint c = 0; c < ncols && (guint)c < row->children->len; c++)
        {
            gint w = table_cell_width(g_ptr_array_index(row->children, c)) + 2;
            if (w > widths[c])
                widths[c] = w;
        }
    }

    for (guint r = 0; r < node->children->len; r++)
    {
        const MdNode* row = g_ptr_array_index(node->children, r);
        insert_text(ctx, "|", NULL);
        for (gint c = 0; c < ncols; c++)
        {
            gchar* pad;
            if ((guint)c < row->children->len)
            {
                const MdNode* cell = g_ptr_array_index(row->children, c);
                gint avail = widths[c] - table_cell_width(cell);
                gint left = 1, right = avail - 1;
                if (cell->align == MD_ALIGN_CENTER)
                {
                    left = avail / 2;
                    right = avail - left;
                }
                else if (cell->align == MD_ALIGN_RIGHT)
                {
                    left = avail;
                    right = 0;
                }
                if (left > 0)
                {
                    pad = g_strnfill((gsize)left, ' ');
                    insert_text(ctx, pad, NULL);
                    g_free(pad);
                }
                render_inline_children(cell, ctx);
                if (right > 0)
                {
                    pad = g_strnfill((gsize)right, ' ');
                    insert_text(ctx, pad, NULL);
                    g_free(pad);
                }
            }
            else
            {
                pad = g_strnfill((gsize)widths[c], ' ');
                insert_text(ctx, pad, NULL);
                g_free(pad);
            }
            insert_text(ctx, "|", NULL);
        }
        insert_text(ctx, "\n", NULL);

        if (r == 0)
        {
            insert_text(ctx, "|", NULL);
            for (gint c = 0; c < ncols; c++)
            {
                gchar* dash = g_strnfill((gsize)widths[c], '-');
                insert_text(ctx, dash, NULL);
                g_free(dash);
                insert_text(ctx, "|", NULL);
            }
            insert_text(ctx, "\n", NULL);
        }
    }

    apply_tag_from(ctx, ctx->table, start);
    g_free(widths);
}

static void render_list(const MdNode* node, RenderCtx* ctx)
{
    gboolean prev_tight = ctx->tight;
    gint prev_depth = ctx->list_depth;
    ctx->tight = node->tight;
    ctx->list_depth++;

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
    ctx->list_depth = prev_depth;
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

    case MD_NODE_TABLE:
        render_table(node, ctx);
        insert_text(ctx, "\n", NULL);
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

void md_render_to_buffer(const MdNode* doc, const MdRenderOptions* options)
{
    g_return_if_fail(doc != NULL);
    g_return_if_fail(options != NULL);
    g_return_if_fail(GTK_IS_TEXT_VIEW(options->view));

    GtkTextBuffer* buffer = gtk_text_view_get_buffer(options->view);
    cancel_pending_loads(options->view);

    RenderCtx ctx = {0};
    ctx.buffer = buffer;
    ctx.view = options->view;
    ctx.session = options->session;
    ctx.base_dir = options->base_dir;
    ctx.max_width = options->max_width;
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