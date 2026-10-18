#include "ui/view.h"

#include <zui/app.h>

int main(int argc, char** argv) {
    zui::App app(argc, argv);
    zdb::Session session;
    session.app = &app;
    if (!zdb::start(session, argc, argv)) {
        return 1;
    }
    return app.run();
}
