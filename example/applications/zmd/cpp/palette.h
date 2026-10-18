#ifndef ZMD_PALETTE_H
#define ZMD_PALETTE_H

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <zui/app.h>

#include "command.h"
#include "later.h"
#include "palette_model.h"

namespace zmd {

/// The command palette: Ctrl+K lists commands, Ctrl+P lists documents.
/// It knows how to show and filter; what a command *does* is injected, so the
/// palette never grows into a second session.
class Palette {
public:
    enum class Mode { Commands, Files };
    using Runner = std::function<void(std::string_view id)>;

    Palette(zui::Document& page, Runner runner);

    void set_commands(std::vector<Command> commands);
    void set_files(std::vector<Command> files);
    void open(Mode mode);
    void close();
    void toggle(Mode mode);
    void set_query(std::string_view query);
    void pick(std::size_t row);
    void move(int delta);

    bool is_open() const { return open_; }
    Mode mode() const { return mode_; }
    std::size_t active() const { return active_; }

private:
    void render();

    zui::Document& page_;
    /// The palette focuses its field a frame after opening; an owner-guarded defer drops that
    /// focus when the session it belongs to is gone (see `later.h`).
    Later later_;
    Runner runner_;
    PaletteModel model_;
    std::vector<Command> commands_;
    std::vector<Command> files_;
    Mode mode_ = Mode::Commands;
    std::size_t active_ = 0;
    bool open_ = false;
};

}  // namespace zmd

#endif
