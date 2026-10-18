#include "ext/slots.h"

#include <zui/plugin.h>

#include <utility>

namespace zui {
namespace {

bool attached(const HostServices& services) { return static_cast<bool>(services); }

}  // namespace

plugin::OsResult HostServices::notify(std::string_view title, std::string_view body) const {
    if (attached(*this)) {
        if (NotifyService* service = ExtensionRegistry::instance().notify()) {
            return service->notify(title, body);
        }
    }
    return {};
}

plugin::OsResult HostServices::tray_item(std::string_view tip, plugin::Handler on_activate) const {
    if (attached(*this)) {
        if (TrayService* service = ExtensionRegistry::instance().tray()) {
            return service->item(tip, std::move(on_activate));
        }
    }
    return {};
}

plugin::OsResult HostServices::tray_badge(int count) const {
    if (attached(*this)) {
        if (TrayService* service = ExtensionRegistry::instance().tray()) {
            return service->badge(count);
        }
    }
    return {};
}

int HostServices::tray_badge_count() const {
    if (attached(*this)) {
        if (TrayService* service = ExtensionRegistry::instance().tray()) {
            return service->badge_count();
        }
    }
    return 0;
}

plugin::OsResult HostServices::global_hotkey(const plugin::Hotkey& key, plugin::Handler on_fire) const {
    if (attached(*this)) {
        if (HotkeyService* service = ExtensionRegistry::instance().hotkey()) {
            return service->global_hotkey(key, std::move(on_fire));
        }
    }
    return {};
}

plugin::OsResult HostServices::media_key(plugin::MediaKey action, plugin::Handler on_fire) const {
    if (attached(*this)) {
        if (MediaKeyService* service = ExtensionRegistry::instance().media_key()) {
            return service->media_key(action, std::move(on_fire));
        }
    }
    return {};
}

bool HostServices::claim_single_instance(std::string_view identity) const {
    if (attached(*this)) {
        if (InstanceService* service = ExtensionRegistry::instance().instance_service()) {
            return service->claim(identity);
        }
    }
    return false;
}

bool HostServices::signal_primary(std::string_view identity, std::string_view args) const {
    if (attached(*this)) {
        if (InstanceService* service = ExtensionRegistry::instance().instance_service()) {
            return service->signal_primary(identity, args);
        }
    }
    return false;
}

std::string HostServices::take_instance_args(std::string_view identity) const {
    if (attached(*this)) {
        if (InstanceService* service = ExtensionRegistry::instance().instance_service()) {
            return service->take_args(identity);
        }
    }
    return {};
}

bool HostServices::register_file_extension(const plugin::FileAssociation& spec) const {
    if (attached(*this)) {
        if (FileAssociationService* service = ExtensionRegistry::instance().file_association()) {
            return service->register_file_extension(spec);
        }
    }
    return false;
}

bool HostServices::unregister_file_extension(const plugin::FileAssociation& spec) const {
    if (attached(*this)) {
        if (FileAssociationService* service = ExtensionRegistry::instance().file_association()) {
            return service->unregister_file_extension(spec);
        }
    }
    return false;
}

bool HostServices::register_url_scheme(const plugin::SchemeAssociation& spec) const {
    if (attached(*this)) {
        if (FileAssociationService* service = ExtensionRegistry::instance().file_association()) {
            return service->register_url_scheme(spec);
        }
    }
    return false;
}

bool HostServices::unregister_url_scheme(const plugin::SchemeAssociation& spec) const {
    if (attached(*this)) {
        if (FileAssociationService* service = ExtensionRegistry::instance().file_association()) {
            return service->unregister_url_scheme(spec);
        }
    }
    return false;
}

plugin::OpenerReport HostServices::register_opener(const plugin::OpenerSpec& spec) const {
    if (attached(*this)) {
        if (OpenerService* service = ExtensionRegistry::instance().opener()) {
            return service->register_opener(spec);
        }
    }
    return {};
}

plugin::OpenerReport HostServices::query_opener(const plugin::OpenerSpec& spec) const {
    if (attached(*this)) {
        if (OpenerService* service = ExtensionRegistry::instance().opener()) {
            return service->query_opener(spec);
        }
    }
    return {};
}

plugin::OpenerReport HostServices::unregister_opener(const plugin::OpenerSpec& spec) const {
    if (attached(*this)) {
        if (OpenerService* service = ExtensionRegistry::instance().opener()) {
            return service->unregister_opener(spec);
        }
    }
    return {};
}

plugin::OpenerReport HostServices::launch_opener(const plugin::OpenerSpec& spec,
                                                 std::string_view file_path) const {
    if (attached(*this)) {
        if (OpenerService* service = ExtensionRegistry::instance().opener()) {
            return service->launch_opener(spec, file_path);
        }
    }
    return {};
}

plugin::UpdateReport HostServices::apply_local_update(std::string_view manifest_path,
                                                      std::string_view current_version,
                                                      std::string_view dest_dir) const {
    if (attached(*this)) {
        if (UpdateService* service = ExtensionRegistry::instance().update()) {
            return service->apply(manifest_path, current_version, dest_dir);
        }
    }
    return {};
}

}  // namespace zui
