#include "lightbox.h"

namespace album {
namespace {

const char* kZoom[] = {"z1", "z2", "z3", "z4", "z5"};

/// Pointer travel below this is a click that wobbled, not a pan.
constexpr float kDragSlop = 4.f;

std::string photo_src(const Photo& photo, const std::filesystem::path& dir) {
    if (photo.file.rfind("album-photos/", 0) == 0) {
        return (dir / photo.file).lexically_normal().string();
    }
    return photo.file;
}

int zoom_percent(int zoom) {
    if (zoom <= 0) {
        return 100;
    }
    if (zoom == 1) {
        return 150;
    }
    if (zoom == 2) {
        return 200;
    }
    if (zoom == 3) {
        return 300;
    }
    return 400;
}

}  // namespace

void Lightbox::clear() {
    photo_id_.clear();
    zoom_ = 0;
    panning_ = false;
    dragged_ = false;
}

void Lightbox::open(std::string id) {
    photo_id_ = std::move(id);
    zoom_ = 0;
    panning_ = false;
    dragged_ = false;
}

void Lightbox::set_zoom(int next) {
    if (next < 0) {
        next = 0;
    }
    if (next > 4) {
        next = 4;
    }
    zoom_ = next;
    if (zoom_ <= 0) {
        panning_ = false;
        dragged_ = false;
    }
}

int Lightbox::index_in(const std::vector<Photo*>& rows) const {
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        if (rows[static_cast<std::size_t>(i)]->id == photo_id_) {
            return i;
        }
    }
    return -1;
}

bool Lightbox::step(const std::vector<Photo*>& rows, int delta) {
    const int index = index_in(rows);
    const int next = index + delta;
    if (index < 0 || next < 0 || next >= static_cast<int>(rows.size())) {
        return false;
    }
    open(rows[static_cast<std::size_t>(next)]->id);
    return true;
}

void Lightbox::paint(zui::App& app, const Photo& photo, const std::filesystem::path& dir) const {
    for (const char* zoom : kZoom) {
        app.document().remove_class("light-img", zoom);
    }
    app.document().add_class("light-img", kZoom[zoom_]);
    app.document().set("light-img", "src", photo_src(photo, dir));
    if (zoom_ > 0) {
        app.document().add_class("stage", "is-zoomed");
    } else {
        app.document().remove_class("stage", "is-zoomed");
    }
    app.document().set("zoom-label", "text", std::to_string(zoom_percent(zoom_)) + "%");
}

void Lightbox::pointer_down(float x, float y, float scroll_left, float scroll_top) {
    panning_ = true;
    dragged_ = false;
    origin_x_ = x;
    origin_y_ = y;
    origin_left_ = scroll_left;
    origin_top_ = scroll_top;
}

void Lightbox::pointer_move(zui::App& app, float x, float y) {
    if (!panning_ || zoom_ <= 0) {
        return;
    }
    const float dx = x - origin_x_;
    const float dy = y - origin_y_;
    if (dx > kDragSlop || dx < -kDragSlop || dy > kDragSlop || dy < -kDragSlop) {
        dragged_ = true;
    }
    app.document().set("stage", "scroll_left", std::to_string(origin_left_ - dx));
    app.document().set("stage", "scroll_top", std::to_string(origin_top_ - dy));
}

void Lightbox::pointer_up() { panning_ = false; }

}  // namespace album
