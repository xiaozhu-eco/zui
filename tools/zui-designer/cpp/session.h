#ifndef ZUI_DESIGNER_SESSION_H
#define ZUI_DESIGNER_SESSION_H

#include "document.h"

#include <zui/app.h>

#include <filesystem>
#include <string>

namespace designer {

struct Drag {
    enum class Mode { None, Create, Move, Resize, Board };

    Mode mode = Mode::None;
    std::string kind;
    std::string id;
    float start_x = 0;
    float start_y = 0;
    int ox = 0;
    int oy = 0;
    int ow = 0;
    int oh = 0;
    bool moved = false;
    bool undo = false;
};

struct Session {
    zui::App* app = nullptr;
    Document doc;
    Drag drag;
    std::string drop_id;
    std::filesystem::path store;
    bool text_undo = false;
    bool title_undo = false;
};

void bind_actions(Session& session);
void boot(Session& session);

}  // namespace designer

#endif
