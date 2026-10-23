#ifndef MD_RECENT_H
#define MD_RECENT_H

#include <glib.h>

G_BEGIN_DECLS

#define MD_RECENT_MAX 10

/**
 * Path backing the recent-file list: "<XDG config dir>/shiny-chainsaw/recent.txt".
 * (transfer full)
 */
gchar* md_recent_default_path(void);

/**
 * Read the recent list, most recently opened first. A missing or empty file is
 * not an error: the returned array is simply empty.
 *
 * @param file (transfer none) path of the list file.
 * @return (transfer full) GPtrArray of gchar*, at most MD_RECENT_MAX entries.
 */
GPtrArray* md_recent_read(const gchar* file);

/**
 * Put @path at the front of @list, dropping any older copy of it, and trim the
 * list to MD_RECENT_MAX. Paths are made absolute so the same file opened twice
 * through different spellings is stored once.
 *
 * @param list (transfer none) the live list.
 * @param path (transfer none) file that was just opened.
 * @return TRUE if the list changed.
 */
gboolean md_recent_add(GPtrArray* list, const gchar* path);

/**
 * Write the list, one absolute path per line, creating the parent directory.
 *
 * @param file  (transfer none) path of the list file.
 * @param list  (transfer none) the live list.
 * @param error (transfer none) set on failure.
 * @return FALSE on failure.
 */
gboolean md_recent_write(const gchar* file, GPtrArray* list, GError** error);

G_END_DECLS

#endif /* MD_RECENT_H */
