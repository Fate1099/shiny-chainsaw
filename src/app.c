#include "app.h"
#include "parser.h"

struct _MdApp
{
    GtkApplication parent_instance;
};

G_DEFINE_TYPE(MdApp, md_app, GTK_TYPE_APPLICATION)

static gchar *
read_file_to_string(const gchar *path, GError **error)
{
    gchar *contents = NULL;
    if (!g_file_get_contents(path, &contents, NULL, error))
        return NULL;
    return contents;
}

void
md_app_open_file(MdApp *app, const gchar *path, GError **error)
{
    g_return_if_fail(MD_IS_APP(app));
    g_return_if_fail(path != NULL);

    gchar *markdown = read_file_to_string(path, error);
    if (markdown == NULL)
        return;

    GError *parse_error = NULL;
    gchar *html = md_parser_parse(markdown, &parse_error);
    g_free(markdown);

    if (html == NULL)
    {
        g_propagate_error(error, parse_error);
        return;
    }

    /* TODO: render html into the GtkTextView via GtkTextBuffer markup. */
    g_free(html);
}

static void
md_app_activate(GApplication *application)
{
    MdApp *app = MD_APP(application);

    GtkWidget *window = gtk_application_window_new(GTK_APPLICATION(app));
    gtk_window_set_title(GTK_WINDOW(window), "md-viewer");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);

    GtkWidget *view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);

    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scrolled), view);
    gtk_container_add(GTK_CONTAINER(window), scrolled);

    gtk_widget_show_all(window);
}

static void
md_app_init(MdApp *app)
{
    (void)app;
}

static void
md_app_class_init(MdAppClass *klass)
{
    G_APPLICATION_CLASS(klass)->activate = md_app_activate;
}

MdApp *
md_app_new(void)
{
    return g_object_new(MD_TYPE_APP,
                        "application-id", "org.example.mdviewer",
                        NULL);
}