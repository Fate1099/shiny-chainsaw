#include "app.h"
#include "parser.h"
#include "recent.h"
#include "render.h"
#include "shortcuts.h"

#include <libsoup/soup.h>

struct _MdApp
{
    GtkApplication parent_instance;

    GtkWidget* window;
    GtkWidget* headerbar;
    GtkWidget* paned;
    GtkWidget* outline_view;
    GtkWidget* search_entry;
    GtkWidget* search_results;
    GtkWidget* text_view;
    GtkWidget* statusbar;
    GtkWidget* sidebar_button;
    GtkWidget* search_button;
    GtkWidget* zoom_button;
    GtkWidget* search_controls;
    GPtrArray* pending_files; /* of gchar*, opened once the window exists */
    GPtrArray* recent_files;  /* of gchar*, most recently opened first */
    GArray* search_matches;   /* of SearchMatch */
    GMenu* recent_menu;       /* "Open recent" submenu, rebuilt on every open */
    SoupSession* session;
    GtkCssProvider* css;        /* zoom styling, added once to the screen */
    gchar* base_dir;            /* dir of the currently open file, for image paths */
    gchar* current_path;        /* absolute path of the open file */
    gchar* current_markdown;    /* contents last rendered, to skip no-op reloads */
    gchar* recent_path;         /* file backing the recent list */
    GFileMonitor* file_monitor; /* live reload of the open file */
    guint reload_source;        /* one-shot reload debounce, 0 when idle */
    guint image_max_width;
    gint search_index;
    double zoom;
};

typedef struct
{
    gint start;
    gint end;
} SearchMatch;

G_DEFINE_TYPE(MdApp, md_app, GTK_TYPE_APPLICATION)

#define MD_MIN_ZOOM 0.5
#define MD_MAX_ZOOM 4.0
#define MD_SIDEBAR_FRACTION 0.12
#define MD_SIDEBAR_MIN_WIDTH 100

/* ------------------------------------------------------------------ */
/* Default content: the bundled manual (doubles as a parser torture test) */
/* ------------------------------------------------------------------ */

#define MD_MANUAL_RESOURCE "/org/example/mdviewer/manual.md"

static void render_markdown(MdApp* app, const gchar* markdown);

static void set_placeholder(MdApp* app)
{
    g_clear_pointer(&app->base_dir, g_free);

    GBytes* bytes = g_resources_lookup_data(MD_MANUAL_RESOURCE, G_RESOURCE_LOOKUP_FLAGS_NONE, NULL);
    if (bytes == NULL)
    {
        gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->text_view)),
                                 "md-viewer\n\n(manual resource not found)", -1);
        return;
    }

    gsize len = 0;
    const gchar* markdown = g_bytes_get_data(bytes, &len);
    gchar* markdown_copy = g_strndup(markdown, len);
    render_markdown(app, markdown_copy);
    g_free(markdown_copy);
    g_bytes_unref(bytes);
}

/* ------------------------------------------------------------------ */
/* Zoom                                                                */
/* ------------------------------------------------------------------ */

static void apply_zoom(MdApp* app)
{
    if (app->css == NULL)
    {
        app->css = gtk_css_provider_new();
        gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                                  GTK_STYLE_PROVIDER(app->css),
                                                  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }

    char css[64];
    g_snprintf(css, sizeof(css), "textview { font-size: %gpt; }", 12.0 * app->zoom);
    gtk_css_provider_load_from_data(app->css, css, -1, NULL);

    char label[16];
    g_snprintf(label, sizeof(label), "%d%%", (int)(app->zoom * 100.0));
    gtk_button_set_label(GTK_BUTTON(app->zoom_button), label);

    gtk_widget_set_sensitive(app->zoom_button, app->zoom != 1.0);
}

void md_app_zoom_reset(MdApp* app)
{
    g_return_if_fail(MD_IS_APP(app));
    app->zoom = 1.0;
    apply_zoom(app);
}

void md_app_zoom_in(MdApp* app)
{
    g_return_if_fail(MD_IS_APP(app));
    app->zoom = CLAMP(app->zoom * 1.1, MD_MIN_ZOOM, MD_MAX_ZOOM);
    apply_zoom(app);
}

void md_app_zoom_out(MdApp* app)
{
    g_return_if_fail(MD_IS_APP(app));
    app->zoom = CLAMP(app->zoom / 1.1, MD_MIN_ZOOM, MD_MAX_ZOOM);
    apply_zoom(app);
}

