#include "app.h"
#include <glib.h>
#include <string.h>

/* Translate a -f/--file argument into a positional file so GApplication routes
 * it through the open signal. This works uniformly for a fresh launch and for
 * a handoff to an already-running instance. Every other argument is kept. */
static gchar** cli_argv_with_file(int argc, char* argv[], int* out_argc)
{
    for (int i = 1; i < argc; i++)
    {
        const gchar* arg = argv[i];
        const gchar* file = NULL;
        int consumed = 1;

        if (arg == NULL)
            continue;

        if (strcmp(arg, "-f") == 0 || strcmp(arg, "--file") == 0)
        {
            if (i + 1 >= argc)
                continue;
            file = argv[i + 1];
            consumed = 2;
        }
        else if (g_str_has_prefix(arg, "--file="))
        {
            file = arg + strlen("--file=");
        }

        if (file == NULL)
            continue;

        gchar** args = g_new(gchar*, (gsize)argc + 1);
        int n = 0;
        for (int j = 0; j < i; j++)
            args[n++] = argv[j];
        args[n++] = (gchar*)file;
        for (int j = i + consumed; j < argc; j++)
            args[n++] = argv[j];
        args[n] = NULL;
        *out_argc = n;
        return args;
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
        g_free((void*)run_argv);
    return status;
}