#ifndef ZUI_APP_H
#define ZUI_APP_H

#include <zui/document.h>
#include <zui/element_box.h>
#include <zui/event.h>
#include <zui/export.h>
#include <zui/host.h>
#include <zui/host_types.h>
#include <zui/model.h>
#include <zui/plugin.h>
#include <zui/window.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace zui {

namespace frame {
struct Access;
}

/// The application session.
///
/// Structure comes from Document::load (HTML5). Skin and layout come from CSS referenced
/// by that document. C++ binds handler names and writes data / class tokens.
/// Application code includes this header only.
///
/// App owns one Document, one Host, and one Window. It does not inherit them, and those
/// objects are not data members of this class: they live in the session implementation.
/// run, pump_frame, and quit are the session. Page, process, and chrome calls go through
/// document(), host(), and window().
class ZUI_EXPORT App {
    friend struct frame::Access;
    friend class Document;
    friend class Host;
    friend class Window;

public:
    App(int argc, char** argv);
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;
    App(App&&) = delete;
    App& operator=(App&&) = delete;

    Document& document() noexcept;
    const Document& document() const noexcept;
    Host& host() noexcept;
    const Host& host() const noexcept;
    Window& window() noexcept;
    const Window& window() const noexcept;

    /// Opts into a diagnostic log at @p path and a crash report beside it. The default build
    /// installs neither. @return false when @p path is empty or the file cannot be opened.
    bool enable_diagnostics(std::string_view log_path);

    /// Appends one line to the diagnostic log. No-op until `enable_diagnostics`.
    void log_line(std::string_view line);

    /// Runs the main loop until quit().
    /// Desktop: creates backends then `while (running && pump_frame()) {}`.
    /// Mobile hosts that own the system loop call pump_frame() themselves after setup.
    /// @return the process exit code.
    int run();

    /// Pumps one frame (poll → style/layout/paint → present). @return false when the
    /// loop should stop (quit, poll failed, or close not prevented). Requires run()'s
    /// backend setup (or a future injectable session); otherwise returns false.
    bool pump_frame();

    /// Asks the loop started by run() to stop.
    void quit();

private:
    class Impl;
    // Const queries still mutate the platform cache. The reference is mutable on purpose.
    Impl& impl() const noexcept;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace zui

#endif  // ZUI_APP_H
