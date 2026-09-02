#include "app.h"
#include <glib.h>

static gchar *
parse_args(int argc, char *argv[], GError **error)
{
    gchar *file = NULL;

    const GOptionEntry entries[] = {
        { "file", 'f', 0, G_OPTION_ARG_FILENAME, &file,
          "Markdown file to open", "FILE" },
        { NULL }
    };

    GOptionContext *context = g_option_context_new("- lightweight Markdown viewer");
    g_option_context_add_main_entries(context, entries, NULL);

    if (!g_option_context_parse(context, &argc, &argv, error))
    {
        g_option_context_free(context);
        return NULL;
    }

    g_option_context_free(context);
    return file;
}

int
main(int argc, char *argv[])
{
    GError *error = NULL;

    gchar *file = parse_args(argc, argv, &error);
    if (error != NULL)
    {
        g_printerr("md-viewer: %s\n", error->message);
        g_error_free(error);
        return 1;
    }

    MdApp *app = md_app_new();
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    g_free(file);

    return status;
}