#include "cpp/session.h"

#include <zui/app.h>

int main(int argc, char** argv) {
    zui::App app(argc, argv);
    album::Session session;
    session.app = &app;
    session.dir = app.host().user_data_dir();
    if (!album::start(session, argc, argv)) {
        return 1;
    }
    return app.run();
}
