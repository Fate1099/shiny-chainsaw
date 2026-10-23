#include "shortcuts.h"

typedef void (*MdActivateFunc)(MdApp* app, GVariant* parameter);

typedef struct
{
    const gchar* name;      /* action suffix, registered as "app.<name>" */
    const gchar* accels[3]; /* GTK accelerator strings, NULL-terminated */
    MdActivateFunc handler;
    const GVariantType* parameter; /* argument type of the action, NULL for none */
} MdShortcut;

static void quit_activated(MdApp* app, GVariant* parameter)
{
    (void)parameter;
    g_application_quit(G_APPLICATION(app));
}

static void open_activated(MdApp* app, GVariant* parameter)
{
    (void)parameter;
    md_app_show_open_dialog(app);
}

static void open_recent_activated(MdApp* app, GVariant* parameter)
{
    const gchar* path = (parameter != NULL) ? g_variant_get_string(parameter, NULL) : NULL;
    if (path == NULL || *path == '\0')
        return;

    GError* error = NULL;
    md_app_open_file(app, path, &error);
    if (error != NULL)
    {
        g_printerr("md-viewer: %s\n", error->message);
        g_clear_error(&error);
    }
}

static void reload_activated(MdApp* app, GVariant* parameter)
{
    (void)parameter;
    md_app_reload(app);
}

static void search_activated(MdApp* app, GVariant* parameter)
{
    (void)parameter;
    md_app_show_search(app);
}

static void sidebar_activated(MdApp* app, GVariant* parameter)
{
    (void)parameter;
    md_app_toggle_sidebar(app);
}

static void zoom_in_activated(MdApp* app, GVariant* parameter)
{
    (void)parameter;
    md_app_zoom_in(app);
}

static void zoom_out_activated(MdApp* app, GVariant* parameter)
{
    (void)parameter;
    md_app_zoom_out(app);
}

static void zoom_reset_activated(MdApp* app, GVariant* parameter)
{
    (void)parameter;
    md_app_zoom_reset(app);
}

static void about_activated(MdApp* app, GVariant* parameter)
{
    (void)parameter;
    md_app_show_about(app);
}

/* One place to see and edit every shortcut in the app. */
static const MdShortcut SHORTCUTS[] = {
    {"open", {"<Primary>o", NULL}, open_activated, NULL},
    {"search", {"<Primary>f", NULL}, search_activated, NULL},
    {"sidebar", {"F9", NULL}, sidebar_activated, NULL},
    /* Ctrl+= is what most keyboards reach for; Ctrl++ (Shift+1) is the
     * literal "plus" keysym and is registered as well. */
    {"zoom-in", {"<Primary>plus", "<Primary>equal", NULL}, zoom_in_activated, NULL},
    {"zoom-out", {"<Primary>minus", NULL}, zoom_out_activated, NULL},
    {"zoom-reset", {"<Primary>0", NULL}, zoom_reset_activated, NULL},
    {"reload", {"<Primary>r", NULL}, reload_activated, NULL},
    /* No accel: the recent list lives in the menu, and each entry carries its
     * own path as the action argument. */
    {"open-recent", {NULL}, open_recent_activated, G_VARIANT_TYPE_STRING},
    {"about", {NULL}, about_activated, NULL},
    {"quit", {"<Primary>q", NULL}, quit_activated, NULL},
};

typedef struct
{
    MdApp* app;
    MdActivateFunc handler;
} MdActionData;

static void on_activate(GSimpleAction* action, GVariant* parameter, gpointer user_data)
{
    (void)action;
    MdActionData* data = user_data;
    data->handler(data->app, parameter);
}

static void action_data_free(gpointer data, GClosure* closure)
{
    (void)closure;
    g_free(data);
}

void md_shortcuts_register(MdApp* app)
{
    GApplication* gapp = G_APPLICATION(app);

    for (guint i = 0; i < G_N_ELEMENTS(SHORTCUTS); i++)
    {
        const MdShortcut* sc = &SHORTCUTS[i];

        MdActionData* data = g_new0(MdActionData, 1);
        data->app = app;
        data->handler = sc->handler;

        GSimpleAction* action = g_simple_action_new(sc->name, sc->parameter);
        g_signal_connect_data(action, "activate", G_CALLBACK(on_activate), data,
                              (GClosureNotify)action_data_free, 0);
        g_action_map_add_action(G_ACTION_MAP(gapp), G_ACTION(action));

        if (sc->accels[0] != NULL)
        {
            gchar* full = g_strconcat("app.", sc->name, NULL);
            gtk_application_set_accels_for_action(GTK_APPLICATION(gapp), full, sc->accels);
            g_free(full);
        }
    }
}
