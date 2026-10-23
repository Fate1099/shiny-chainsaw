/* Recent-file list: ordering, deduplication, truncation, round trip.
 *
 * src/recent.c is GLib only, so this suite runs without a display. */
#include "recent.h"
#include <assert.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    gchar* dir = g_strdup_printf("%s/shiny-chainsaw-recent-%u", g_get_tmp_dir(), g_random_int());
    g_mkdir_with_parents(dir, 0700);
    gchar* list_file = g_build_filename(dir, "recent.txt", NULL);
    gchar* a = g_build_filename(dir, "a.md", NULL);
    gchar* b = g_build_filename(dir, "b.md", NULL);
    g_file_set_contents(a, "a", -1, NULL);
    g_file_set_contents(b, "b", -1, NULL);

    /* A missing list is an empty list, not an error. */
    GPtrArray* list = md_recent_read(list_file);
    assert(list->len == 0);

    /* Most recently opened first. */
    assert(md_recent_add(list, a));
    assert(md_recent_add(list, b));
    assert(list->len == 2);
    assert(g_strcmp0(g_ptr_array_index(list, 0), b) == 0);
    assert(g_strcmp0(g_ptr_array_index(list, 1), a) == 0);

    /* Re-opening moves the entry to the front instead of duplicating it. */
    assert(md_recent_add(list, a));
    assert(list->len == 2);
    assert(g_strcmp0(g_ptr_array_index(list, 0), a) == 0);

    /* Already the newest entry: nothing changes. */
    assert(!md_recent_add(list, a));
    assert(list->len == 2);

    /* NULL and empty paths are ignored. */
    assert(!md_recent_add(list, NULL));
    assert(!md_recent_add(list, ""));
    assert(list->len == 2);

    /* Relative paths are stored absolute. */
    assert(md_recent_add(list, "relative-name.md"));
    assert(g_path_is_absolute(g_ptr_array_index(list, 0)));
    g_ptr_array_remove_index(list, 0);

    /* Truncation: the oldest entry falls off. */
    for (int i = 0; i < MD_RECENT_MAX + 5; i++)
    {
        gchar* path = g_strdup_printf("%s/f%d.md", dir, i);
        md_recent_add(list, path);
        g_free(path);
    }
    assert(list->len == MD_RECENT_MAX);
    gchar* newest = g_strdup_printf("%s/f%d.md", dir, MD_RECENT_MAX + 4);
    gchar* oldest = g_strdup_printf("%s/f%d.md", dir, 5);
    assert(g_strcmp0(g_ptr_array_index(list, 0), newest) == 0);
    assert(g_strcmp0(g_ptr_array_index(list, list->len - 1), oldest) == 0);
    g_free(newest);
    g_free(oldest);

    /* Round trip through the file. */
    GError* error = NULL;
    assert(md_recent_write(list_file, list, &error));
    assert(error == NULL);

    GPtrArray* reloaded = md_recent_read(list_file);
    assert(reloaded->len == list->len);
    for (guint i = 0; i < list->len; i++)
        assert(g_strcmp0(g_ptr_array_index(reloaded, i), g_ptr_array_index(list, i)) == 0);

    g_ptr_array_unref(reloaded);
    g_ptr_array_unref(list);

    g_remove(a);
    g_remove(b);
    g_remove(list_file);
    g_remove(dir);
    g_free(a);
    g_free(b);
    g_free(list_file);
    g_free(dir);

    printf("test_recent: ordering, dedup, truncation and round trip ok\n");
    return 0;
}
