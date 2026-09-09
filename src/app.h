#ifndef MD_APP_H
#define MD_APP_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define MD_TYPE_APP (md_app_get_type())

G_DECLARE_FINAL_TYPE(MdApp, md_app, MD, APP, GtkApplication)

MdApp* md_app_new(void);

void md_app_open_file(MdApp* app, const gchar* path, GError** error);
void md_app_show_open_dialog(MdApp* app);
void md_app_show_about(MdApp* app);
void md_app_toggle_sidebar(MdApp* app);
void md_app_show_search(MdApp* app);
void md_app_zoom_in(MdApp* app);
void md_app_zoom_out(MdApp* app);
void md_app_zoom_reset(MdApp* app);

G_END_DECLS

#endif /* MD_APP_H */