/* ------------------------------------------------------------------ */
/* Sidebar visibility                                                  */
/* ------------------------------------------------------------------ */

static void sync_sidebar_visibility(MdApp* app, gboolean visible)
{
    gtk_widget_set_visible(gtk_paned_get_child1(GTK_PANED(app->paned)), visible);
}

static void toggle_sidebar(GtkToggleButton* button, gpointer user_data)
{
    MdApp* app = user_data;
    sync_sidebar_visibility(app, gtk_toggle_button_get_active(button));
}

void md_app_toggle_sidebar(MdApp* app)
{
    g_return_if_fail(MD_IS_APP(app));
    gtk_toggle_button_set_active(
        GTK_TOGGLE_BUTTON(app->sidebar_button),
        !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->sidebar_button)));
}

void md_app_show_search(MdApp* app)
{
    g_return_if_fail(MD_IS_APP(app));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->search_button), TRUE);
    gtk_widget_set_visible(app->search_controls, TRUE);
    gtk_widget_grab_focus(app->search_entry);
}

static void search_toggled(GtkToggleButton* button, gpointer user_data)
{
    MdApp* app = user_data;
    gboolean active = gtk_toggle_button_get_active(button);
    gtk_widget_set_visible(app->search_controls, active);
    if (active)
        gtk_widget_grab_focus(app->search_entry);
}

/* ------------------------------------------------------------------ */
/* Sidebar outline (built from the parsed AST)                         */
/* ------------------------------------------------------------------ */

static void outline_append_text(const MdNode* node, GString* out)
{
    for (guint i = 0; i < node->children->len; i++)
    {
        const MdNode* child = g_ptr_array_index(node->children, i);
        if (child->kind == MD_NODE_TEXT)
            g_string_append(out, child->literal);
        else if (child->children != NULL)
            outline_append_text(child, out);
    }
}

static void outline_collect(const MdNode* node, GtkListStore* store)
{
    if (node->kind == MD_NODE_HEADING)
    {
        GString* text = g_string_new(NULL);
        outline_append_text(node, text);

        GString* row = g_string_new(NULL);
        for (int i = 1; i < node->level; i++)
            g_string_append_c(row, ' ');
        g_string_append(row, text->str);

        GtkTreeIter iter;
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, 0, row->str, 1, text->str, -1);
        g_string_free(row, TRUE);
        g_string_free(text, TRUE);
    }

    if (node->children != NULL)
        for (guint i = 0; i < node->children->len; i++)
            outline_collect(g_ptr_array_index(node->children, i), store);
}

static void populate_outline(MdApp* app, const MdNode* doc)
{
    GtkListStore* store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
    outline_collect(doc, store);
    gtk_tree_view_set_model(GTK_TREE_VIEW(app->outline_view), GTK_TREE_MODEL(store));
    g_object_unref(store);
}

static void outline_activated(GtkTreeView* view, GtkTreePath* path, GtkTreeViewColumn* column,
                              gpointer user_data)
{
    (void)view;
    (void)column;
    MdApp* app = user_data;

    GtkTreeModel* model = gtk_tree_view_get_model(view);
    GtkTreeIter iter;
    if (!gtk_tree_model_get_iter(model, &iter, path))
        return;

    gchar* text = NULL;
    gtk_tree_model_get(model, &iter, 1, &text, -1);
    if (text != NULL && *text != '\0')
    {
        GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->text_view));
        GtkTextIter start;
        gtk_text_buffer_get_start_iter(buffer, &start);
        GtkTextIter match_start, match_end;
        if (gtk_text_iter_forward_search(
                &start, text, GTK_TEXT_SEARCH_TEXT_ONLY | GTK_TEXT_SEARCH_CASE_INSENSITIVE,
                &match_start, &match_end, NULL))
        {
            gtk_text_buffer_place_cursor(buffer, &match_start);
            gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(app->text_view), &match_start, 0.0, FALSE,
                                         0.0, 0.0);
        }
    }
    g_free(text);
}

/* ------------------------------------------------------------------ */
/* Search                                                              */
/* ------------------------------------------------------------------ */

static void update_search_label(MdApp* app)
{
    guint count = app->search_matches->len;
    char msg[64];
    if (count == 0)
        g_snprintf(msg, sizeof(msg), "No matches");
    else if (count == 1)
        g_snprintf(msg, sizeof(msg), "1 match");
    else
        g_snprintf(msg, sizeof(msg), "%u of %u matches", app->search_index + 1, count);
    gtk_label_set_text(GTK_LABEL(app->search_results), msg);
}

