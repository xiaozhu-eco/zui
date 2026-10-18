#ifndef SHARE_GESTURE_H
#define SHARE_GESTURE_H

#include <zui/event.h>

#include <cmath>

namespace share {

enum class GestureAction { None, Refresh, Back, Reject };

struct GestureConfig {
    float min_distance_px = 64.f;
    float axis_ratio = 1.5f;  // dominant axis must exceed the other by this factor
};

/// Touch-only swipe recognizer for share: pull-down refresh (files), swipe-right back,
/// pull-down reject (incoming). Mouse and Pen never classify as an action.
struct GestureTracker {
    bool tracking = false;
    zui::PointerType type = zui::PointerType::Mouse;
    float x0 = 0;
    float y0 = 0;
    bool on_incoming = false;  // true when pointer started on incoming sheet/scrim
    bool on_files = false;     // true when started on browse file list

    void reset() {
        tracking = false;
        type = zui::PointerType::Mouse;
        x0 = 0;
        y0 = 0;
        on_incoming = false;
        on_files = false;
    }

    void on_down(const zui::Event& e, bool incoming, bool files) {
        const zui::Pointer* p = e.is_pointer();
        if (p == nullptr) {
            reset();
            return;
        }
        tracking = true;
        type = p->pointer_type;
        x0 = p->x;
        y0 = p->y;
        on_incoming = incoming;
        on_files = files;
    }

    GestureAction on_up(const zui::Event& e, const GestureConfig& cfg = {}) {
        if (!tracking) {
            return GestureAction::None;
        }
        const zui::Pointer* p = e.is_pointer();
        const float x1 = p != nullptr ? p->x : x0;
        const float y1 = p != nullptr ? p->y : y0;
        const zui::PointerType up_type = p != nullptr ? p->pointer_type : type;
        const GestureAction action = classify(up_type, x1 - x0, y1 - y0, on_incoming, on_files, cfg);
        reset();
        return action;
    }

    /// Test seam: start a gesture without constructing a zui::Event.
    void down_for_test(zui::PointerType pointer_type, float x, float y, bool incoming, bool files) {
        tracking = true;
        type = pointer_type;
        x0 = x;
        y0 = y;
        on_incoming = incoming;
        on_files = files;
    }

    /// Test seam: finish a gesture at (x, y) and classify.
    GestureAction up_for_test(float x, float y, const GestureConfig& cfg = {}) {
        if (!tracking) {
            return GestureAction::None;
        }
        const GestureAction action = classify(type, x - x0, y - y0, on_incoming, on_files, cfg);
        reset();
        return action;
    }

    static GestureAction classify(zui::PointerType pointer_type, float dx, float dy, bool incoming,
                                  bool files, const GestureConfig& cfg = {}) {
        if (pointer_type != zui::PointerType::Touch) {
            return GestureAction::None;
        }
        const float adx = std::fabs(dx);
        const float ady = std::fabs(dy);
        const float min_d = cfg.min_distance_px;
        const float ratio = cfg.axis_ratio;

        // Incoming sheet pull-down -> reject (user-approved; not swipe-left).
        if (incoming && dy > min_d && ady > ratio * adx) {
            return GestureAction::Reject;
        }
        // Files list pull-down -> refresh.
        if (files && dy > min_d && ady > ratio * adx) {
            return GestureAction::Refresh;
        }
        // Swipe right -> back.
        if (dx > min_d && adx > ratio * ady) {
            return GestureAction::Back;
        }
        return GestureAction::None;
    }
};

}  // namespace share

#endif  // SHARE_GESTURE_H
