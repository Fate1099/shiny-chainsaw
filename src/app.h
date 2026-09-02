#ifndef MD_APP_H
#define MD_APP_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define MD_TYPE_APP (md_app_get_type())

G_DECLARE_FINAL_TYPE(MdApp, md_app, MD, APP, GtkApplication)

MdApp *md_app_new(void);

void md_app_open_file(MdApp *app, const gchar *path, GError **error);

G_END_DECLS

#endif /* MD_APP_H */