static void goto_search_index(MdApp* app)
{
    if (app->search_matches->len == 0)
        return;
    guint idx = (guint)(app->search_index % (gint)app->search_matches->len);
    SearchMatch m = g_array_index(app->search_matches, SearchMatch, idx);
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->text_view));
    GtkTextIter iter;
    gtk_text_buffer_get_iter_at_offset(buffer, &iter, m.start);
    gtk_text_buffer_place_cursor(buffer, &iter);
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(app->text_view), &iter, 0.0, FALSE, 0.0, 0.0);
    update_search_label(app);
}

static void do_search(MdApp* app)
{
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->text_view));
    const gchar* query = gtk_entry_get_text(GTK_ENTRY(app->search_entry));

    g_array_set_size(app->search_matches, 0);
    app->search_index = 0;

    if (query != NULL && *query != '\0')
    {
        GtkTextIter start;
        gtk_text_buffer_get_start_iter(buffer, &start);
        gboolean found = TRUE;
        while (found)
        {
            GtkTextIter match_start, match_end;
            found = gtk_text_iter_forward_search(
                &start, query, GTK_TEXT_SEARCH_TEXT_ONLY | GTK_TEXT_SEARCH_CASE_INSENSITIVE,
                &match_start, &match_end, NULL);
            if (found)
            {
                SearchMatch m = {gtk_text_iter_get_offset(&match_start),
                                 gtk_text_iter_get_offset(&match_end)};
                g_array_append_val(app->search_matches, m);
                start = match_end;
            }
        }
    }

    update_search_label(app);
}

static void search_changed(GtkEntry* entry, gpointer user_data)
{
    (void)entry;
    do_search(MD_APP(user_data));
}

static void search_activate(GtkEntry* entry, gpointer user_data)
{
    (void)entry;
    goto_search_index(MD_APP(user_data));
}

static void search_prev_clicked(GtkButton* button, gpointer user_data)
{
    (void)button;
    MdApp* app = user_data;
    guint len = app->search_matches->len;
    if (len > 0)
    {
        app->search_index = (app->search_index + (gint)len - 1) % (gint)len;
        goto_search_index(app);
    }
}

static void search_next_clicked(GtkButton* button, gpointer user_data)
{
    (void)button;
    MdApp* app = user_data;
    if (app->search_matches->len > 0)
    {
        app->search_index = (app->search_index + 1) % (gint)app->search_matches->len;
        goto_search_index(app);
    }
}

/* ------------------------------------------------------------------ */
/* Recent files                                                        */
/* ------------------------------------------------------------------ */

static void rebuild_recent_menu(MdApp* app)
{
    g_menu_remove_all(app->recent_menu);

    if (app->recent_files->len == 0)
    {
        GMenuItem* empty = g_menu_item_new("No recent files", NULL);
        g_menu_append_item(app->recent_menu, empty);
        g_object_unref(empty);
        return;
    }

    for (guint i = 0; i < app->recent_files->len; i++)
    {
        const gchar* path = g_ptr_array_index(app->recent_files, i);
        gchar* basename = g_path_get_basename(path);
        GMenuItem* item = g_menu_item_new(basename, "app.open-recent");
        g_menu_item_set_action_and_target_value(item, "app.open-recent",
                                                g_variant_new_string(path));
        g_menu_append_item(app->recent_menu, item);
        g_object_unref(item);
        g_free(basename);
    }
}

static void record_recent_file(MdApp* app, const gchar* path)
{
    if (!md_recent_add(app->recent_files, path))
        return;

    GError* error = NULL;
    if (!md_recent_write(app->recent_path, app->recent_files, &error))
    {
        g_printerr("md-viewer: recent files not saved: %s\n",
                   error != NULL ? error->message : "unknown");
        g_clear_error(&error);
    }
    rebuild_recent_menu(app);
}

/* ------------------------------------------------------------------ */
/* Live reload                                                         */
/* ------------------------------------------------------------------ */

#define MD_RELOAD_DEBOUNCE_MS 200

static void reload_current_file(MdApp* app);
static void on_file_changed(GFileMonitor* monitor, GFile* file, GFile* other,
                            GFileMonitorEvent event, gpointer user_data);

static void stop_file_monitor(MdApp* app)
{
    if (app->reload_source != 0)
    {
        g_source_remove(app->reload_source);
        app->reload_source = 0;
    }
    g_clear_pointer(&app->file_monitor, g_object_unref);
}

