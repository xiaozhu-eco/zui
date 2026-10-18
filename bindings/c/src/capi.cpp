#include "zui_capi.h"

#include <zui/app.h>

#include <cstddef>
#include <cstring>
#include <new>
#include <string>
#include <string_view>
#include <functional>
#include <utility>
#include <vector>

struct zui_app {
    explicit zui_app(int argc, char** argv) : app(argc, argv) {}
    zui::App app;
    // Integer ids returned by zui_app_open_window. Slot 0 is unused. A closed id is null.
    std::vector<zui::ChildDocument> child_docs{zui::ChildDocument{}};
};

struct zui_task_signal {
    const zui::TaskSignal* signal = nullptr;
};

struct zui_list_model {
    virtual ~zui_list_model() = default;
    virtual const zui::ListModel& model() const = 0;
};

// C linkage so this matches zui_list_visit_fn. The engine uses the child only
// inside the visit callback.
struct ZuiVisitCtx {
    const std::function<void(const zui::ListModel&)>* fn = nullptr;
};

extern "C" void zui_capi_on_list_child(const zui_list_model* child, void* visit_user) {
    if (child == nullptr || visit_user == nullptr) {
        return;
    }
    const ZuiVisitCtx* ctx = static_cast<const ZuiVisitCtx*>(visit_user);
    if (ctx->fn == nullptr || !*ctx->fn) {
        return;
    }
    (*ctx->fn)(child->model());
}

namespace {

static_assert(static_cast<int>(zui::PickStatus::Ok) == ZUI_PICK_OK, "pick status");
static_assert(static_cast<int>(zui::PickStatus::Cancelled) == ZUI_PICK_CANCELLED, "pick status");
static_assert(static_cast<int>(zui::PickStatus::Unsupported) == ZUI_PICK_UNSUPPORTED, "pick status");
static_assert(static_cast<int>(zui::ClipboardStatus::Ok) == ZUI_CLIPBOARD_OK, "clipboard");
static_assert(static_cast<int>(zui::ClipboardStatus::Unsupported) == ZUI_CLIPBOARD_UNSUPPORTED,
              "clipboard");
static_assert(static_cast<int>(zui::Permission::Camera) == ZUI_PERMISSION_CAMERA, "permission");
static_assert(static_cast<int>(zui::Permission::Microphone) == ZUI_PERMISSION_MICROPHONE,
              "permission");
static_assert(static_cast<int>(zui::Permission::Location) == ZUI_PERMISSION_LOCATION, "permission");
static_assert(static_cast<int>(zui::Permission::Multicast) == ZUI_PERMISSION_MULTICAST, "permission");
static_assert(static_cast<int>(zui::Permission::StorageWrite) == ZUI_PERMISSION_STORAGE_WRITE,
              "permission");
static_assert(static_cast<int>(zui::Permission::ManageStorage) == ZUI_PERMISSION_MANAGE_STORAGE,
              "permission");
static_assert(static_cast<int>(zui::Permission::FullScreenIntent) ==
                  ZUI_PERMISSION_FULL_SCREEN_INTENT,
              "permission");
static_assert(static_cast<int>(zui::PermissionState::Unsupported) == ZUI_PERMISSION_UNSUPPORTED,
              "permission state");
static_assert(static_cast<int>(zui::PermissionState::NotRequested) == ZUI_PERMISSION_NOT_REQUESTED,
              "permission state");
static_assert(static_cast<int>(zui::PermissionState::Granted) == ZUI_PERMISSION_GRANTED,
              "permission state");
static_assert(static_cast<int>(zui::PermissionState::Denied) == ZUI_PERMISSION_DENIED,
              "permission state");
static_assert(static_cast<int>(zui::PermissionState::Blocked) == ZUI_PERMISSION_BLOCKED,
              "permission state");
static_assert(static_cast<int>(zui::ScrollAlign::Start) == ZUI_SCROLL_START, "scroll");
static_assert(static_cast<int>(zui::ScrollAlign::Center) == ZUI_SCROLL_CENTER, "scroll");
static_assert(static_cast<int>(zui::ScrollAlign::End) == ZUI_SCROLL_END, "scroll");
static_assert(static_cast<int>(zui::ScrollAlign::Nearest) == ZUI_SCROLL_NEAREST, "scroll");
static_assert(static_cast<int>(zui::PointerType::Mouse) == ZUI_POINTER_MOUSE, "pointer");
static_assert(static_cast<int>(zui::PointerType::Pen) == ZUI_POINTER_PEN, "pointer");
static_assert(static_cast<int>(zui::PointerType::Touch) == ZUI_POINTER_TOUCH, "pointer");
static_assert(static_cast<int>(zui::DropEffect::None) == ZUI_DROP_EFFECT_NONE, "drag");
static_assert(static_cast<int>(zui::DropEffect::Copy) == ZUI_DROP_EFFECT_COPY, "drag");
static_assert(static_cast<int>(zui::DropEffect::Move) == ZUI_DROP_EFFECT_MOVE, "drag");
static_assert(static_cast<int>(zui::DropEffect::Link) == ZUI_DROP_EFFECT_LINK, "drag");
static_assert(static_cast<int>(zui::EffectAllowed::Uninitialized) == ZUI_EFFECT_ALLOWED_UNINITIALIZED,
              "drag");
static_assert(static_cast<int>(zui::EffectAllowed::All) == ZUI_EFFECT_ALLOWED_ALL, "drag");
static_assert(zui::kGamepadAxisCount == ZUI_GAMEPAD_AXES, "gamepad axes");
static_assert(zui::kGamepadButtonCount == ZUI_GAMEPAD_BUTTONS, "gamepad buttons");

std::string_view chars(const char* text) {
    return text == nullptr ? std::string_view{} : std::string_view{text};
}

size_t write_str(std::string_view text, char* buf, size_t cap) {
    if (buf != nullptr && cap > 0) {
        const size_t n = text.size() < cap ? text.size() : cap - 1;
        if (n > 0) {
            std::memcpy(buf, text.data(), n);
        }
        buf[n] = '\0';
    }
    return text.size();
}

char* dup_str(std::string_view text) {
    char* out = new (std::nothrow) char[text.size() + 1];
    if (out == nullptr) {
        return nullptr;
    }
    if (!text.empty()) {
        std::memcpy(out, text.data(), text.size());
    }
    out[text.size()] = '\0';
    return out;
}

void copy_id(char* dst, size_t cap, const std::string& src) {
    if (dst == nullptr || cap == 0) {
        return;
    }
    const size_t n = src.size() < cap ? src.size() : cap - 1;
    if (n > 0) {
        std::memcpy(dst, src.data(), n);
    }
    dst[n] = '\0';
}

template <class Fn>
std::string read_model_string(Fn fn) {
    constexpr size_t kCap = 8u * 1024u * 1024u;
    char stack[512];
    const size_t n = fn(stack, sizeof(stack));
    if (n < sizeof(stack)) {
        return std::string(stack, n);
    }
    if (n > kCap) {
        return {};
    }
    std::string buf(n + 1, '\0');
    size_t n2 = fn(buf.data(), buf.size());
    if (n2 >= buf.size()) {
        if (n2 > kCap) {
            return {};
        }
        buf.resize(n2 + 1);
        n2 = fn(buf.data(), buf.size());
        if (n2 >= buf.size()) {
            n2 = buf.size() - 1;
        }
    }
    buf.resize(n2);
    return buf;
}

template <class T>
bool covers(const T* obj, size_t end) {
    return obj != nullptr && static_cast<size_t>(obj->struct_size) >= end;
}

bool copy_prefix(void* dst, size_t dst_size, const void* src, uint32_t src_size) {
    if (src == nullptr || src_size < sizeof(uint32_t) || src_size > 4096) {
        return false;
    }
    std::memset(dst, 0, dst_size);
    const size_t n = src_size < dst_size ? static_cast<size_t>(src_size) : dst_size;
    std::memcpy(dst, src, n);
    return true;
}

struct ListAdapter : zui::ListModel {
    zui_list_vtable vt{};
    void* user = nullptr;

