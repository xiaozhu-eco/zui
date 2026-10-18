#ifndef ZUI_SOURCE_APP_TIMER_QUEUE_H
#define ZUI_SOURCE_APP_TIMER_QUEUE_H

// The app's timers: one queue per `App`, armed by `App::after` / `App::every` and drained once per
// frame (KD13: a nested host modal pick suspends the drain).

#include <zui/app.h>

#include <chrono>
#include <functional>
#include <utility>
#include <vector>

namespace zui {
namespace app_detail {

class TimerQueue {
public:
    struct Entry {
        TimerId id = 0;
        std::chrono::steady_clock::time_point due{};
        int interval_ms = 0;
        bool repeating = false;
        void* page = nullptr;
        std::function<void()> fn;
    };

    TimerId arm(std::function<void()> fn, int delay_ms, bool repeating, void* page) {
        if (!fn) {
            return 0;
        }
        if (delay_ms < 0) {
            delay_ms = 0;
        }
        Entry entry;
        entry.id = next_id_++;
        if (next_id_ == 0) {
            next_id_ = 1;
        }
        entry.due = std::chrono::steady_clock::now() + std::chrono::milliseconds(delay_ms);
        entry.interval_ms = delay_ms;
        entry.repeating = repeating;
        entry.page = page;
        entry.fn = std::move(fn);
        const TimerId id = entry.id;
        entries_.push_back(std::move(entry));
        return id;
    }

    /// Fire every entry whose deadline passed, keeping repeating ones armed.
    void drain_due() {
        if (entries_.empty()) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        std::vector<std::function<void()>> batch;
        std::vector<Entry> rest;
        rest.reserve(entries_.size());
        for (auto& t : entries_) {
            if (!t.fn) {
                continue;
            }
            if (t.due > now) {
                rest.push_back(std::move(t));
                continue;
            }
            // Copy the callback so a repeating timer can keep its entry.
            batch.push_back(t.fn);
            if (t.repeating) {
                const int ms = t.interval_ms > 0 ? t.interval_ms : 0;
                t.due = now + std::chrono::milliseconds(ms);
                rest.push_back(std::move(t));
            }
        }
        entries_.swap(rest);
        for (auto& fn : batch) {
            if (fn) {
                fn();
            }
        }
    }

    std::vector<Entry>& entries() { return entries_; }
    const std::vector<Entry>& entries() const { return entries_; }

    /// Drops one timer. The page must be the document that armed it.
    bool cancel(TimerId id, void* page) {
        if (id == 0) {
            return false;
        }
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->id == id && it->page == page) {
                entries_.erase(it);
                return true;
            }
        }
        return false;
    }

    /// Drops every timer armed by one child document.
    void drop_page(void* page) {
        if (page == nullptr) {
            return;
        }
        std::vector<Entry> rest;
        rest.reserve(entries_.size());
        for (auto& entry : entries_) {
            if (entry.page != page) {
                rest.push_back(std::move(entry));
            }
        }
        entries_.swap(rest);
    }

    void clear() { entries_.clear(); }
    bool empty() const { return entries_.empty(); }

private:
    std::vector<Entry> entries_;
    TimerId next_id_ = 1;
};

}  // namespace app_detail
}  // namespace zui

#endif