static void start_file_monitor(MdApp* app, const gchar* path)
{
    stop_file_monitor(app);

    GFile* file = g_file_new_for_path(path);
    app->file_monitor = g_file_monitor(file, G_FILE_MONITOR_NONE, NULL, NULL);
    g_object_unref(file);

    if (app->file_monitor != NULL)
        g_signal_connect(app->file_monitor, "changed", G_CALLBACK(on_file_changed), app);
    else
        g_printerr("md-viewer: live reload unavailable for %s\n", path);
}

static gboolean reload_timeout(gpointer user_data)
{
    MdApp* app = user_data;
    app->reload_source = 0;
    reload_current_file(app);
    return G_SOURCE_REMOVE;
}

static void on_file_changed(GFileMonitor* monitor, GFile* file, GFile* other,
                            GFileMonitorEvent event, gpointer user_data)
{
    (void)monitor;
    (void)file;
    (void)other;
    MdApp* app = user_data;

    /* Editors save either in place (CHANGED) or by writing a temporary file
     * and renaming it over the original (DELETED + CREATED). Both should
     * reload; CHANGES_DONE_HINT is the tail of a burst. */
    switch (event)
    {
    case G_FILE_MONITOR_EVENT_CHANGED:
    case G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT:
    case G_FILE_MONITOR_EVENT_CREATED:
        break;
    default:
        return;
    }

    /* Editors write in bursts; collapse them into a single reload. */
    if (app->reload_source == 0)
        app->reload_source = g_timeout_add(MD_RELOAD_DEBOUNCE_MS, reload_timeout, app);
}

static void reload_current_file(MdApp* app)
{
    if (app->current_path == NULL)
        return;

    gchar* contents = NULL;
    GError* error = NULL;
    if (!g_file_get_contents(app->current_path, &contents, NULL, &error))
    {
        g_printerr("md-viewer: reload failed: %s\n", error != NULL ? error->message : "unknown");
        g_clear_error(&error);
        return;
    }

    /* inotify also fires on a plain touch; only re-render real content changes. */
    if (app->current_markdown != NULL && g_strcmp0(contents, app->current_markdown) == 0)
    {
        g_free(contents);
        return;
    }

    /* Keep the reader where they are: remember the visible range before the
     * buffer is cleared, then scroll to the same offset in the new render. */
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->text_view));
    gint scroll_offset = 0;
    GdkRectangle visible;
    gtk_text_view_get_visible_rect(GTK_TEXT_VIEW(app->text_view), &visible);
    GtkTextIter first_visible;
    gtk_text_view_get_line_at_y(GTK_TEXT_VIEW(app->text_view), &first_visible, visible.y, NULL);
    scroll_offset = gtk_text_iter_get_offset(&first_visible);

    g_free(app->current_markdown);
    app->current_markdown = contents;

    render_markdown(app, contents);

    if (scroll_offset > 0)
    {
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(buffer, &end);
        gint end_offset = gtk_text_iter_get_offset(&end);
        if (scroll_offset > end_offset)
            scroll_offset = end_offset;
        GtkTextIter iter;
        gtk_text_buffer_get_iter_at_offset(buffer, &iter, scroll_offset);
        gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(app->text_view), &iter, 0.0, FALSE, 0.0, 0.0);
    }

    guint ctx = gtk_statusbar_get_context_id(GTK_STATUSBAR(app->statusbar), "main");
    gtk_statusbar_remove_all(GTK_STATUSBAR(app->statusbar), ctx);
    gtk_statusbar_push(GTK_STATUSBAR(app->statusbar), ctx, "Reloaded (file changed on disk)");
}

void md_app_reload(MdApp* app)
{
    g_return_if_fail(MD_IS_APP(app));
    reload_current_file(app);
}

/* The document buffer. Exposed so the render suite can assert on what the app
 * actually painted; (transfer none). */
GtkTextBuffer* md_app_get_buffer(MdApp* app)
{
    g_return_val_if_fail(MD_IS_APP(app), NULL);
    if (app->text_view == NULL)
        return NULL;
    return gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->text_view));
}

/* ------------------------------------------------------------------ */
/* Dialogs                                                             */
/* ------------------------------------------------------------------ */