    size_t row_count() const override { return vt.row_count != nullptr ? vt.row_count(user) : 0; }

    std::string value(size_t row, std::string_view key) const override {
        if (vt.value == nullptr) {
            return {};
        }
        return read_model_string([&](char* buf, size_t cap) {
            return vt.value(user, row, key.data(), key.size(), buf, cap);
        });
    }

    std::string row_key(size_t row) const override {
        if (vt.row_key == nullptr) {
            return {};
        }
        return read_model_string([&](char* buf, size_t cap) { return vt.row_key(user, row, buf, cap); });
    }

    void with_children(size_t row, const std::function<void(const zui::ListModel&)>& visit) const override {
        if (vt.with_children == nullptr) {
            return;
        }
        ZuiVisitCtx ctx{&visit};
        vt.with_children(user, row, zui_capi_on_list_child, &ctx);
    }
};

struct TableAdapter : zui::TableModel {
    zui_table_vtable vt{};
    void* user = nullptr;

    size_t row_count() const override { return vt.row_count != nullptr ? vt.row_count(user) : 0; }

    size_t column_count() const override {
        return vt.column_count != nullptr ? vt.column_count(user) : 0;
    }

    std::string column_key(size_t column) const override {
        if (vt.column_key == nullptr) {
            return {};
        }
        return read_model_string(
            [&](char* buf, size_t cap) { return vt.column_key(user, column, buf, cap); });
    }

    std::string cell(size_t row, size_t column) const override {
        if (vt.cell == nullptr) {
            return {};
        }
        return read_model_string(
            [&](char* buf, size_t cap) { return vt.cell(user, row, column, buf, cap); });
    }

    std::string cell_value(size_t row, size_t column, std::string_view key) const override {
        if (vt.cell_value == nullptr) {
            return zui::TableModel::cell_value(row, column, key);
        }
        return read_model_string([&](char* buf, size_t cap) {
            return vt.cell_value(user, row, column, key.data(), key.size(), buf, cap);
        });
    }

