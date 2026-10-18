#ifndef ZUI_EXT_SLOTS_H
#define ZUI_EXT_SLOTS_H

// Process registry. The service types live in include/ext. This header does not
// declare a second set of abstracts and does not include a backend window header.
// Registration methods are inline so a plugin can fill the object it was given
// without linking zui.

#include <ext/clipboard.h>
#include <ext/file_association.h>
#include <ext/file_dialog.h>
#include <ext/gamepad.h>
#include <ext/hotkey.h>
#include <ext/instance.h>
#include <ext/media_key.h>
#include <ext/notify.h>
#include <ext/opener.h>
#include <ext/shell.h>
#include <ext/tasks.h>
#include <ext/tray.h>
#include <ext/update.h>
#include <ext/view.h>

#include <zui/export.h>

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace zui {

/// Out-of-framework example. No built-in local storage ships with the core.
class KeyValueStore {
public:
    virtual ~KeyValueStore() = default;
    virtual void set_item(std::string_view key, std::string_view value) = 0;
    virtual std::string get_item(std::string_view key) const = 0;
    virtual void remove_item(std::string_view key) = 0;
};

class ExtensionRegistry {
public:
    ZUI_EXPORT static ExtensionRegistry& instance();

    void add_dialogs(PlatformKind kind, std::unique_ptr<FileDialogService> service) {
        keep(state().dialogs[index(kind)], std::move(service));
    }
    FileDialogService* dialogs(PlatformKind kind) const {
        return state_ == nullptr ? nullptr : state_->dialogs[index(kind)].get();
    }

    void add_shell(PlatformKind kind, std::unique_ptr<ShellService> service) {
        keep(state().shells[index(kind)], std::move(service));
    }
    ShellService* shell(PlatformKind kind) const {
        return state_ == nullptr ? nullptr : state_->shells[index(kind)].get();
    }

    void add_clipboard(std::unique_ptr<ClipboardService> service) { keep(state().clipboard, std::move(service)); }
    ClipboardService* clipboard() const { return state_ == nullptr ? nullptr : state_->clipboard.get(); }

    void add_gamepad(std::unique_ptr<GamepadService> service) { keep(state().gamepad, std::move(service)); }
    GamepadService* gamepad() const { return state_ == nullptr ? nullptr : state_->gamepad.get(); }

    void add_task_factory(std::unique_ptr<TaskServiceFactory> factory) { keep(state().tasks, std::move(factory)); }
    TaskServiceFactory* task_factory() const { return state_ == nullptr ? nullptr : state_->tasks.get(); }

    void add_notify(std::unique_ptr<NotifyService> service) { keep(state().notify, std::move(service)); }
    NotifyService* notify() const { return state_ == nullptr ? nullptr : state_->notify.get(); }

    void add_tray(std::unique_ptr<TrayService> service) { keep(state().tray, std::move(service)); }
    TrayService* tray() const { return state_ == nullptr ? nullptr : state_->tray.get(); }

    void add_hotkey(std::unique_ptr<HotkeyService> service) { keep(state().hotkey, std::move(service)); }
    HotkeyService* hotkey() const { return state_ == nullptr ? nullptr : state_->hotkey.get(); }

    void add_media_key(std::unique_ptr<MediaKeyService> service) { keep(state().media_key, std::move(service)); }
    MediaKeyService* media_key() const { return state_ == nullptr ? nullptr : state_->media_key.get(); }

    void add_instance(std::unique_ptr<InstanceService> service) { keep(state().instance_service, std::move(service)); }
    InstanceService* instance_service() const {
        return state_ == nullptr ? nullptr : state_->instance_service.get();
    }

    void add_file_association(std::unique_ptr<FileAssociationService> service) {
        keep(state().file_association, std::move(service));
    }
    FileAssociationService* file_association() const {
        return state_ == nullptr ? nullptr : state_->file_association.get();
    }

    void add_opener(std::unique_ptr<OpenerService> service) { keep(state().opener, std::move(service)); }
    OpenerService* opener() const { return state_ == nullptr ? nullptr : state_->opener.get(); }

    void add_update(std::unique_ptr<UpdateService> service) { keep(state().update, std::move(service)); }
    UpdateService* update() const { return state_ == nullptr ? nullptr : state_->update.get(); }

    void add_store(std::unique_ptr<KeyValueStore> store) { keep(state().store, std::move(store)); }
    KeyValueStore* store() const { return state_ == nullptr ? nullptr : state_->store.get(); }

    /// Views append. They do not replace one another.
    void add_view(std::unique_ptr<ViewExtension> extension) {
        if (extension == nullptr) {
            return;
        }
        state().views.push_back(std::move(extension));
    }
    void mount_views(DocumentCommit& document) {
        if (state_ == nullptr) {
            return;
        }
        for (const std::unique_ptr<ViewExtension>& view : state_->views) {
            if (view != nullptr) {
                view->mount(document);
            }
        }
    }

private:
    static constexpr std::size_t kKinds = 6;

    struct State {
        std::array<std::unique_ptr<FileDialogService>, kKinds> dialogs{};
        std::array<std::unique_ptr<ShellService>, kKinds> shells{};
        std::unique_ptr<ClipboardService> clipboard;
        std::unique_ptr<GamepadService> gamepad;
        std::unique_ptr<TaskServiceFactory> tasks;
        std::unique_ptr<NotifyService> notify;
        std::unique_ptr<TrayService> tray;
        std::unique_ptr<HotkeyService> hotkey;
        std::unique_ptr<MediaKeyService> media_key;
        std::unique_ptr<InstanceService> instance_service;
        std::unique_ptr<FileAssociationService> file_association;
        std::unique_ptr<OpenerService> opener;
        std::unique_ptr<UpdateService> update;
        std::unique_ptr<KeyValueStore> store;
        std::vector<std::unique_ptr<ViewExtension>> views;
    };

    static std::size_t index(PlatformKind kind) { return static_cast<std::size_t>(kind); }

    template <typename T>
    static void keep(std::unique_ptr<T>& slot, std::unique_ptr<T> next) {
        if (slot == nullptr) {
            slot = std::move(next);
        }
    }

    State& state() {
        if (state_ == nullptr) {
            state_ = new State();
        }
        return *state_;
    }

    // Process lifetime. Destroying plugin objects from a static destructor races
    // platform teardown, so the table is intentionally not deleted.
    State* state_ = nullptr;
};

inline PickPaths dialog_open(PlatformKind kind, NativeWindowId owner, const FileDialogOptions& opt) {
    if (FileDialogService* service = ExtensionRegistry::instance().dialogs(kind)) {
        return service->open(owner, opt);
    }
    return {PickStatus::Unsupported, {}};
}

inline PickPath dialog_save(PlatformKind kind, NativeWindowId owner, const FileDialogOptions& opt) {
    if (FileDialogService* service = ExtensionRegistry::instance().dialogs(kind)) {
        return service->save(owner, opt);
    }
    return {PickStatus::Unsupported, {}};
}

inline PickPath dialog_folder(PlatformKind kind, NativeWindowId owner, const FileDialogOptions& opt) {
    if (FileDialogService* service = ExtensionRegistry::instance().dialogs(kind)) {
        return service->folder(owner, opt);
    }
    return {PickStatus::Unsupported, {}};
}

inline bool shell_open_uri(PlatformKind kind, std::string_view uri) {
    if (ShellService* service = ExtensionRegistry::instance().shell(kind)) {
        return service->open_uri(uri);
    }
    return false;
}

inline std::string shell_user_data_dir(PlatformKind kind, std::string_view app_id) {
    if (ShellService* service = ExtensionRegistry::instance().shell(kind)) {
        return service->user_data_dir(app_id);
    }
    return {};
}

inline std::string shell_cache_dir(PlatformKind kind, std::string_view app_id) {
    if (ShellService* service = ExtensionRegistry::instance().shell(kind)) {
        return service->cache_dir(app_id);
    }
    return {};
}

}  // namespace zui

#endif
