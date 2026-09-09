#include "app.h"
#include <glib.h>
#include <string.h>

/* Translate a -f/--file argument into a positional file so GApplication
 * routes it through the open signal. This works uniformly for a fresh launch
 * and for a handoff to an already-running instance. */
static gchar** cli_argv_with_file(int argc, char* argv[], int* out_argc)
{
    for (int i = 1; i < argc; i++)
    {
        const gchar* file = NULL;
        if ((strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--file") == 0) && i + 1 < argc)
            file = argv[i + 1];
        else if (g_str_has_prefix(argv[i], "--file="))
            file = argv[i] + strlen("--file=");

        if (file != NULL)
        {
            gchar** args = g_new(gchar*, 3);
            args[0] = argv[0];
            args[1] = (gchar*)file;
            args[2] = NULL;
            *out_argc = 2;
            return args;
        }
    }
    *out_argc = argc;
    return argv;
}

int main(int argc, char* argv[])
{
    int run_argc = 0;
    gchar** run_argv = cli_argv_with_file(argc, argv, &run_argc);

    MdApp* app = md_app_new();
    int status = g_application_run(G_APPLICATION(app), run_argc, run_argv);
    g_object_unref(app);

    if (run_argv != argv)
        g_free(run_argv);
    return status;
}