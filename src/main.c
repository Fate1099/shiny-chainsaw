#include "app.h"

int main(int argc, char* argv[])
{
    MdApp* app = md_app_new();
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}