/* Live reload and the recent-file list, driven through the real GtkApplication.
 *
 * Needs a display like the render suite and prints "skipped" (exit 0) when
 * there is none. XDG_CONFIG_HOME is pointed at a temporary directory so the
 * test never touches the developer's real recent-file list. */
#include "app.h"
#include "recent.h"
#include <assert.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

static gboolean buffer_contains(GtkTextBuffer* buffer, const char* needle)
{
    GtkTextIter start, end;
    gtk_text_buffer_get_start_iter(buffer, &start);
    gtk_text_buffer_get_end_iter(buffer, &end);
    gchar* text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    gboolean found = strstr(text, needle) != NULL;
    g_free(text);
    return found;
}

/* Pump the main context until the monitor's debounce has fired. */
static void pump_until(GtkTextBuffer* buffer, const char* needle)
{
    gint64 deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
    while (g_get_monotonic_time() < deadline && !buffer_contains(buffer, needle))
    {
        g_main_context_iteration(NULL, FALSE);
        g_usleep(20000);
    }
}

int main(void)
{
    if (!gtk_init_check(NULL, NULL))
    {
        printf("live_reload: skipped (no display)\n");
        return 0;
    }

    guint stamp = g_random_int();
    gchar* dir = g_strdup_printf("%s/md-viewer-live-%u", g_get_tmp_dir(), stamp);
    gchar* config = g_strdup_printf("%s/md-viewer-live-config-%u", g_get_tmp_dir(), stamp);
    g_mkdir_with_parents(dir, 0700);
    g_mkdir_with_parents(config, 0700);
    g_setenv("XDG_CONFIG_HOME", config, TRUE);

    gchar* path = g_build_filename(dir, "live.md", NULL);
    gchar* other = g_build_filename(dir, "other.md", NULL);
    g_file_set_contents(path, "# Alpha\n\nthe first version\n", -1, NULL);
    g_file_set_contents(other, "# Other file\n", -1, NULL);

    MdApp* app = md_app_new();

    /* A test-only id: registering under the real one would hand the activate
     * over to a md-viewer that is already running and leave this instance
     * without a window. */
    g_application_set_application_id(G_APPLICATION(app), "org.example.mdviewer.test");

    GError* register_error = NULL;
    if (!g_application_register(G_APPLICATION(app), NULL, &register_error))
    {
        g_printerr("live_reload: %s\n",
                   register_error != NULL ? register_error->message : "unknown");
        return 1;
    }
    g_application_activate(G_APPLICATION(app));

    GError* error = NULL;
    md_app_open_file(app, path, &error);
    if (error != NULL)
    {
        g_printerr("live_reload: could not open %s: %s\n", path, error->message);
        return 1;
    }

    GtkTextBuffer* buffer = md_app_get_buffer(app);
    assert(buffer != NULL);
    assert(buffer_contains(buffer, "Alpha"));

    /* Opening a file records it, and the record is written to disk. */
    gchar* recent_file = md_recent_default_path();
    gchar* recent_contents = NULL;
    assert(g_file_get_contents(recent_file, &recent_contents, NULL, NULL));
    assert(strstr(recent_contents, path) != NULL);
    g_free(recent_contents);

    /* The monitor reloads on its own when the file changes. */
    g_file_set_contents(path, "# Beta\n\nthe second version\n", -1, NULL);
    pump_until(buffer, "Beta");
    assert(buffer_contains(buffer, "Beta"));
    assert(!buffer_contains(buffer, "Alpha"));

    /* A menu entry from the recent list opens the file it carries. */
    GAction* open_recent = g_action_map_lookup_action(G_ACTION_MAP(app), "open-recent");
    assert(open_recent != NULL);
    g_action_activate(open_recent, g_variant_new_string(other));
    assert(buffer_contains(buffer, "Other file"));

    /* The reload action re-reads the file on demand. */
    g_file_set_contents(other, "# Reloaded\n", -1, NULL);
    md_app_reload(app);
    assert(buffer_contains(buffer, "Reloaded"));

    /* Most recently opened first. */
    recent_contents = NULL;
    assert(g_file_get_contents(recent_file, &recent_contents, NULL, NULL));
    gchar** lines = g_strsplit(recent_contents, "\n", -1);
    assert(g_strcmp0(lines[0], other) == 0);
    assert(g_strcmp0(lines[1], path) == 0);
    g_strfreev(lines);
    g_free(recent_contents);
    g_free(recent_file);

    g_object_unref(app);
    g_remove(path);
    g_remove(other);
    g_remove(g_build_filename(config, "md-viewer", "recent.txt", NULL));
    g_remove(g_build_filename(config, "md-viewer", NULL));
    g_remove(config);
    g_remove(dir);
    g_free(path);
    g_free(other);
    g_free(dir);
    g_free(config);

    printf("live_reload: monitor reload, recent list and reload action ok\n");
    return 0;
}
