#ifndef ZUI_PLUGIN_H
#define ZUI_PLUGIN_H

#include <zui/export.h>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace zui {

/// Result and payload types for optional platform services. The calls themselves
/// are `Host::services()`, which is live only for an object obtained from a Host.
namespace plugin {

struct OsResult {
    bool ok = false;
    std::uint32_t os_error = 0;
    std::string title;
    std::string body;
};

using Handler = std::function<void()>;

/// Identity of one global hotkey. Two registrations with different keys stay distinct.
struct Hotkey {
    std::string key;
    bool control = false;
    bool alt = false;
    bool shift = false;
    bool meta = false;

    friend bool operator==(const Hotkey& lhs, const Hotkey& rhs) noexcept {
        return lhs.key == rhs.key && lhs.control == rhs.control && lhs.alt == rhs.alt &&
               lhs.shift == rhs.shift && lhs.meta == rhs.meta;
    }
    friend bool operator!=(const Hotkey& lhs, const Hotkey& rhs) noexcept { return !(lhs == rhs); }
};

/// The only media-key actions the host accepts.
enum class MediaKey { Play, Next, Previous };

inline const char* media_key_name(MediaKey action) noexcept {
    switch (action) {
    case MediaKey::Play:
        return "play";
    case MediaKey::Next:
        return "next";
    case MediaKey::Previous:
        return "previous";
    }
    return "";
}

/// File-extension registration. Unregister takes this same identity.
struct FileAssociation {
    std::string extension;
    std::string prog_id;
    std::string executable;
};

/// URL-scheme registration. Unregister takes this same identity.
struct SchemeAssociation {
    std::string scheme;
    std::string prog_id;
    std::string executable;
};

/// Choosable opener. Query and unregister take this same identity.
/// Strings are owned, so a temporary cannot be stored as a view.
struct OpenerSpec {
    std::string app_name;
    std::string prog_id;
    std::string executable;
    std::string extension;
    std::string mime;
    std::string scheme;
};

/// Lookup key a platform query already understands: extension, else mime, else scheme, else prog id.
inline std::string opener_lookup(const OpenerSpec& spec) {
    if (!spec.extension.empty()) {
        return spec.extension;
    }
    if (!spec.mime.empty()) {
        return spec.mime;
    }
    if (!spec.scheme.empty()) {
        return spec.scheme;
    }
    return spec.prog_id;
}

struct OpenerReport {
    bool ran = false;
    bool ok = false;
    std::uint32_t os_error = 0;
    std::string app_name;
    std::string executable;
    std::string command;
    std::string extension;
    std::string mime;
    std::string scheme;
    std::string desktop_name;
    std::string desktop_entry;
    std::string mimeapps;
};

struct UpdateReport {
    bool newer = false;
    bool applied = false;
    std::string version;
    std::string payload;
};

}  // namespace plugin

/// Optional OS operations for one running App.
///
/// A default-constructed object is not attached to the process services.
/// `Host::services()` returns the object that is.
class ZUI_EXPORT HostServices {
public:
    HostServices() = default;

    plugin::OsResult notify(std::string_view title, std::string_view body) const;
    plugin::OsResult tray_item(std::string_view tip, plugin::Handler on_activate) const;
    plugin::OsResult tray_badge(int count) const;
    int tray_badge_count() const;
    plugin::OsResult global_hotkey(const plugin::Hotkey& key, plugin::Handler on_fire) const;
    plugin::OsResult media_key(plugin::MediaKey action, plugin::Handler on_fire) const;
    bool claim_single_instance(std::string_view identity) const;
    bool signal_primary(std::string_view identity, std::string_view args) const;
    std::string take_instance_args(std::string_view identity) const;
    bool register_file_extension(const plugin::FileAssociation& spec) const;
    bool unregister_file_extension(const plugin::FileAssociation& spec) const;
    bool register_url_scheme(const plugin::SchemeAssociation& spec) const;
    bool unregister_url_scheme(const plugin::SchemeAssociation& spec) const;
    plugin::OpenerReport register_opener(const plugin::OpenerSpec& spec) const;
    plugin::OpenerReport query_opener(const plugin::OpenerSpec& spec) const;
    plugin::OpenerReport unregister_opener(const plugin::OpenerSpec& spec) const;
    plugin::OpenerReport launch_opener(const plugin::OpenerSpec& spec, std::string_view file_path) const;
    plugin::UpdateReport apply_local_update(std::string_view manifest_path,
                                            std::string_view current_version,
                                            std::string_view dest_dir) const;

    explicit operator bool() const noexcept { return live_; }

private:
    friend class Host;
    struct LiveTag {};
    explicit HostServices(LiveTag) noexcept : live_(true) {}

    bool live_ = false;
};

}  // namespace zui

#endif  // ZUI_PLUGIN_H
