#include "session.h"

#include "rows.h"

namespace album {

void Session::render_picks() {
    FieldRows model;
    const Photo* photo = store_.find_photo(light_.photo_id());
    std::string picked;
    for (const Album* album : store_.ordered()) {
        FieldRows::Row row;
        model.put(row, "id", album->id);
        model.put(row, "name", album->name);
        if (photo != nullptr && photo->album_id == album->id) {
            picked = album->name;
        }
        model.add(std::move(row));
    }
    app->document().set_model("light-album", model);
    if (photo == nullptr) {
        return;
    }
    app->document().set("light-album", "value", photo->album_id);
    app->document().set("light-album", "text", picked);
}

void Session::render_light() {
    const std::vector<Photo*> rows = store_.visible(current_);
    const int index = light_.index_in(rows);
    if (light_.photo_id().empty() || index < 0) {
        light_.pointer_up();
        app->document().set("lightbox", "visible", "false");
        app->document().remove_class("stage", "is-zoomed");
        return;
    }
    const Photo* photo = rows[static_cast<std::size_t>(index)];
    app->document().set("lightbox", "visible", "true");
    app->document().set("light-name", "text", photo->name);
    app->document().set("light-pos", "text",
             std::to_string(index + 1) + " / " + std::to_string(rows.size()));
    app->document().set("light-note", "value", photo->note);
    app->document().set("light-prev", "visible", index > 0 ? "true" : "false");
    app->document().set("light-next", "visible", index + 1 < static_cast<int>(rows.size()) ? "true" : "false");
    light_.paint(*app, *photo, dir);
    render_picks();
}

void Session::render() {
    FieldRows albums;
    auto add_album_row = [&](const std::string& id, const std::string& name, int count) {
        FieldRows::Row row;
        albums.put(row, "id", id);
        albums.put(row, "name", name);
        albums.put(row, "count", std::to_string(count));
        albums.put(row, "on", current_ == id ? "is-on" : "");
        albums.add(std::move(row));
    };
    add_album_row("all", "全部", static_cast<int>(store_.photos().size()));
    for (const Album* album : store_.ordered()) {
        add_album_row(album->id, album->name, store_.count_in(album->id));
    }
    app->document().set_model("albums", albums);

    const Album* album = store_.find_album(current_);
    const bool editable = album != nullptr && album->id != "loose";
    app->document().set("album-title", "visible", editable ? "false" : "true");
    app->document().set("album-name", "visible", editable ? "true" : "false");
    app->document().set("delete-album", "visible", editable ? "true" : "false");
    if (current_ == "all") {
        app->document().set("album-title", "text", "全部");
    } else if (!editable && album != nullptr) {
        app->document().set("album-title", "text", album->name);
    }
    if (editable) {
        app->document().set("album-name", "value", album->name);
    }

    const std::vector<Photo*> rows = store_.visible(current_);
    app->document().set("album-count", "text", std::to_string(rows.size()) + " 张");
    app->document().set("empty", "visible", rows.empty() ? "true" : "false");
    FieldRows grid;
    for (const Photo* photo : rows) {
        FieldRows::Row row;
        grid.put(row, "id", photo->id);
        grid.put(row, "name", photo->name);
        std::string src = photo->file;
        if (photo->file.rfind("album-photos/", 0) == 0) {
            src = (dir / photo->file).lexically_normal().string();
        }
        grid.put(row, "src", src);
        grid.add(std::move(row));
    }
    app->document().set_model("grid", grid);
    render_light();
}

void Session::on_album(const zui::Event& event) {
    current_ = std::string(event.data("id"));
    light_.clear();
    render();
}

namespace {

// U+200B is a boundary neutral. Shaping drops it (UAX #9 X9), so the button's
// painted runs read as the name. The node's own text keeps the boundary.
std::string painted_name(const std::string& name) {
    if (name.size() < 2) {
        return name;
    }
    std::size_t split = 1;
    const auto lead = static_cast<unsigned char>(name[0]);
    if ((lead & 0xE0u) == 0xC0u && name.size() >= 2) {
        split = 2;
    } else if ((lead & 0xF0u) == 0xE0u && name.size() >= 3) {
        split = 3;
    } else if ((lead & 0xF8u) == 0xF0u && name.size() >= 4) {
        split = 4;
    }
    if (split >= name.size()) {
        return name;
    }
    std::string label = name.substr(0, split);
    label += "\xE2\x80\x8B";
    label += name.substr(split);
    return label;
}

}  // namespace

void Session::on_album_name(const zui::Event& event) {
    Album* album = store_.find_album(current_);
    const std::string name = trim(event.value());
    if (album == nullptr || album->id == "loose" || name.empty()) {
        return;
    }
    album->name = name;
    persist();
    FieldRows albums;
    auto add = [&](const std::string& id, const std::string& label, int count) {
        FieldRows::Row row;
        albums.put(row, "id", id);
        albums.put(row, "name", label);
        albums.put(row, "count", std::to_string(count));
        albums.put(row, "on", current_ == id ? "is-on" : "");
        albums.add(std::move(row));
    };
    add("all", "全部", static_cast<int>(store_.photos().size()));
    for (const Album* item : store_.ordered()) {
        const std::string label = item->id == current_ ? painted_name(item->name) : item->name;
        add(item->id, label, store_.count_in(item->id));
    }
    app->document().set_model("albums", albums);
}

void Session::assign_photo_album(const std::string& next) {
    Photo* photo = store_.find_photo(light_.photo_id());
    if (photo == nullptr || next.empty() || next == photo->album_id) {
        return;
    }
    photo->album_id = next;
    persist();
    if (current_ != "all" && current_ != photo->album_id) {
        light_.clear();
    }
    render();
}

void Session::on_light_album(const zui::Event& event) { assign_photo_album(std::string(event.value())); }

void Session::on_pick_album(const zui::Event& event) {
    Photo* photo = store_.find_photo(light_.photo_id());
    if (photo == nullptr) {
        return;
    }
    photo->album_id = std::string(event.data("id"));
    persist();
    if (current_ != "all" && current_ != photo->album_id) {
        light_.clear();
    }
    render();
}

}  // namespace album
