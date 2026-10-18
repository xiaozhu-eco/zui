#include <zui/app.h>

#include "session.h"

int main(int argc, char** argv) {
    zui::App app(argc, argv);
    zmd::Session session(app);
    if (!session.start(argc, argv)) {
        return 1;
    }
    return app.run();
}
