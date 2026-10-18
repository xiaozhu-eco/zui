#ifndef ALBUM_LIGHTBOX_H
#define ALBUM_LIGHTBOX_H

#include "store.h"

#include <zui/app.h>

#include <filesystem>
#include <string>
#include <vector>

namespace album {

/// The open photo, the five zoom steps, and the press-and-drag that pans the stage.
class Lightbox {
public:
    const std::string& photo_id() const { return photo_id_; }
    int zoom() const { return zoom_; }
    /// True when the press in progress moved past the slop. A release that did is a pan, so the
    /// lightbox must not read the click that follows it as "close".
    bool dragged() const { return dragged_; }

    void clear();
    void open(std::string id);
    void set_zoom(int next);

    int index_in(const std::vector<Photo*>& rows) const;
    /// Moves to the neighbour in @p rows. False at either end or when nothing is open.
    bool step(const std::vector<Photo*>& rows, int delta);

    void paint(zui::App& app, const Photo& photo, const std::filesystem::path& dir) const;
    void pointer_down(float x, float y, float scroll_left, float scroll_top);
    void pointer_move(zui::App& app, float x, float y);
    void pointer_up();

private:
    std::string photo_id_;
    int zoom_ = 0;
    bool panning_ = false;
    bool dragged_ = false;
    float origin_x_ = 0.f;
    float origin_y_ = 0.f;
    float origin_left_ = 0.f;
    float origin_top_ = 0.f;
};

}  // namespace album

#endif
