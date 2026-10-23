/* Recently opened files, persisted one absolute path per line.
 *
 * GLib only, like the parser, so the list can be exercised without a display. */
#include "recent.h"

gchar* md_recent_default_path(void)
{
    return g_build_filename(g_get_user_config_dir(), "shiny-chainsaw", "recent.txt", NULL);
}

/* Absolute paths are stored as given; relative ones are resolved against the
 * current directory so the same file is not listed twice under two spellings. */
static gchar* normalize_path(const gchar* path)
{
    if (g_path_is_absolute(path))
        return g_strdup(path);

    gchar* cwd = g_get_current_dir();
    gchar* absolute = g_build_filename(cwd, path, NULL);
    g_free(cwd);
    return absolute;
}

GPtrArray* md_recent_read(const gchar* file)
{
    GPtrArray* list = g_ptr_array_new_with_free_func(g_free);

    gchar* contents = NULL;
    if (!g_file_get_contents(file, &contents, NULL, NULL))
        return list; /* no list yet is normal, not an error */

    gchar** lines = g_strsplit(contents, "\n", -1);
    for (guint i = 0; lines[i] != NULL && list->len < MD_RECENT_MAX; i++)
    {
        gchar* line = g_strstrip(lines[i]);
        if (*line != '\0')
            g_ptr_array_add(list, g_strdup(line));
    }
    g_strfreev(lines);
    g_free(contents);

    return list;
}

gboolean md_recent_add(GPtrArray* list, const gchar* path)
{
    if (list == NULL || path == NULL || *path == '\0')
        return FALSE;

    gchar* absolute = normalize_path(path);

    for (guint i = 0; i < list->len; i++)
    {
        if (g_strcmp0(g_ptr_array_index(list, i), absolute) == 0)
        {
            if (i == 0)
            {
                g_free(absolute);
                return FALSE; /* already the newest entry */
            }
            g_ptr_array_remove_index(list, i);
            break;
        }
    }

    g_ptr_array_insert(list, 0, absolute);
    while (list->len > MD_RECENT_MAX)
        g_ptr_array_remove_index(list, list->len - 1);

    return TRUE;
}

gboolean md_recent_write(const gchar* file, GPtrArray* list, GError** error)
{
    gchar* dir = g_path_get_dirname(file);
    gboolean ok = g_mkdir_with_parents(dir, 0700) == 0;
    g_free(dir);

    if (ok)
    {
        GString* out = g_string_new(NULL);
        for (guint i = 0; i < list->len; i++)
            g_string_append_printf(out, "%s\n", (const gchar*)g_ptr_array_index(list, i));
        ok = g_file_set_contents(file, out->str, -1, error);
        g_string_free(out, TRUE);
    }

    return ok;
}