    std::string row_key(size_t row) const override {
        if (vt.row_key == nullptr) {
            return {};
        }
        return read_model_string([&](char* buf, size_t cap) { return vt.row_key(user, row, buf, cap); });
    }
};

struct ListHolder : zui_list_model {
    ListAdapter adapter;
    const zui::ListModel& model() const override { return adapter; }
};

struct TableHolder : zui_list_model {
    TableAdapter adapter;
    const zui::ListModel& model() const override { return adapter; }
};

struct EventBridge {
    zui::Event* cpp = nullptr;
    std::string type;
    std::string name;
    std::string source;
    std::string value;
    std::string key_text;
    std::string button;
    std::vector<std::string> paths;
    std::vector<const char*> path_ptrs;
    std::string drag_source;
    std::string drag_text;
    std::string drag_uri;
    zui_key key{};
    zui_drop drop{};
    zui_wheel wheel{};
    zui_pointer pointer{};
    zui_drag drag{};
    zui_event event{};
};

void fill_event(EventBridge& bridge, zui::Event& event) {
    bridge.cpp = &event;
    bridge.type.assign(event.type().begin(), event.type().end());
    bridge.name.assign(event.name().begin(), event.name().end());
    bridge.source.assign(event.source().begin(), event.source().end());
    bridge.value.assign(event.value().begin(), event.value().end());
    bridge.event.abi_version = ZUI_CAPI_ABI_VERSION;
    bridge.event.struct_size = static_cast<uint32_t>(sizeof(zui_event));
    bridge.event._impl = &event;
    bridge.event.type = bridge.type.c_str();
    bridge.event.name = bridge.name.c_str();
    bridge.event.source = bridge.source.c_str();
    bridge.event.value = bridge.value.c_str();
    bridge.event.key = nullptr;
    bridge.event.drop = nullptr;
    bridge.event.wheel = nullptr;
    bridge.event.pointer = nullptr;
    bridge.event.drag = nullptr;

    if (const zui::Key* key = event.is_key()) {
        bridge.key_text.assign(key->key.begin(), key->key.end());
        bridge.key.key = bridge.key_text.c_str();
        bridge.key.ctrl = key->ctrl ? 1 : 0;
        bridge.key.shift = key->shift ? 1 : 0;
        bridge.key.alt = key->alt ? 1 : 0;
        bridge.key.meta = key->meta ? 1 : 0;
        bridge.key.composing = key->composing ? 1 : 0;
        bridge.event.key = &bridge.key;
    }
    if (const zui::Drop* drop = event.is_drop()) {
        bridge.paths.reserve(drop->count);
        for (size_t i = 0; i < drop->count; ++i) {
            const std::string_view path = drop->path(i);
            bridge.paths.emplace_back(path.begin(), path.end());
        }
        bridge.path_ptrs.resize(bridge.paths.size());
        for (size_t i = 0; i < bridge.paths.size(); ++i) {
            bridge.path_ptrs[i] = bridge.paths[i].c_str();
        }
        bridge.drop.paths = bridge.path_ptrs.empty() ? nullptr : bridge.path_ptrs.data();
        bridge.drop.count = bridge.path_ptrs.size();
        bridge.event.drop = &bridge.drop;
    }
    if (const zui::Wheel* wheel = event.is_wheel()) {
        bridge.wheel.delta_x = wheel->delta_x;
        bridge.wheel.delta_y = wheel->delta_y;
        bridge.wheel.ctrl = wheel->ctrl ? 1 : 0;
        bridge.wheel.shift = wheel->shift ? 1 : 0;
        bridge.wheel.alt = wheel->alt ? 1 : 0;
        bridge.wheel.meta = wheel->meta ? 1 : 0;
        bridge.event.wheel = &bridge.wheel;
    }
    if (const zui::Pointer* pointer = event.is_pointer()) {
        bridge.button.assign(pointer->button.begin(), pointer->button.end());
        bridge.pointer.x = pointer->x;
        bridge.pointer.y = pointer->y;
        bridge.pointer.button = bridge.button.c_str();
        bridge.pointer.buttons = pointer->buttons;
        bridge.pointer.count = pointer->count;
        bridge.pointer.ctrl = pointer->ctrl ? 1 : 0;
        bridge.pointer.shift = pointer->shift ? 1 : 0;
        bridge.pointer.alt = pointer->alt ? 1 : 0;
        bridge.pointer.meta = pointer->meta ? 1 : 0;
        bridge.pointer.pointer_id = pointer->pointer_id;
        bridge.pointer.pointer_type = static_cast<int>(pointer->pointer_type);
        bridge.pointer.is_primary = pointer->is_primary ? 1 : 0;
        bridge.pointer.pressure = pointer->pressure;
        bridge.event.pointer = &bridge.pointer;
    }
    if (const zui::Drag* drag = event.is_drag()) {
        bridge.drag_source.assign(drag->source_id.begin(), drag->source_id.end());
        bridge.drag_text.assign(drag->text.begin(), drag->text.end());
        bridge.drag_uri.assign(drag->uri.begin(), drag->uri.end());
        bridge.drag.x = drag->x;
        bridge.drag.y = drag->y;
        bridge.drag.source_id = bridge.drag_source.c_str();
        bridge.drag.text = bridge.drag_text.c_str();
        bridge.drag.uri = bridge.drag_uri.c_str();
        bridge.drag.effect = static_cast<int>(drag->effect);
        bridge.drag.allowed = static_cast<int>(drag->allowed);
        bridge.event.drag = &bridge.drag;
    }
}

zui::FileDialogOptions to_dialog(const zui_file_dialog* dialog) {
    zui::FileDialogOptions out;
    if (!covers(dialog, sizeof(uint32_t))) {
        return out;
    }
    if (covers(dialog, offsetof(zui_file_dialog, title) + sizeof(const char*)) && dialog->title != nullptr) {
        out.title = dialog->title;
    }
    if (covers(dialog, offsetof(zui_file_dialog, default_name) + sizeof(const char*)) &&
        dialog->default_name != nullptr) {
        out.default_name = dialog->default_name;
    }
    if (covers(dialog, offsetof(zui_file_dialog, filter_count) + sizeof(size_t)) &&
        dialog->filters != nullptr) {
        for (size_t i = 0; i < dialog->filter_count; ++i) {
            const zui_file_filter& filter = dialog->filters[i];
            zui::FileFilter item;
            if (covers(&filter, offsetof(zui_file_filter, label) + sizeof(const char*)) &&
                filter.label != nullptr) {
                item.label = filter.label;
            }
            if (covers(&filter, offsetof(zui_file_filter, patterns) + sizeof(const char*)) &&
                filter.patterns != nullptr) {
                item.patterns = filter.patterns;
            }
            out.filters.push_back(std::move(item));
        }
    }
    if (covers(dialog, offsetof(zui_file_dialog, multi) + sizeof(int))) {
        out.multi = dialog->multi != 0;
    }
    return out;
}

void fill_paths(const zui::PickPaths& in, zui_pick_paths* out) {
    out->status = static_cast<int>(in.status);
    out->count = 0;
    out->paths = nullptr;
    if (in.paths.empty()) {
        return;
    }
    char** paths = new (std::nothrow) char*[in.paths.size()];
    if (paths == nullptr) {
        out->status = ZUI_PICK_UNSUPPORTED;
        return;
    }
    for (size_t i = 0; i < in.paths.size(); ++i) {
        paths[i] = dup_str(in.paths[i]);
        if (paths[i] == nullptr) {
            for (size_t j = 0; j < i; ++j) {
                delete[] paths[j];
            }
            delete[] paths;
            out->status = ZUI_PICK_UNSUPPORTED;
            return;
        }
    }
    out->count = in.paths.size();
    out->paths = paths;
}

void fill_path(const zui::PickPath& in, zui_pick_path* out) {
    out->status = static_cast<int>(in.status);
    out->path = in.path.empty() ? nullptr : dup_str(in.path);
    if (!in.path.empty() && out->path == nullptr) {
        out->status = ZUI_PICK_UNSUPPORTED;
    }
}

void write_box(const zui::ElementBox& box, zui_element_box* out) {
    const uint32_t size = out->struct_size == 0 ? static_cast<uint32_t>(sizeof(zui_element_box))
                                                 : out->struct_size;
    zui_element_box full{};
    full.struct_size = size;
    full.laid = box.laid ? 1 : 0;
    full.border.x = box.border.x;
    full.border.y = box.border.y;
    full.border.width = box.border.width;
    full.border.height = box.border.height;
    full.content_width = box.content_width;
    full.content_height = box.content_height;
    full.client_width = box.client_width;
    full.client_height = box.client_height;
    full.scroll_width = box.scroll_width;
    full.scroll_height = box.scroll_height;
    const size_t n = size < sizeof(full) ? size : sizeof(full);
    std::memcpy(out, &full, n);
    if (n >= sizeof(uint32_t)) {
        out->struct_size = size;
    }
}

void write_gamepad(const zui::GamepadState& state, zui_gamepad* out) {
    const uint32_t size =
        out->struct_size == 0 ? static_cast<uint32_t>(sizeof(zui_gamepad)) : out->struct_size;
    zui_gamepad full{};
    full.struct_size = size;
    full.connected = state.connected ? 1 : 0;
    copy_id(full.id, sizeof(full.id), state.id);
    for (int i = 0; i < ZUI_GAMEPAD_AXES; ++i) {
        full.axes[i] = state.axes[i];
    }
    for (int i = 0; i < ZUI_GAMEPAD_BUTTONS; ++i) {
        full.buttons[i].pressed = state.buttons[i].pressed ? 1 : 0;
        full.buttons[i].value = state.buttons[i].value;
    }
    const size_t n = size < sizeof(full) ? size : sizeof(full);
    std::memcpy(out, &full, n);
    if (n >= sizeof(uint32_t)) {
        out->struct_size = size;
    }
}

void write_display(const zui::DisplayIdentity& id, zui_display* out) {
    const uint32_t size =
        out->struct_size == 0 ? static_cast<uint32_t>(sizeof(zui_display)) : out->struct_size;
    zui_display full{};
    full.struct_size = size;
    full.known = id.known ? 1 : 0;
    copy_id(full.id, sizeof(full.id), id.id);
    full.index = id.index;
    full.width_px = id.width_px;
    full.height_px = id.height_px;
    full.scale = id.scale;
    const size_t n = size < sizeof(full) ? size : sizeof(full);
    std::memcpy(out, &full, n);
    if (n >= sizeof(uint32_t)) {
        out->struct_size = size;
    }
}

zui::Event* event_impl(const zui_event* event) {
    return event == nullptr ? nullptr : static_cast<zui::Event*>(event->_impl);
}

}  // namespace