void md_app_show_about(MdApp* app)
{
    g_return_if_fail(MD_IS_APP(app));
    gtk_show_about_dialog(GTK_WINDOW(app->window), "program-name", "md-viewer", "version", "0.1.0",
                          "comments", "A lightweight Markdown viewer for technical docs.",
                          "logo-icon-name", "text-x-generic", NULL);
}

void md_app_show_open_dialog(MdApp* app)
{
    g_return_if_fail(MD_IS_APP(app));

    GtkFileChooserNative* native =
        gtk_file_chooser_native_new("Open Markdown file", GTK_WINDOW(app->window),
                                    GTK_FILE_CHOOSER_ACTION_OPEN, "_Open", "_Cancel");

    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Markdown");
    gtk_file_filter_add_pattern(filter, "*.md");
    gtk_file_filter_add_pattern(filter, "*.markdown");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(native), filter);

    if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(native)) == GTK_RESPONSE_ACCEPT)
    {
        gchar* path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(native));
        GError* error = NULL;
        md_app_open_file(app, path, &error);
        g_free(path);
        if (error != NULL)
        {
            g_printerr("md-viewer: %s\n", error->message);
            g_error_free(error);
        }
    }

    g_object_unref(native);
}

/* ------------------------------------------------------------------ */
/* Header bar                                                          */
/* ------------------------------------------------------------------ */

static GtkWidget* headerbar_new(MdApp* app)
{
    GtkWidget* header = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
    gtk_header_bar_set_title(GTK_HEADER_BAR(header), "md-viewer");
    gtk_header_bar_set_subtitle(GTK_HEADER_BAR(header), "No file open");

    app->sidebar_button = gtk_toggle_button_new();
    gtk_button_set_image(
        GTK_BUTTON(app->sidebar_button),
        gtk_image_new_from_icon_name("sidebar-show-symbolic", GTK_ICON_SIZE_BUTTON));
    gtk_widget_set_tooltip_text(app->sidebar_button, "Toggle sidebar (F9)");
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), app->sidebar_button);

    GtkWidget* open_button =
        gtk_button_new_from_icon_name("document-open-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(open_button, "Open a Markdown file (Ctrl+O)");
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), open_button);
    g_signal_connect_swapped(open_button, "clicked", G_CALLBACK(md_app_show_open_dialog), app);

    app->search_button = gtk_toggle_button_new();
    gtk_button_set_image(GTK_BUTTON(app->search_button),
                         gtk_image_new_from_icon_name("edit-find-symbolic", GTK_ICON_SIZE_BUTTON));
    gtk_widget_set_tooltip_text(app->search_button, "Search (Ctrl+F)");
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), app->search_button);
    g_signal_connect(app->search_button, "toggled", G_CALLBACK(search_toggled), app);

    GtkWidget* menu_button = gtk_menu_button_new();
    gtk_button_set_image(GTK_BUTTON(menu_button),
                         gtk_image_new_from_icon_name("open-menu-symbolic", GTK_ICON_SIZE_BUTTON));
    GMenu* menu = g_menu_new();
    app->recent_menu = g_menu_new();
    rebuild_recent_menu(app);
    GMenuItem* recent_item = g_menu_item_new("Open recent", NULL);
    g_menu_item_set_submenu(recent_item, G_MENU_MODEL(app->recent_menu));
    g_menu_append_item(menu, recent_item);
    g_object_unref(recent_item);
    g_menu_append(menu, "Reload file", "app.reload");
    g_menu_append(menu, "About md-viewer", "app.about");
    g_menu_append(menu, "Quit", "app.quit");
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menu_button), G_MENU_MODEL(menu));
    g_object_unref(menu);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), menu_button);

    GtkWidget* zoom_out = gtk_button_new_from_icon_name("zoom-out-symbolic", GTK_ICON_SIZE_BUTTON);
    GtkWidget* zoom_in = gtk_button_new_from_icon_name("zoom-in-symbolic", GTK_ICON_SIZE_BUTTON);
    app->zoom_button = gtk_button_new_with_label("100%");
    gtk_widget_set_tooltip_text(zoom_out, "Zoom out (Ctrl+-)");
    gtk_widget_set_tooltip_text(zoom_in, "Zoom in (Ctrl+= or Ctrl++)");
    gtk_widget_set_tooltip_text(app->zoom_button, "Reset zoom (Ctrl+0)");

    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), zoom_out);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), zoom_in);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header), app->zoom_button);

    g_signal_connect(app->sidebar_button, "toggled", G_CALLBACK(toggle_sidebar), app);
    g_signal_connect_swapped(zoom_out, "clicked", G_CALLBACK(md_app_zoom_out), app);
    g_signal_connect_swapped(zoom_in, "clicked", G_CALLBACK(md_app_zoom_in), app);
    g_signal_connect_swapped(app->zoom_button, "clicked", G_CALLBACK(md_app_zoom_reset), app);

    return header;
}

