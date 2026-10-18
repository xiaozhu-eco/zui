#ifndef ZUI_WINDOW_H
#define ZUI_WINDOW_H

#include <zui/export.h>
#include <zui/host_types.h>

#include <memory>
#include <string_view>

namespace zui {

class Document;

/// Non-owning handle to a child document. Destroying the handle does not destroy
/// the document. `close_document` does, after `beforeunload`.
class ChildDocument {
public:
    ChildDocument() = default;

    explicit operator bool() const noexcept { return doc_ != nullptr; }
    Document* get() const noexcept { return doc_; }
    Document* operator->() const noexcept { return doc_; }
    Document& operator*() const { return *doc_; }

private:
    friend class Window;
    explicit ChildDocument(Document* doc) noexcept : doc_(doc) {}
    Document* doc_ = nullptr;
};

/// The one operating-system window for an App.
/// set_window_size stays the only geometry setter. A second page is another Document
/// on this same window, not another Window.
class ZUI_EXPORT Window {
    friend class App;

public:
    /// Sets the OS window title and updates the loaded document's title. Applied at window create in
    /// `run()` when called before run; updates a live window after. @return false when empty.
    bool set_title(std::string_view title);

    /// Sets the client/logical CSS-px window size (KD9: host viewport, not widget geometry).
    /// Overrides `<meta name="zui-window">` when called before `run()`. After run, resizes the
    /// live window. @return false when @p w or @p h is not positive.
    /// This is the only window-geometry setter.
    bool set_window_size(int w, int h);

    /// Runtime title-bar / taskbar icon (RGBA8888). Not the executable icon.
    /// A valid icon is kept until a window exists, then handed to that window.
    /// @return false when the buffer does not match the size, or the live window rejects it.
    bool set_icon(const WindowIcon& icon);

    /// Minimizes the current window. @return false when no window is running.
    bool minimize();

    /// Maximizes the current window, or restores it when @p on is false.
    /// @return false when no window is running.
    bool set_maximized(bool on);

    /// Enters or leaves fullscreen. @return false when no window is running.
    bool set_fullscreen(bool on);

    /// Pins the window above others, or clears that. @return false when no window is running.
    bool set_topmost(bool on);

    /// Current chrome flags. False when no window is running.
    bool minimized() const;
    bool maximized() const;
    bool fullscreen() const;
    bool topmost() const;

    /// Whether the host reports the window as visible right now. A `visibilitychange` says that
    /// this answer moved; it does not say which way. Desktop hosts fold "another window took the
    /// focus", occlusion, and minimize into hidden (source/backends/window.h).
    /// False when no window is running.
    bool visible() const;

    /// Display the current window is on. `known` is false without a window.
    DisplayIdentity display() const;

    /// Opens another document on this window. @p modal routes pointer and key input to that
    /// document until it closes. This is not a geometry setter: `set_window_size` stays the only
    /// one. The window owns the document. The returned handle does not.
    /// An empty handle means @p html_path did not load.
    ChildDocument open_document(std::string_view html_path, bool modal);

    /// Closes a child opened by `open_document`. Fires `beforeunload` on that document; a handler
    /// that calls `prevent_default()` keeps it open. The parent keeps running. Does not call
    /// `quit()` unless that handler does. A successful close destroys the document.
    /// @return false when @p doc is not an open child or the close was cancelled.
    bool close_document(ChildDocument doc);

    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) = delete;
    Window& operator=(Window&&) = delete;

private:
    class Impl;
    explicit Window(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace zui

#endif  // ZUI_WINDOW_H