extern "C" {

uint32_t zui_capi_abi_version(void) { return ZUI_CAPI_ABI_VERSION; }

void zui_capi_fill_layout(zui_capi_layout* out) {
    if (out == nullptr) {
        return;
    }
    const uint32_t size =
        out->struct_size == 0 ? static_cast<uint32_t>(sizeof(zui_capi_layout)) : out->struct_size;
    zui_capi_layout full{};
    full.struct_size = static_cast<uint32_t>(sizeof(full));
    full.abi_version = ZUI_CAPI_ABI_VERSION;
    full.file_dialog = static_cast<uint32_t>(sizeof(zui_file_dialog));
    full.file_filter = static_cast<uint32_t>(sizeof(zui_file_filter));
    full.element_box = static_cast<uint32_t>(sizeof(zui_element_box));
    full.gamepad = static_cast<uint32_t>(sizeof(zui_gamepad));
    full.display = static_cast<uint32_t>(sizeof(zui_display));
    full.list_vtable = static_cast<uint32_t>(sizeof(zui_list_vtable));
    full.table_vtable = static_cast<uint32_t>(sizeof(zui_table_vtable));
    full.event = static_cast<uint32_t>(sizeof(zui_event));
    full.key = static_cast<uint32_t>(sizeof(zui_key));
    full.pointer = static_cast<uint32_t>(sizeof(zui_pointer));
    full.wheel = static_cast<uint32_t>(sizeof(zui_wheel));
    full.drag = static_cast<uint32_t>(sizeof(zui_drag));
    full.drop = static_cast<uint32_t>(sizeof(zui_drop));
    full.pick_paths = static_cast<uint32_t>(sizeof(zui_pick_paths));
    full.pick_path = static_cast<uint32_t>(sizeof(zui_pick_path));
    full.image = static_cast<uint32_t>(sizeof(zui_image));
    const size_t n = size < sizeof(full) ? size : sizeof(full);
    std::memcpy(out, &full, n);
}

zui_app* zui_app_create(int argc, char** argv) {
    try {
        return new zui_app(argc, argv);
    } catch (...) {
        return nullptr;
    }
}

void zui_app_destroy(zui_app* app) { delete app; }

int zui_app_load(zui_app* app, const char* path) {
    if (app == nullptr) {
        return 0;
    }
    try {
        return app->app.document().load(chars(path)) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

int zui_app_load_fragment(zui_app* app, const char* path) {
    if (app == nullptr) {
        return 0;
    }
    try {
        return app->app.document().load_fragment(chars(path)) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

int zui_app_load_fragment_into(zui_app* app, const char* path, const char* into_id) {
    if (app == nullptr) {
        return 0;
    }
    try {
        return app->app.document().load_fragment(chars(path), chars(into_id)) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

int zui_app_replace_fragment(zui_app* app, const char* path, const char* into_id) {
    if (app == nullptr) {
        return 0;
    }
    try {
        return app->app.document().replace_fragment(chars(path), chars(into_id)) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

int zui_app_unload_fragment(zui_app* app, const char* id) {
    if (app == nullptr) {
        return 0;
    }
    try {
        return app->app.document().unload_fragment(chars(id)) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

int zui_app_set_theme(zui_app* app, const char* name) {
    if (app == nullptr) {
        return 0;
    }
    try {
        return app->app.document().set_theme(chars(name)) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

void zui_app_bind(zui_app* app, const char* name, zui_handler fn, void* user) {
    if (app == nullptr || name == nullptr) {
        return;
    }
    try {
        if (fn == nullptr) {
            app->app.document().unbind(name);
            return;
        }
        app->app.document().bind(name, [fn, user](zui::Event& event) {
            EventBridge bridge;
            fill_event(bridge, event);
            fn(&bridge.event, user);
        });
    } catch (...) {
    }
}

void zui_app_unbind(zui_app* app, const char* name) {
    if (app == nullptr || name == nullptr) {
        return;
    }
    try {
        app->app.document().unbind(name);
    } catch (...) {
    }
}

int zui_app_set(zui_app* app, const char* id, const char* field, const char* value) {
    if (app == nullptr) {
        return 0;
    }
    try {
        return app->app.document().set(chars(id), chars(field), chars(value)) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

size_t zui_app_get(zui_app* app, const char* id, const char* field, char* buf, size_t cap) {
    if (app == nullptr) {
        return write_str({}, buf, cap);
    }
    try {
        return write_str(app->app.document().get(chars(id), chars(field)), buf, cap);
    } catch (...) {
        return write_str({}, buf, cap);
    }
}

zui_list_model* zui_list_model_create(const zui_list_vtable* vtable, void* user) {
    if (vtable == nullptr) {
        return nullptr;
    }
    auto* holder = new (std::nothrow) ListHolder();
    if (holder == nullptr) {
        return nullptr;
    }
    if (!copy_prefix(&holder->adapter.vt, sizeof(holder->adapter.vt), vtable, vtable->struct_size) ||
        holder->adapter.vt.row_count == nullptr || holder->adapter.vt.value == nullptr) {
        delete holder;
        return nullptr;
    }
    holder->adapter.user = user;
    return holder;
}

zui_list_model* zui_table_model_create(const zui_table_vtable* vtable, void* user) {
    if (vtable == nullptr) {
        return nullptr;
    }
    auto* holder = new (std::nothrow) TableHolder();
    if (holder == nullptr) {
        return nullptr;
    }
    if (!copy_prefix(&holder->adapter.vt, sizeof(holder->adapter.vt), vtable, vtable->struct_size) ||
        holder->adapter.vt.row_count == nullptr || holder->adapter.vt.column_count == nullptr ||
        holder->adapter.vt.column_key == nullptr || holder->adapter.vt.cell == nullptr) {
        delete holder;
        return nullptr;
    }
    holder->adapter.user = user;
    return holder;
}

void zui_list_model_destroy(zui_list_model* model) { delete model; }

int zui_app_set_model(zui_app* app, const char* id, const zui_list_model* model) {
    if (app == nullptr || model == nullptr) {
        return 0;
    }
    try {
        return app->app.document().set_model(chars(id), model->model()) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

uint64_t zui_app_watch(zui_app* app, const char* id, zui_watch_fn fn, void* user) {
    if (app == nullptr || fn == nullptr) {
        return 0;
    }
    try {
        return app->app.document().watch(chars(id), [fn, user](std::string_view watched) {
            const std::string text(watched.begin(), watched.end());
            fn(text.c_str(), user);
        });
    } catch (...) {
        return 0;
    }
}

void zui_app_unwatch(zui_app* app, uint64_t watch) {
    if (app == nullptr) {
        return;
    }
    try {
        app->app.document().unwatch(watch);
    } catch (...) {
    }
}

int zui_app_measure(zui_app* app, const char* id, zui_element_box* out) {
    if (app == nullptr || out == nullptr) {
        return 0;
    }
    try {
        const std::optional<zui::ElementBox> box = app->app.document().measure(chars(id));
        if (!box) {
            return 0;
        }
        write_box(*box, out);
        return 1;
    } catch (...) {
        return 0;
    }
}

int zui_app_add_class(zui_app* app, const char* id, const char* class_name) {
    if (app == nullptr) {
        return 0;
    }
    try {
        return app->app.document().add_class(chars(id), chars(class_name)) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

int zui_app_remove_class(zui_app* app, const char* id, const char* class_name) {
    if (app == nullptr) {
        return 0;
    }
    try {
        return app->app.document().remove_class(chars(id), chars(class_name)) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

void zui_app_defer(zui_app* app, zui_fn fn, void* user) {
    if (app == nullptr || fn == nullptr) {
        return;
    }
    try {
        app->app.document().defer([fn, user]() { fn(user); });
    } catch (...) {
    }
}

uint64_t zui_app_set_timeout(zui_app* app, zui_fn fn, void* user, int delay_ms) {
    if (app == nullptr || fn == nullptr) {
        return 0;
    }
    try {
        return app->app.document().set_timeout([fn, user]() { fn(user); }, delay_ms);
    } catch (...) {
        return 0;
    }
}

uint64_t zui_app_set_interval(zui_app* app, zui_fn fn, void* user, int interval_ms) {
    if (app == nullptr || fn == nullptr) {
        return 0;
    }
    try {
        return app->app.document().set_interval([fn, user]() { fn(user); }, interval_ms);
    } catch (...) {
        return 0;
    }
}

void zui_app_clear_timer(zui_app* app, uint64_t timer) {
    if (app == nullptr) {
        return;
    }
    try {
        app->app.document().clear_timer(timer);
    } catch (...) {
    }
}

int zui_app_run(zui_app* app) {
    if (app == nullptr) {
        return 1;
    }
    try {
        return app->app.run();
    } catch (...) {
        return 1;
    }
}

int zui_app_pump_frame(zui_app* app) {
    if (app == nullptr) {
        return 0;
    }
    try {
        return app->app.pump_frame() ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

void zui_app_quit(zui_app* app) {
    if (app == nullptr) {
        return;
    }
    try {
        app->app.quit();
    } catch (...) {
    }
}

void zui_app_set_app_id(zui_app* app, const char* id) {
    if (app == nullptr) {
        return;
    }
    try {
        app->app.host().set_app_id(chars(id));
    } catch (...) {
    }
}

size_t zui_app_user_data_dir(zui_app* app, char* buf, size_t cap) {
    if (app == nullptr) {
        return write_str({}, buf, cap);
    }
    try {
        return write_str(app->app.host().user_data_dir(), buf, cap);
    } catch (...) {
        return write_str({}, buf, cap);
    }
}

size_t zui_app_cache_dir(zui_app* app, char* buf, size_t cap) {
    if (app == nullptr) {
        return write_str({}, buf, cap);
    }
    try {
        return write_str(app->app.host().cache_dir(), buf, cap);
    } catch (...) {
        return write_str({}, buf, cap);
    }
}

void zui_app_pick_open(zui_app* app, const zui_file_dialog* dialog, zui_pick_paths* out) {
    if (out == nullptr) {
        return;
    }
    out->status = ZUI_PICK_UNSUPPORTED;
    out->count = 0;
    out->paths = nullptr;
    if (app == nullptr) {
        return;
    }
    try {
        fill_paths(app->app.host().pick_open(to_dialog(dialog)), out);
    } catch (...) {
        out->status = ZUI_PICK_UNSUPPORTED;
    }
}

void zui_app_pick_save(zui_app* app, const zui_file_dialog* dialog, zui_pick_path* out) {
    if (out == nullptr) {
        return;
    }
    out->status = ZUI_PICK_UNSUPPORTED;
    out->path = nullptr;
    if (app == nullptr) {
        return;
    }
    try {
        fill_path(app->app.host().pick_save(to_dialog(dialog)), out);
    } catch (...) {
        out->status = ZUI_PICK_UNSUPPORTED;
    }
}

void zui_app_pick_folder(zui_app* app, const zui_file_dialog* dialog, zui_pick_path* out) {
    if (out == nullptr) {
        return;
    }
    out->status = ZUI_PICK_UNSUPPORTED;
    out->path = nullptr;
    if (app == nullptr) {
        return;
    }
    try {
        fill_path(app->app.host().pick_folder(to_dialog(dialog)), out);
    } catch (...) {
        out->status = ZUI_PICK_UNSUPPORTED;
    }
}

void zui_app_pick_open_async(zui_app* app, const zui_file_dialog* dialog, zui_pick_paths_fn fn,
                             void* user) {
    if (app == nullptr || fn == nullptr) {
        return;
    }
    try {
        app->app.host().pick_open_async(to_dialog(dialog), [fn, user](zui::PickPaths result) {
            zui_pick_paths view{};
            fill_paths(result, &view);
            fn(&view, user);
            zui_pick_paths_free(&view);
        });
    } catch (...) {
    }
}

void zui_app_pick_save_async(zui_app* app, const zui_file_dialog* dialog, zui_pick_path_fn fn,
                             void* user) {
    if (app == nullptr || fn == nullptr) {
        return;
    }
    try {
        app->app.host().pick_save_async(to_dialog(dialog), [fn, user](zui::PickPath result) {
            zui_pick_path view{};
            fill_path(result, &view);
            fn(&view, user);
            zui_pick_path_free(&view);
        });
    } catch (...) {
    }
}

void zui_app_pick_folder_async(zui_app* app, const zui_file_dialog* dialog, zui_pick_path_fn fn,
                               void* user) {
    if (app == nullptr || fn == nullptr) {
        return;
    }
    try {
        app->app.host().pick_folder_async(to_dialog(dialog), [fn, user](zui::PickPath result) {
            zui_pick_path view{};
            fill_path(result, &view);
            fn(&view, user);
            zui_pick_path_free(&view);
        });
    } catch (...) {
    }
}

namespace {

bool to_permission(zui_permission value, zui::Permission& out) {
    if (value < ZUI_PERMISSION_CAMERA || value > ZUI_PERMISSION_FULL_SCREEN_INTENT) {
        return false;
    }
    out = static_cast<zui::Permission>(value);
    return true;
}

}  // namespace

int zui_app_permission_state(zui_app* app, zui_permission permission) {
    if (app == nullptr) {
        return ZUI_PERMISSION_UNSUPPORTED;
    }
    zui::Permission named{};
    if (!to_permission(permission, named)) {
        return ZUI_PERMISSION_UNSUPPORTED;
    }
    try {
        return static_cast<int>(app->app.host().permission_state(named));
    } catch (...) {
        return ZUI_PERMISSION_UNSUPPORTED;
    }
}

void zui_app_request_permission(zui_app* app, zui_permission permission, zui_permission_fn fn,
                                void* user) {
    if (app == nullptr || fn == nullptr) {
        return;
    }
    zui::Permission named{};
    if (!to_permission(permission, named)) {
        fn(ZUI_PERMISSION_UNSUPPORTED, user);
        return;
    }
    try {
        app->app.host().request_permission(named, [fn, user](zui::PermissionState state) {
            fn(static_cast<zui_permission_state>(state), user);
        });
    } catch (...) {
        fn(ZUI_PERMISSION_UNSUPPORTED, user);
    }
}

void zui_pick_paths_free(zui_pick_paths* paths) {
    if (paths == nullptr) {
        return;
    }
    if (paths->paths != nullptr) {
        for (size_t i = 0; i < paths->count; ++i) {
            delete[] paths->paths[i];
        }
        delete[] paths->paths;
    }
    paths->paths = nullptr;
    paths->count = 0;
}

void zui_pick_path_free(zui_pick_path* path) {
    if (path == nullptr) {
        return;
    }
    delete[] path->path;
    path->path = nullptr;
}

size_t zui_app_clipboard_text(zui_app* app, int* status, char* buf, size_t cap) {
    if (status != nullptr) {
        *status = ZUI_CLIPBOARD_UNSUPPORTED;
    }
    if (app == nullptr) {
        return write_str({}, buf, cap);
    }
    try {
        const zui::ClipboardText text = app->app.host().clipboard_text();
        if (status != nullptr) {
            *status = static_cast<int>(text.status);
        }
        if (text.status != zui::ClipboardStatus::Ok) {
            return write_str({}, buf, cap);
        }
        return write_str(text.text, buf, cap);
    } catch (...) {
        return write_str({}, buf, cap);
    }
}

int zui_app_set_clipboard_text(zui_app* app, const char* text) {
    if (app == nullptr) {
        return 0;
    }
    try {
        return app->app.host().set_clipboard_text(chars(text)) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

void zui_app_clipboard_image(zui_app* app, zui_image* out) {
    if (out == nullptr) {
        return;
    }
    out->status = ZUI_CLIPBOARD_UNSUPPORTED;
    out->width = 0;
    out->height = 0;
    out->rgba = nullptr;
    out->bytes = 0;
    if (app == nullptr) {
        return;
    }
    try {
        const zui::ClipboardImage image = app->app.host().clipboard_image();
        out->status = static_cast<int>(image.status);
        out->width = image.width;
        out->height = image.height;
        if (image.rgba.empty()) {
            return;
        }
        uint8_t* rgba = new (std::nothrow) uint8_t[image.rgba.size()];
        if (rgba == nullptr) {
            out->status = ZUI_CLIPBOARD_UNSUPPORTED;
            return;
        }
        std::memcpy(rgba, image.rgba.data(), image.rgba.size());
        out->rgba = rgba;
        out->bytes = image.rgba.size();
    } catch (...) {
        out->status = ZUI_CLIPBOARD_UNSUPPORTED;
    }
}

int zui_app_set_clipboard_image(zui_app* app, int width, int height, const uint8_t* rgba,
                                size_t bytes) {
    if (app == nullptr) {
        return 0;
    }
    try {
        zui::ClipboardImage image;
        image.width = width;
        image.height = height;
        if (rgba != nullptr && bytes > 0) {
            image.rgba.assign(rgba, rgba + bytes);
        }
        return app->app.host().set_clipboard_image(image) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

void zui_image_free(zui_image* image) {
    if (image == nullptr) {
        return;
    }
    delete[] image->rgba;
    image->rgba = nullptr;
    image->bytes = 0;
}

int zui_app_open_uri(zui_app* app, const char* uri) {
    if (app == nullptr) {
        return 0;
    }
    try {
        return app->app.host().open_uri(chars(uri)) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

int zui_app_scroll_into_view(zui_app* app, const char* id, int align) {
    if (app == nullptr) {
        return 0;
    }
    zui::ScrollAlign block = zui::ScrollAlign::Nearest;
    if (align >= ZUI_SCROLL_START && align <= ZUI_SCROLL_NEAREST) {
        block = static_cast<zui::ScrollAlign>(align);
    }
    try {
        return app->app.document().scroll_into_view(chars(id), block) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

int zui_app_focus(zui_app* app, const char* id) {
    if (app == nullptr) {
        return 0;
    }
    try {
        return app->app.document().focus(chars(id)) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

int zui_app_blur(zui_app* app) {
    if (app == nullptr) {
        return 0;
    }
    try {
        return app->app.document().blur() ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

void zui_app_gamepad(zui_app* app, size_t index, zui_gamepad* out) {
    if (out == nullptr) {
        return;
    }
    zui::GamepadState state;
    if (app != nullptr) {
        try {
            state = app->app.host().gamepad(index);
        } catch (...) {
            state = {};
        }
    }
    write_gamepad(state, out);
}

size_t zui_app_gamepad_count(zui_app* app) {
    if (app == nullptr) {
        return 0;
    }
    try {
        return app->app.host().gamepad_count();
    } catch (...) {
        return 0;
    }
}

uint64_t zui_app_start_task(zui_app* app, zui_task_work work, zui_task_done done, void* user,
                            const char* progress_id) {
    if (app == nullptr || work == nullptr) {
        return 0;
    }
    try {
        zui::MainThreadWork on_done;
        if (done != nullptr) {
            on_done = zui::MainThreadWork([done, user]() { done(user); });
        }
        return app->app.host().start_task(
            [work, user](const zui::TaskSignal& signal) {
                zui_task_signal view{&signal};
                work(&view, user);
            },
            std::move(on_done), chars(progress_id));
    } catch (...) {
        return 0;
    }
}

int zui_app_cancel_task(zui_app* app, uint64_t task) {
    if (app == nullptr) {
        return 0;
    }
    try {
        return app->app.host().cancel_task(task) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

int zui_task_cancelled(const zui_task_signal* signal) {
    if (signal == nullptr || signal->signal == nullptr) {
        return 0;
    }
    return signal->signal->cancelled() ? 1 : 0;
}

void zui_task_set_progress(zui_task_signal* signal, float fraction) {
    if (signal == nullptr || signal->signal == nullptr) {
        return;
    }
    signal->signal->set_progress(fraction);
}

int zui_app_set_title(zui_app* app, const char* title) {
    if (app == nullptr) {
        return 0;
    }
    try {
        return app->app.window().set_title(chars(title)) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

int zui_app_set_window_size(zui_app* app, int width, int height) {
    if (app == nullptr) {
        return 0;
    }
    try {
        return app->app.window().set_window_size(width, height) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

int zui_app_set_icon(zui_app* app, int width, int height, const uint8_t* rgba, size_t bytes) {
    if (app == nullptr) {
        return 0;
    }
    try {
        zui::WindowIcon icon;
        icon.width = width;
        icon.height = height;
        if (rgba != nullptr && bytes > 0) {
            icon.rgba.assign(rgba, rgba + bytes);
        }
        return app->app.window().set_icon(icon) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

int zui_app_minimize(zui_app* app) {
    if (app == nullptr) {
        return 0;
    }
    try {
        return app->app.window().minimize() ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

int zui_app_set_maximized(zui_app* app, int on) {
    if (app == nullptr) {
        return 0;
    }
    try {
        return app->app.window().set_maximized(on != 0) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

int zui_app_set_fullscreen(zui_app* app, int on) {
    if (app == nullptr) {
        return 0;
    }
    try {
        return app->app.window().set_fullscreen(on != 0) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

int zui_app_set_topmost(zui_app* app, int on) {
    if (app == nullptr) {
        return 0;
    }
    try {
        return app->app.window().set_topmost(on != 0) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

int zui_app_minimized(zui_app* app) {
    if (app == nullptr) {
        return 0;
    }
    try {
        return app->app.window().minimized() ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

int zui_app_maximized(zui_app* app) {
    if (app == nullptr) {
        return 0;
    }
    try {
        return app->app.window().maximized() ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

int zui_app_window_visible(zui_app* app) {
    if (app == nullptr) {
        return 0;
    }
    try {
        return app->app.window().visible() ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

int zui_app_fullscreen(zui_app* app) {
    if (app == nullptr) {
        return 0;
    }
    try {
        return app->app.window().fullscreen() ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

int zui_app_topmost(zui_app* app) {
    if (app == nullptr) {
        return 0;
    }
    try {
        return app->app.window().topmost() ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

void zui_app_display(zui_app* app, zui_display* out) {
    if (out == nullptr) {
        return;
    }
    zui::DisplayIdentity id;
    if (app != nullptr) {
        try {
            id = app->app.window().display();
        } catch (...) {
            id = {};
        }
    }
    write_display(id, out);
}

int zui_app_open_window(zui_app* app, const char* html_path, int modal) {
    if (app == nullptr) {
        return 0;
    }
    try {
        const zui::ChildDocument doc = app->app.window().open_document(chars(html_path), modal != 0);
        if (!doc) {
            return 0;
        }
        app->child_docs.push_back(doc);
        return static_cast<int>(app->child_docs.size()) - 1;
    } catch (...) {
        return 0;
    }
}

int zui_app_close_window(zui_app* app, int window) {
    if (app == nullptr || window <= 0 || static_cast<size_t>(window) >= app->child_docs.size()) {
        return 0;
    }
    const zui::ChildDocument doc = app->child_docs[static_cast<size_t>(window)];
    if (!doc) {
        return 0;
    }
    try {
        if (!app->app.window().close_document(doc)) {
            return 0;
        }
        app->child_docs[static_cast<size_t>(window)] = {};
        return 1;
    } catch (...) {
        return 0;
    }
}

int zui_app_enable_diagnostics(zui_app* app, const char* log_path) {
    if (app == nullptr) {
        return 0;
    }
    try {
        return app->app.enable_diagnostics(chars(log_path)) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

void zui_app_log_line(zui_app* app, const char* line) {
    if (app == nullptr) {
        return;
    }
    try {
        app->app.log_line(chars(line));
    } catch (...) {
    }
}

void zui_event_prevent_default(zui_event* event) {
    if (zui::Event* impl = event_impl(event)) {
        impl->prevent_default();
    }
}

int zui_event_default_prevented(const zui_event* event) {
    const zui::Event* impl = event_impl(event);
    return impl != nullptr && impl->default_prevented() ? 1 : 0;
}

size_t zui_event_data(const zui_event* event, const char* name, char* buf, size_t cap) {
    const zui::Event* impl = event_impl(event);
    if (impl == nullptr) {
        return write_str({}, buf, cap);
    }
    const std::string_view value = impl->data(chars(name));
    return write_str(std::string(value.begin(), value.end()), buf, cap);
}

size_t zui_event_drag_data(const zui_event* event, const char* name, char* buf, size_t cap) {
    const zui::Event* impl = event_impl(event);
    if (impl == nullptr) {
        return write_str({}, buf, cap);
    }
    const std::string_view value = impl->drag_data(chars(name));
    return write_str(std::string(value.begin(), value.end()), buf, cap);
}

}  // extern "C"