/* ------------------------------------------------------------------ */
/* Sidebar                                                             */
/* ------------------------------------------------------------------ */

static GtkWidget* sidebar_new(MdApp* app)
{
    app->outline_view = gtk_tree_view_new();

    GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn* col =
        gtk_tree_view_column_new_with_attributes("Outline", renderer, "text", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(app->outline_view), col);
    g_signal_connect(app->outline_view, "row-activated", G_CALLBACK(outline_activated), app);

    GtkWidget* scrolled = gtk_scrolled_window_new(NULL, NULL);
    /* AUTOMATIC (not NEVER) so the pane's minimum width tracks the scrollbar
     * instead of the widest outline row, letting the sidebar shrink freely. */
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scrolled, MD_SIDEBAR_MIN_WIDTH, -1);
    gtk_container_add(GTK_CONTAINER(scrolled), app->outline_view);
    return scrolled;
}

/* ------------------------------------------------------------------ */
/* Search controls (live in the status bar)                            */
/* ------------------------------------------------------------------ */

static void search_close_clicked(GtkButton* button, gpointer user_data)
{
    (void)button;
    MdApp* app = user_data;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->search_button), FALSE);
}

static GtkWidget* search_controls_new(MdApp* app)
{
    app->search_controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

    app->search_entry = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(app->search_entry), "Search");
    g_signal_connect(app->search_entry, "search-changed", G_CALLBACK(search_changed), app);
    g_signal_connect(app->search_entry, "activate", G_CALLBACK(search_activate), app);

    app->search_results = gtk_label_new("");

    GtkWidget* prev = gtk_button_new_from_icon_name("go-up-symbolic", GTK_ICON_SIZE_BUTTON);
    GtkWidget* next = gtk_button_new_from_icon_name("go-down-symbolic", GTK_ICON_SIZE_BUTTON);
    GtkWidget* close = gtk_button_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_BUTTON);
    g_signal_connect(prev, "clicked", G_CALLBACK(search_prev_clicked), app);
    g_signal_connect(next, "clicked", G_CALLBACK(search_next_clicked), app);
    g_signal_connect(close, "clicked", G_CALLBACK(search_close_clicked), app);

    gtk_box_pack_start(GTK_BOX(app->search_controls), app->search_entry, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(app->search_controls), app->search_results, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(app->search_controls), prev, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(app->search_controls), next, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(app->search_controls), close, FALSE, FALSE, 0);

    return app->search_controls;
}

/* ------------------------------------------------------------------ */
/* Document pane                                                       */
/* ------------------------------------------------------------------ */

static void set_readonly_cursor(GtkWidget* widget, gpointer user_data)
{
    (void)user_data;
    GdkCursor* cursor = gdk_cursor_new_for_display(gdk_display_get_default(), GDK_ARROW);
    GtkTextView* view = GTK_TEXT_VIEW(widget);
    gdk_window_set_cursor(gtk_text_view_get_window(view, GTK_TEXT_WINDOW_TEXT), cursor);
    g_object_unref(cursor);
}

static GtkWidget* document_pane_new(MdApp* app)
{
    app->text_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(app->text_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(app->text_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app->text_view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(app->text_view), 40);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(app->text_view), 40);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(app->text_view), 20);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(app->text_view), 20);
    g_signal_connect(app->text_view, "realize", G_CALLBACK(set_readonly_cursor), NULL);

    GtkWidget* scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scrolled), app->text_view);
    return scrolled;
}

/* ------------------------------------------------------------------ */
/* Statusbar (hosts the search controls)                               */
/* ------------------------------------------------------------------ */

static GtkWidget* statusbar_new(MdApp* app)
{
    GtkWidget* controls = search_controls_new(app);

    app->statusbar = gtk_statusbar_new();
    gtk_widget_set_hexpand(app->statusbar, TRUE);
    gtk_statusbar_push(GTK_STATUSBAR(app->statusbar),
                       gtk_statusbar_get_context_id(GTK_STATUSBAR(app->statusbar), "main"),
                       "No file open");

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(box), app->statusbar, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(box), controls, FALSE, FALSE, 0);
    return box;
}

