#if defined(__cplusplus)
#error this example is compiled as C
#endif

#include "zui_capi.h"

#include <stdio.h>
#include <string.h>

static void on_save(zui_event* event, void* user) {
    zui_app* app = (zui_app*)user;
    if (event == NULL || event->type == NULL || strcmp(event->type, "click") != 0) {
        return;
    }
    zui_app_set(app, "status", "text", "Saved");
    zui_app_add_class(app, "title", "error");
}

int main(int argc, char** argv) {
    const char* page = argc > 1 ? argv[1] : "c_hello.html";
    zui_app* app = zui_app_create(argc, argv);
    if (app == NULL) {
        fprintf(stderr, "zui_app_create failed\n");
        return 1;
    }
    if (!zui_app_load(app, page)) {
        fprintf(stderr, "failed to load %s\n", page);
        zui_app_destroy(app);
        return 1;
    }
    zui_app_bind(app, "on_save", on_save, app);
    zui_app_set(app, "status", "text", "Ready");
    const int code = zui_app_run(app);
    zui_app_destroy(app);
    return code;
}
