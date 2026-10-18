#ifndef ZMD_LATER_H
#define ZMD_LATER_H

#include <functional>
#include <memory>
#include <utility>

#include <zui/app.h>

namespace zmd {

/// `Document::defer` with an owner. The page outlives the part that armed the callback - `zui::App`
/// keeps its tree across sessions, and the tests open a new Session on one App - so a lambda still
/// queued when the part dies would run against freed memory (the outline panel's model, a Find
/// that already went away). `run` wraps the callback with a token and the part's own destruction
/// drops it; the queued lambda then returns without touching anything.
class Later {
public:
    explicit Later(zui::Document& page) : page_(&page) {}

    void run(std::function<void()> fn) {
        std::weak_ptr<int> guard = alive_;
        page_->defer([guard, fn = std::move(fn)] {
            if (guard.expired()) {
                return;
            }
            fn();
        });
    }

private:
    zui::Document* page_;
    std::shared_ptr<int> alive_ = std::make_shared<int>(0);
};

}  // namespace zmd

#endif  // ZMD_LATER_H