/* ------------------------------------------------------------------ */
/* File open                                                           */
/* ------------------------------------------------------------------ */

static gchar* read_file_to_string(const gchar* path, GError** error)
{
    gchar* contents = NULL;
    if (!g_file_get_contents(path, &contents, NULL, error))
        return NULL;
    return contents;
}

/* Image width cap from GSettings; 0 (auto-fit) when the schema is missing. */
static guint md_image_max_width(void)
{
    GSettingsSchemaSource* source = g_settings_schema_source_get_default();
    GSettingsSchema* schema = g_settings_schema_source_lookup(source, "org.example.mdviewer", TRUE);
    if (schema == NULL)
        return 0;
    GSettings* settings = g_settings_new("org.example.mdviewer");
    gint value = g_settings_get_int(settings, "image-max-width");
    g_object_unref(settings);
    g_settings_schema_unref(schema);
    return value > 0 ? (guint)value : 0;
}

static void render_markdown(MdApp* app, const gchar* markdown)
{
    GError* error = NULL;
    MdOptions opts = md_options_default();
    md_options_set(&opts, MD_FEATURE_GFM_TABLES, TRUE);
    md_options_set(&opts, MD_FEATURE_GFM_STRIKETHROUGH, TRUE);
    md_options_set(&opts, MD_FEATURE_GFM_TASKLISTS, TRUE);
    md_options_set(&opts, MD_FEATURE_GFM_AUTOLINKS, TRUE);
    md_options_set(&opts, MD_FEATURE_GFM_RAW_HTML, TRUE);
    MdNode* doc = md_parse(markdown, &opts, &error);
    if (doc == NULL)
    {
        g_printerr("md-viewer: parse error: %s\n", error != NULL ? error->message : "unknown");
        g_clear_error(&error);
        return;
    }
    MdRenderOptions options = {0};
    options.view = GTK_TEXT_VIEW(app->text_view);
    options.session = app->session;
    options.base_dir = app->base_dir;
    options.max_width = app->image_max_width;
    md_render_to_buffer(doc, &options);
    populate_outline(app, doc);
    md_node_free(doc);

    /* Re-run the current query so search results survive a reload. */
    do_search(app);
}

void md_app_open_file(MdApp* app, const gchar* path, GError** error)
{
    g_return_if_fail(MD_IS_APP(app));
    g_return_if_fail(path != NULL);

    gchar* markdown = read_file_to_string(path, error);
    if (markdown == NULL)
        return;

    g_free(app->base_dir);
    app->base_dir = g_path_get_dirname(path);

    g_free(app->current_path);
    app->current_path = g_strdup(path);
    g_free(app->current_markdown);
    app->current_markdown = markdown;

    render_markdown(app, markdown);

    gchar* basename = g_path_get_basename(path);
    gtk_header_bar_set_subtitle(GTK_HEADER_BAR(app->headerbar), path);
    gtk_window_set_title(GTK_WINDOW(app->window), basename);
    g_free(basename);

    guint ctx = gtk_statusbar_get_context_id(GTK_STATUSBAR(app->statusbar), "main");
    gtk_statusbar_remove_all(GTK_STATUSBAR(app->statusbar), ctx);
    gtk_statusbar_push(GTK_STATUSBAR(app->statusbar), ctx, path);

    record_recent_file(app, path);
    start_file_monitor(app, path);
}

/* Queue a file to open now (window exists) or once activate builds it. */
static void queue_file(MdApp* app, const gchar* path)
{
    if (app->window != NULL)
    {
        GError* error = NULL;
        md_app_open_file(app, path, &error);
        if (error != NULL)
        {
            g_printerr("md-viewer: %s\n", error->message);
            g_error_free(error);
        }
    }
    else
    {
        g_ptr_array_add(app->pending_files, g_strdup(path));
    }
}

/* Handles positional file arguments (as passed by the desktop file's %F and
 * by the -f translation in main.c). Builds the window on first launch; on a
 * handoff the existing window is reused. */
static void md_app_open(GApplication* application, GFile** files, gint n_files, const gchar* hint)
{
    (void)hint;
    MdApp* app = MD_APP(application);
    gboolean had_window = (app->window != NULL);
    for (gint i = 0; i < n_files; i++)
    {
        gchar* path = g_file_get_path(files[i]);
        if (path != NULL)
        {
            queue_file(app, path);
            g_free(path);
        }
    }
    if (!had_window)
        g_application_activate(application); /* build the window; opens queued files */
}

