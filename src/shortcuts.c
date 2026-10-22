#include "shortcuts.h"

typedef void (*MdActivateFunc)(MdApp* app);

typedef struct
{
    const gchar* name;      /* action suffix, registered as "app.<name>" */
    const gchar* accels[3]; /* GTK accelerator strings, NULL-terminated */
    MdActivateFunc handler;
} MdShortcut;

static void quit_activated(MdApp* app)
{
    g_application_quit(G_APPLICATION(app));
}

/* One place to see and edit every shortcut in the app. */
static const MdShortcut SHORTCUTS[] = {
    {"open", {"<Primary>o", NULL}, md_app_show_open_dialog},
    {"search", {"<Primary>f", NULL}, md_app_show_search},
    {"sidebar", {"F9", NULL}, md_app_toggle_sidebar},
    /* Ctrl+= is what most keyboards reach for; Ctrl++ (Shift+1) is the
     * literal "plus" keysym and is registered as well. */
    {"zoom-in", {"<Primary>plus", "<Primary>equal", NULL}, md_app_zoom_in},
    {"zoom-out", {"<Primary>minus", NULL}, md_app_zoom_out},
    {"zoom-reset", {"<Primary>0", NULL}, md_app_zoom_reset},
    {"about", {NULL}, md_app_show_about},
    {"quit", {"<Primary>q", NULL}, quit_activated},
};

typedef struct
{
    MdApp* app;
    MdActivateFunc handler;
} MdActionData;

static void on_activate(GSimpleAction* action, GVariant* parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    MdActionData* data = user_data;
    data->handler(data->app);
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

        GSimpleAction* action = g_simple_action_new(sc->name, NULL);
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