/* ------------------------------------------------------------------ */
/* Application lifecycle                                               */
/* ------------------------------------------------------------------ */

static void md_app_activate(GApplication* application)
{
    MdApp* app = MD_APP(application);
    if (app->window != NULL)
    {
        gtk_window_present(GTK_WINDOW(app->window));
        return;
    }

    app->zoom = 1.0;

    md_shortcuts_register(app);

    GtkWidget* header = headerbar_new(app);
    app->headerbar = header;
    GtkWidget* sidebar = sidebar_new(app);
    GtkWidget* document = document_pane_new(app);
    GtkWidget* statusbar = statusbar_new(app);

    app->paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_pack1(GTK_PANED(app->paned), sidebar, FALSE, FALSE);
    gtk_paned_pack2(GTK_PANED(app->paned), document, TRUE, FALSE);

    GtkWidget* layout = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(layout), app->paned, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(layout), statusbar, FALSE, FALSE, 0);

    app->window = gtk_application_window_new(GTK_APPLICATION(app));
    gtk_window_set_title(GTK_WINDOW(app->window), "md-viewer");
    gtk_window_set_default_size(GTK_WINDOW(app->window), 900, 640);
    gtk_window_set_titlebar(GTK_WINDOW(app->window), header);
    gtk_container_add(GTK_CONTAINER(app->window), layout);

    /* Sidebar defaults to ~12% of the window width. */
    gint win_w = 0, win_h = 0;
    gtk_window_get_default_size(GTK_WINDOW(app->window), &win_w, &win_h);
    gtk_paned_set_position(GTK_PANED(app->paned), (gint)(win_w * MD_SIDEBAR_FRACTION));

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->sidebar_button), TRUE);
    set_placeholder(app);
    apply_zoom(app);

    if (app->pending_files->len > 0)
    {
        for (guint i = 0; i < app->pending_files->len; i++)
        {
            GError* error = NULL;
            md_app_open_file(app, g_ptr_array_index(app->pending_files, i), &error);
            if (error != NULL)
            {
                g_printerr("md-viewer: %s\n", error->message);
                g_error_free(error);
            }
        }
        g_ptr_array_set_size(app->pending_files, 0);
    }

    gtk_widget_show_all(app->window);
    gtk_widget_hide(app->search_controls);
}

static void md_app_init(MdApp* app)
{
    app->pending_files = g_ptr_array_new_with_free_func(g_free);
    app->search_matches = g_array_new(FALSE, TRUE, sizeof(SearchMatch));
    app->session = soup_session_new();
    app->image_max_width = md_image_max_width();
    app->recent_path = md_recent_default_path();
    app->recent_files = md_recent_read(app->recent_path);
}

static void md_app_dispose(GObject* object)
{
    MdApp* app = MD_APP(object);
    /* A pending reload holds a timeout referencing the app; drop it first. */
    stop_file_monitor(app);
    /* In-flight image loads hold a borrowed pointer to the session; cancel them
     * before the session goes away. */
    if (app->text_view != NULL)
        md_render_cancel_loads(GTK_TEXT_VIEW(app->text_view));
    g_clear_pointer(&app->pending_files, g_ptr_array_unref);
    g_clear_pointer(&app->recent_files, g_ptr_array_unref);
    g_clear_pointer(&app->search_matches, g_array_unref);
    g_clear_pointer(&app->session, g_object_unref);
    g_clear_pointer(&app->css, g_object_unref);
    g_clear_pointer(&app->base_dir, g_free);
    g_clear_pointer(&app->current_path, g_free);
    g_clear_pointer(&app->current_markdown, g_free);
    g_clear_pointer(&app->recent_path, g_free);
    G_OBJECT_CLASS(md_app_parent_class)->dispose(object);
}

static void md_app_class_init(MdAppClass* klass)
{
    G_OBJECT_CLASS(klass)->dispose = md_app_dispose;
    G_APPLICATION_CLASS(klass)->activate = md_app_activate;
    G_APPLICATION_CLASS(klass)->open = md_app_open;
}

MdApp* md_app_new(void)
{
    /* CLI options are handled in main.c: -f/--file is rewritten into a
     * positional argument so GApplication's open signal covers both a fresh
     * launch and a handoff to a running instance. */
    return g_object_new(MD_TYPE_APP, "application-id", "org.example.mdviewer", "flags",
                        G_APPLICATION_HANDLES_OPEN, NULL);
}