#include "project.h"

#include <cctype>
#include <set>

namespace zui_tool {
namespace {

bool ident_segment(const std::string& text, bool allow_digit) {
    if (text.empty()) {
        return false;
    }
    for (std::size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        const bool letter = std::isalpha(c) != 0 || c == '_';
        const bool digit = std::isdigit(c) != 0;
        if (i == 0) {
            if (!letter && !(allow_digit && digit)) {
                return false;
            }
        } else if (!letter && !digit) {
            return false;
        }
    }
    return true;
}

std::string json_escape(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (unsigned char c : text) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20) {
                constexpr char kHex[] = "0123456789abcdef";
                out += "\\u00";
                out.push_back(kHex[c >> 4]);
                out.push_back(kHex[c & 0x0f]);
            } else {
                out.push_back(static_cast<char>(c));
            }
            break;
        }
    }
    return out;
}

class Parser {
public:
    explicit Parser(const std::string& text) : text_(text) {}

    bool parse(Project& project, std::string& error) {
        skip();
        if (!consume('{')) {
            error = "zui.json must be an object";
            return false;
        }
        skip();
        if (consume('}')) {
            error = "zui.json is empty";
            return false;
        }
        bool seen_name = false;
        bool seen_schema = false;
        bool seen_version = false;
        bool seen_identifier = false;
        bool seen_icon = false;
        bool seen_page = false;
        bool seen_entry = false;
        bool seen_targets = false;
        bool seen_permissions = false;
        bool seen_android = false;
        bool seen_windows = false;
        bool seen_apple = false;
        bool seen_target = false;
        bool seen_files = false;
        for (;;) {
            std::string key;
            if (!parse_string(key, error)) {
                return false;
            }
            skip();
            if (!consume(':')) {
                error = "zui.json is missing a colon";
                return false;
            }
            skip();
            if (key == "name" && !seen_name) {
                seen_name = true;
                if (!parse_string(project.name, error)) {
                    return false;
                }
            } else if (key == "schema" && !seen_schema) {
                seen_schema = true;
                int version = 0;
                if (!parse_int(version, error)) {
                    return false;
                }
                if (version < 1) {
                    error = "zui.json schema " + std::to_string(version) + " is not a schema";
                    return false;
                }
                if (version > kSchemaVersion) {
                    error = "zui.json schema " + std::to_string(version) +
                            " is newer than this tool reads";
                    return false;
                }
            } else if (key == "version" && !seen_version) {
                seen_version = true;
                if (!parse_string(project.version, error)) {
                    return false;
                }
            } else if (key == "identifier" && !seen_identifier) {
                seen_identifier = true;
                if (!parse_string(project.identifier, error)) {
                    return false;
                }
            } else if (key == "icon" && !seen_icon) {
                seen_icon = true;
                if (!parse_string(project.icon, error)) {
                    return false;
                }
            } else if (key == "page" && !seen_page) {
                seen_page = true;
                if (!parse_string(project.page, error)) {
                    return false;
                }
            } else if (key == "entry" && !seen_entry) {
                seen_entry = true;
                if (!parse_string(project.entry, error)) {
                    return false;
                }
            } else if (key == "target" && !seen_target) {
                seen_target = true;
                if (!parse_string(project.target, error)) {
                    return false;
                }
            } else if (key == "targets" && !seen_targets) {
                seen_targets = true;
                if (!parse_string_array(project.targets, error)) {
                    return false;
                }
            } else if (key == "files" && !seen_files) {
                seen_files = true;
                if (!parse_string_array(project.files, error)) {
                    return false;
                }
            } else if (key == "permissions" && !seen_permissions) {
                seen_permissions = true;
                if (!parse_permissions(project.permissions, error)) {
                    return false;
                }
            } else if (key == "android" && !seen_android) {
                seen_android = true;
                if (!parse_android(project.android, error)) {
                    return false;
                }
            } else if (key == "windows" && !seen_windows) {
                seen_windows = true;
                if (!parse_windows(project.windows, error)) {
                    return false;
                }
            } else if (key == "apple" && !seen_apple) {
                seen_apple = true;
                if (!parse_apple(project.apple, error)) {
                    return false;
                }
            } else if (key == "name" || key == "version" || key == "identifier" || key == "icon" ||
                       key == "page" || key == "entry" || key == "target" || key == "targets" ||
                       key == "files" || key == "permissions" || key == "android" ||
                       key == "windows" || key == "apple" || key == kSchemaKey) {
                error = "duplicate key \"" + key + "\"";
                return false;
            } else {
                error = "unknown key \"" + key + "\"";
                return false;
            }
            skip();
            if (consume('}')) {
                break;
            }
            if (!consume(',')) {
                error = "zui.json is missing a comma";
                return false;
            }
            skip();
        }
        skip();
        if (index_ != text_.size()) {
            error = "zui.json has trailing text";
            return false;
        }
        if (!seen_name) {
            error = "zui.json is missing name";
            return false;
        }
        if (!seen_version) {
            error = "zui.json is missing version";
            return false;
        }
        if (!seen_identifier) {
            error = "zui.json is missing identifier";
            return false;
        }
        if (!seen_icon) {
            error = "zui.json is missing icon";
            return false;
        }
        if (!seen_page) {
            error = "zui.json is missing page";
            return false;
        }
        if (!seen_entry) {
            error = "zui.json is missing entry";
            return false;
        }
        if (!seen_targets) {
            error = "zui.json is missing targets";
            return false;
        }
        if (!seen_permissions) {
            error = "zui.json is missing permissions";
            return false;
        }
        return true;
    }

private:
    const std::string& text_;
    std::size_t index_ = 0;

    void skip() {
        while (index_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[index_])) != 0) {
            ++index_;
        }
    }

    bool consume(char expected) {
        if (index_ < text_.size() && text_[index_] == expected) {
            ++index_;
            return true;
        }
        return false;
    }

    bool parse_string(std::string& out, std::string& error) {
        if (!consume('"')) {
            error = "zui.json expected a string";
            return false;
        }
        out.clear();
        while (index_ < text_.size()) {
            const char c = text_[index_++];
            if (c == '"') {
                return true;
            }
            if (c == '\\') {
                if (index_ >= text_.size()) {
                    error = "zui.json has a broken escape";
                    return false;
                }
                const char esc = text_[index_++];
                switch (esc) {
                case '"':
                case '\\':
                case '/':
                    out.push_back(esc);
                    break;
                case 'n':
                    out.push_back('\n');
                    break;
                case 'r':
                    out.push_back('\r');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                case 'u': {
                    if (index_ + 4 > text_.size()) {
                        error = "zui.json has a broken escape";
                        return false;
                    }
                    int code = 0;
                    for (int n = 0; n < 4; ++n) {
                        const char hex = text_[index_++];
                        code <<= 4;
                        if (hex >= '0' && hex <= '9') {
                            code += hex - '0';
                        } else if (hex >= 'a' && hex <= 'f') {
                            code += hex - 'a' + 10;
                        } else if (hex >= 'A' && hex <= 'F') {
                            code += hex - 'A' + 10;
                        } else {
                            error = "zui.json has a broken escape";
                            return false;
                        }
                    }
                    if (code < 0x80) {
                        out.push_back(static_cast<char>(code));
                    } else {
                        error = "zui.json has a broken escape";
                        return false;
                    }
                    break;
                }
                default:
                    error = "zui.json has a broken escape";
                    return false;
                }
            } else {
                out.push_back(c);
            }
        }
        error = "zui.json has an unterminated string";
        return false;
    }

    bool parse_int(int& out, std::string& error) {
        if (index_ >= text_.size() || std::isdigit(static_cast<unsigned char>(text_[index_])) == 0) {
            error = "zui.json expected a number";
            return false;
        }
        int value = 0;
        while (index_ < text_.size() &&
               std::isdigit(static_cast<unsigned char>(text_[index_])) != 0) {
            value = value * 10 + (text_[index_++] - '0');
            if (value > 100000) {
                error = "zui.json has a number that is too large";
                return false;
            }
        }
        out = value;
        return true;
    }

    bool parse_bool(bool& out, std::string& error) {
        if (text_.compare(index_, 4, "true") == 0) {
            index_ += 4;
            out = true;
            return true;
        }
        if (text_.compare(index_, 5, "false") == 0) {
            index_ += 5;
            out = false;
            return true;
        }
        error = "zui.json expected true or false";
        return false;
    }

    bool parse_android(AndroidOptions& out, std::string& error) {
        if (!consume('{')) {
            error = "zui.json expected an android object";
            return false;
        }
        skip();
        if (consume('}')) {
            return true;
        }
        std::set<std::string> seen;
        for (;;) {
            std::string key;
            if (!parse_string(key, error)) {
                return false;
            }
            if (!seen.insert(key).second) {
                error = "duplicate key \"" + key + "\"";
                return false;
            }
            skip();
            if (!consume(':')) {
                error = "zui.json is missing a colon";
                return false;
            }
            skip();
            if (key == "target") {
                if (!parse_string(out.target, error)) {
                    return false;
                }
            } else if (key == "minSdk") {
                if (!parse_int(out.min_sdk, error)) {
                    return false;
                }
            } else if (key == "targetSdk") {
                if (!parse_int(out.target_sdk, error)) {
                    return false;
                }
            } else if (key == "orientation") {
                if (!parse_string(out.orientation, error)) {
                    return false;
                }
            } else if (key == "abis") {
                if (!parse_string_array(out.abis, error)) {
                    return false;
                }
            } else if (key == "keystore") {
                if (!parse_string(out.keystore, error)) {
                    return false;
                }
            } else if (key == "keyAlias") {
                if (!parse_string(out.key_alias, error)) {
                    return false;
                }
            } else if (key == "debuggable") {
                if (!parse_bool(out.debuggable, error)) {
                    return false;
                }
            } else {
                error = "unknown android key \"" + key + "\"";
                return false;
            }
            skip();
            if (consume('}')) {
                return true;
            }
            if (!consume(',')) {
                error = "zui.json is missing a comma";
                return false;
            }
            skip();
        }
    }

    bool parse_windows(WindowsOptions& out, std::string& error) {
        if (!consume('{')) {
            error = "zui.json expected a windows object";
            return false;
        }
        skip();
        if (consume('}')) {
            return true;
        }
        std::set<std::string> seen;
        for (;;) {
            std::string key;
            if (!parse_string(key, error)) {
                return false;
            }
            if (!seen.insert(key).second) {
                error = "duplicate key \"" + key + "\"";
                return false;
            }
            skip();
            if (!consume(':')) {
                error = "zui.json is missing a colon";
                return false;
            }
            skip();
            if (key == "company") {
                if (!parse_string(out.company, error)) {
                    return false;
                }
            } else if (key == "copyright") {
                if (!parse_string(out.copyright, error)) {
                    return false;
                }
            } else if (key == "description") {
                if (!parse_string(out.description, error)) {
                    return false;
                }
            } else if (key == "certificate") {
                if (!parse_string(out.certificate, error)) {
                    return false;
                }
            } else if (key == "timestampUrl") {
                if (!parse_string(out.timestamp_url, error)) {
                    return false;
                }
            } else {
                error = "unknown windows key \"" + key + "\"";
                return false;
            }
            skip();
            if (consume('}')) {
                return true;
            }
            if (!consume(',')) {
                error = "zui.json is missing a comma";
                return false;
            }
            skip();
        }
    }

    bool parse_apple(AppleOptions& out, std::string& error) {
        if (!consume('{')) {
            error = "zui.json expected an apple object";
            return false;
        }
        skip();
        if (consume('}')) {
            return true;
        }
        std::set<std::string> seen;
        for (;;) {
            std::string key;
            if (!parse_string(key, error)) {
                return false;
            }
            if (!seen.insert(key).second) {
                error = "duplicate key \"" + key + "\"";
                return false;
            }
            skip();
            if (!consume(':')) {
                error = "zui.json is missing a colon";
                return false;
            }
            skip();
            if (key == "identity") {
                if (!parse_string(out.identity, error)) {
                    return false;
                }
            } else if (key == "category") {
                if (!parse_string(out.category, error)) {
                    return false;
                }
            } else if (key == "provisioning") {
                if (!parse_string(out.provisioning, error)) {
                    return false;
                }
            } else if (key == "entitlements") {
                if (!parse_string(out.entitlements, error)) {
                    return false;
                }
            } else if (key == "notaryProfile") {
                if (!parse_string(out.notary_profile, error)) {
                    return false;
                }
            } else if (key == "orientations") {
                if (!parse_string_array(out.orientations, error)) {
                    return false;
                }
            } else if (key == "dmg") {
                if (!parse_bool(out.dmg, error)) {
                    return false;
                }
            } else {
                error = "unknown apple key \"" + key + "\"";
                return false;
            }
            skip();
            if (consume('}')) {
                return true;
            }
            if (!consume(',')) {
                error = "zui.json is missing a comma";
                return false;
            }
            skip();
        }
    }

    bool parse_string_array(std::vector<std::string>& out, std::string& error) {
        if (!consume('[')) {
            error = "zui.json expected an array";
            return false;
        }
        skip();
        if (consume(']')) {
            return true;
        }
        for (;;) {
            std::string item;
            if (!parse_string(item, error)) {
                return false;
            }
            out.push_back(item);
            skip();
            if (consume(']')) {
                return true;
            }
            if (!consume(',')) {
                error = "zui.json is missing a comma";
                return false;
            }
            skip();
        }
    }

    bool parse_permissions(std::vector<Permission>& out, std::string& error) {
        if (!consume('[')) {
            error = "zui.json expected an array";
            return false;
        }
        skip();
        if (consume(']')) {
            return true;
        }
        for (;;) {
            if (!consume('{')) {
                error = "zui.json expected a permission";
                return false;
            }
            Permission permission;
            bool seen_name = false;
            bool seen_android = false;
            bool seen_reason = false;
            skip();
            if (!consume('}')) {
                for (;;) {
                    std::string key;
                    if (!parse_string(key, error)) {
                        return false;
                    }
                    skip();
                    if (!consume(':')) {
                        error = "zui.json is missing a colon";
                        return false;
                    }
                    skip();
                    std::string value;
                    if (!parse_string(value, error)) {
                        return false;
                    }
                    if (key == "name" && !seen_name) {
                        seen_name = true;
                        permission.name = value;
                    } else if (key == "android" && !seen_android) {
                        seen_android = true;
                        permission.android = value;
                    } else if (key == "reason" && !seen_reason) {
                        seen_reason = true;
                        permission.reason = value;
                    } else if (key == "name" || key == "android" || key == "reason") {
                        error = "duplicate key \"" + key + "\"";
                        return false;
                    } else {
                        error = "unknown key \"" + key + "\"";
                        return false;
                    }
                    skip();
                    if (consume('}')) {
                        break;
                    }
                    if (!consume(',')) {
                        error = "zui.json is missing a comma";
                        return false;
                    }
                    skip();
                }
            }
            if (!seen_name || permission.name.empty()) {
                error = "permission is missing a name";
                return false;
            }
            if (!seen_reason || permission.reason.empty()) {
                error = "permission " + permission.name + " is missing a reason";
                return false;
            }
            const CatalogPermission* known = find_catalog_permission(permission.name);
            if (known == nullptr) {
                error = "unknown permission " + permission.name;
                return false;
            }
            if (!seen_android) {
                permission.android = known->android;
            } else if (permission.android != known->android) {
                error = "permission " + permission.name + " does not match " + known->android;
                return false;
            }
            for (const Permission& have : out) {
                if (have.name == permission.name) {
                    error = "permission " + permission.name + " is listed twice";
                    return false;
                }
            }
            out.push_back(permission);
            skip();
            if (consume(']')) {
                return true;
            }
            if (!consume(',')) {
                error = "zui.json is missing a comma";
                return false;
            }
            skip();
        }
    }
};

}  // namespace

bool valid_name(const std::string& name) {
    return name.size() >= 1 && name.size() <= 63 && ident_segment(name, false);
}

bool valid_version(const std::string& version) {
    if (version.empty() || version.size() > 32 || version[0] == '.' || version.back() == '.') {
        return false;
    }
    bool digit = false;
    int dots = 0;
    for (char c : version) {
        if (c == '.') {
            if (!digit || dots == 3) {
                return false;
            }
            digit = false;
            ++dots;
        } else if (c >= '0' && c <= '9') {
            digit = true;
        } else {
            return false;
        }
    }
    return digit;
}

bool valid_identifier(const std::string& identifier) {
    if (identifier.size() < 3 || identifier.size() > 120) {
        return false;
    }
    std::string segment;
    int dots = 0;
    for (std::size_t i = 0; i <= identifier.size(); ++i) {
        if (i == identifier.size() || identifier[i] == '.') {
            if (!ident_segment(segment, false)) {
                return false;
            }
            segment.clear();
            if (i != identifier.size()) {
                ++dots;
            }
        } else {
            segment.push_back(identifier[i]);
        }
    }
    return dots >= 1;
}

bool valid_rel_path(const std::string& path) {
    if (path.empty() || path.size() > 240) {
        return false;
    }
    if (path.front() == '/' || path.front() == '\\' || path.back() == '/' || path.back() == '\\') {
        return false;
    }
    if (path.find(':') != std::string::npos || path.find('\\') != std::string::npos) {
        return false;
    }
    for (unsigned char c : path) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                        c == '_' || c == '-' || c == '.' || c == '/';
        if (!ok) {
            return false;
        }
    }
    std::string segment;
    for (std::size_t i = 0; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == '/') {
            if (segment.empty() || segment == "." || segment == "..") {
                return false;
            }
            segment.clear();
        } else {
            segment.push_back(path[i]);
        }
    }
    return true;
}

bool known_target(const std::string& id) {
    return id == "portable" || id == "installer" || id == "android";
}

bool has_target(const Project& project, const std::string& id) {
    for (const std::string& target : project.targets) {
        if (target == id) {
            return true;
        }
    }
    return false;
}

bool resolve_permission(const std::string& name, const std::string& reason, Permission& out,
                        std::string& error) {
    const CatalogPermission* known = find_catalog_permission(name);
    if (known == nullptr) {
        error = "unknown permission " + name;
        return false;
    }
    out.name = known->name;
    out.android = known->android;
    out.reason = reason.empty() ? known->reason : reason;
    if (out.reason.empty() || out.reason.find_first_of("\"\n\r") != std::string::npos) {
        error = "permission " + name + " is missing a reason";
        return false;
    }
    return true;
}

bool valid_project(const Project& project, std::string& error) {
    if (!valid_name(project.name)) {
        error = "name must be a C identifier";
        return false;
    }
    if (!valid_version(project.version)) {
        error = "version " + project.version + " is not a dotted number";
        return false;
    }
    if (!valid_identifier(project.identifier)) {
        error = "identifier " + project.identifier + " is not an application id";
        return false;
    }
    if (!valid_rel_path(project.icon)) {
        error = "icon " + project.icon + " is outside the project";
        return false;
    }
    if (!valid_rel_path(project.page)) {
        error = "page " + project.page + " is outside the project";
        return false;
    }
    if (!valid_rel_path(project.entry)) {
        error = "entry " + project.entry + " is outside the project";
        return false;
    }
    if (!project.target.empty() && !valid_name(project.target)) {
        error = "target " + project.target + " is not a C identifier";
        return false;
    }
    if (project.targets.empty()) {
        error = "missing targets";
        return false;
    }
    for (const std::string& target : project.targets) {
        if (!known_target(target)) {
            error = "unknown target " + target;
            return false;
        }
    }
    for (std::size_t i = 0; i < project.targets.size(); ++i) {
        for (std::size_t j = i + 1; j < project.targets.size(); ++j) {
            if (project.targets[i] == project.targets[j]) {
                error = "target " + project.targets[i] + " is listed twice";
                return false;
            }
        }
    }
    for (const std::string& file : project.files) {
        if (!valid_rel_path(file)) {
            error = "file " + file + " is outside the project";
            return false;
        }
    }
    for (const Permission& permission : project.permissions) {
        const CatalogPermission* known = find_catalog_permission(permission.name);
        if (known == nullptr) {
            error = "unknown permission " + permission.name;
            return false;
        }
        if (permission.android != known->android) {
            error = "permission " + permission.name + " does not match " + known->android;
            return false;
        }
        if (permission.reason.empty()) {
            error = "permission " + permission.name + " is missing a reason";
            return false;
        }
    }
    const AndroidOptions& android = project.android;
    if (android.min_sdk < 21 || android.min_sdk > 36) {
        error = "android minSdk " + std::to_string(android.min_sdk) + " is outside 21..36";
        return false;
    }
    if (android.target_sdk < android.min_sdk || android.target_sdk > 36) {
        error = "android targetSdk " + std::to_string(android.target_sdk) +
                " is outside minSdk..36";
        return false;
    }
    if (!android.orientation.empty() && android.orientation != "sensor" &&
        android.orientation != "portrait" && android.orientation != "landscape" &&
        android.orientation != "user") {
        error = "android orientation " + android.orientation + " is not one of sensor, portrait, "
                "landscape, user";
        return false;
    }
    for (const std::string& abi : android.abis) {
        if (abi != "arm64-v8a") {
            error = "android abi " + abi + " has no zui build yet; the android preset is arm64-v8a";
            return false;
        }
    }
    if (!android.target.empty() && !valid_name(android.target)) {
        error = "android target " + android.target + " is not a C identifier";
        return false;
    }
    if (!android.keystore.empty() && !valid_rel_path(android.keystore)) {
        error = "android keystore " + android.keystore + " is outside the project";
        return false;
    }
    if (!android.key_alias.empty() && android.keystore.empty()) {
        error = "android keyAlias needs a keystore";
        return false;
    }
    const WindowsOptions& windows = project.windows;
    if (!windows.certificate.empty()) {
        bool thumbprint = windows.certificate.size() == 40;
        for (char c : windows.certificate) {
            const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                             (c >= 'A' && c <= 'F');
            if (!hex) {
                thumbprint = false;
                break;
            }
        }
        if (!thumbprint && !valid_rel_path(windows.certificate)) {
            error = "windows certificate " + windows.certificate +
                    " is neither a project path nor a SHA-1 thumbprint";
            return false;
        }
    }
    const AppleOptions& apple = project.apple;
    for (const std::string& path : {apple.provisioning, apple.entitlements}) {
        if (!path.empty() && !valid_rel_path(path)) {
            error = "apple file " + path + " is outside the project";
            return false;
        }
    }
    for (const std::string& orientation : apple.orientations) {
        const bool known = orientation == "UIInterfaceOrientationPortrait" ||
                           orientation == "UIInterfaceOrientationPortraitUpsideDown" ||
                           orientation == "UIInterfaceOrientationLandscapeLeft" ||
                           orientation == "UIInterfaceOrientationLandscapeRight";
        if (!known) {
            error = "apple orientation " + orientation + " is not a UIInterfaceOrientation name";
            return false;
        }
    }
    return true;
}

std::string project_json(const Project& project) {
    std::string out = "{\n  \"schema\": " + std::to_string(kSchemaVersion) + ",\n  \"name\": \"" +
                      json_escape(project.name) +
                      "\",\n  \"version\": \"" + json_escape(project.version) +
                      "\",\n  \"identifier\": \"" + json_escape(project.identifier) +
                      "\",\n  \"icon\": \"" + json_escape(project.icon) + "\",\n  \"page\": \"" +
                      json_escape(project.page) + "\",\n  \"entry\": \"" + json_escape(project.entry) +
                      "\",\n  \"targets\": [";
    for (std::size_t i = 0; i < project.targets.size(); ++i) {
        if (i != 0) {
            out += ", ";
        }
        out += "\"" + json_escape(project.targets[i]) + "\"";
    }
    out += "],\n  \"permissions\": [";
    if (!project.permissions.empty()) {
        out += "\n";
    }
    for (std::size_t i = 0; i < project.permissions.size(); ++i) {
        const Permission& permission = project.permissions[i];
        out += "    {\n      \"name\": \"" + json_escape(permission.name) +
               "\",\n      \"android\": \"" + json_escape(permission.android) +
               "\",\n      \"reason\": \"" + json_escape(permission.reason) + "\"\n    }";
        if (i + 1 != project.permissions.size()) {
            out += ",";
        }
        out += "\n";
    }
    out += project.permissions.empty() ? "]" : "  ]";
    if (!project.target.empty()) {
        out += ",\n  \"target\": \"" + json_escape(project.target) + "\"";
    }
    if (!project.files.empty()) {
        out += ",\n  \"files\": [";
        for (std::size_t i = 0; i < project.files.size(); ++i) {
            if (i != 0) {
                out += ", ";
            }
            out += "\"" + json_escape(project.files[i]) + "\"";
        }
        out += "]";
    }
    if (has_target(project, "android")) {
        const AndroidOptions& android = project.android;
        out += ",\n  \"android\": {\n    \"minSdk\": " + std::to_string(android.min_sdk) +
               ",\n    \"targetSdk\": " + std::to_string(android.target_sdk);
        if (!android.abis.empty()) {
            out += ",\n    \"abis\": [";
            for (std::size_t i = 0; i < android.abis.size(); ++i) {
                if (i != 0) {
                    out += ", ";
                }
                out += "\"" + json_escape(android.abis[i]) + "\"";
            }
            out += "]";
        }
        if (!android.orientation.empty()) {
            out += ",\n    \"orientation\": \"" + json_escape(android.orientation) + "\"";
        }
        if (!android.target.empty()) {
            out += ",\n    \"target\": \"" + json_escape(android.target) + "\"";
        }
        if (!android.keystore.empty()) {
            out += ",\n    \"keystore\": \"" + json_escape(android.keystore) + "\"";
        }
        if (!android.key_alias.empty()) {
            out += ",\n    \"keyAlias\": \"" + json_escape(android.key_alias) + "\"";
        }
        if (android.debuggable) {
            out += ",\n    \"debuggable\": true";
        }
        out += "\n  }";
    }
    if (!project.windows.company.empty() || !project.windows.copyright.empty() ||
        !project.windows.description.empty() || !project.windows.certificate.empty() ||
        !project.windows.timestamp_url.empty()) {
        const WindowsOptions& windows = project.windows;
        out += ",\n  \"windows\": {";
        bool first = true;
        auto add = [&out, &first](const char* key, const std::string& value) {
            out += first ? "\n" : ",\n";
            first = false;
            out += std::string("    \"") + key + "\": \"" + json_escape(value) + "\"";
        };
        if (!windows.description.empty()) {
            add("description", windows.description);
        }
        if (!windows.company.empty()) {
            add("company", windows.company);
        }
        if (!windows.copyright.empty()) {
            add("copyright", windows.copyright);
        }
        if (!windows.certificate.empty()) {
            add("certificate", windows.certificate);
        }
        if (!windows.timestamp_url.empty()) {
            add("timestampUrl", windows.timestamp_url);
        }
        out += "\n  }";
    }
    const AppleOptions& apple = project.apple;
    if (!apple.identity.empty() || !apple.category.empty() || !apple.provisioning.empty() ||
        !apple.entitlements.empty() || !apple.notary_profile.empty() ||
        !apple.orientations.empty() || !apple.dmg) {
        out += ",\n  \"apple\": {";
        bool first = true;
        auto add = [&out, &first](const char* key, const std::string& value) {
            out += first ? "\n" : ",\n";
            first = false;
            out += std::string("    \"") + key + "\": \"" + json_escape(value) + "\"";
        };
        if (!apple.identity.empty()) {
            add("identity", apple.identity);
        }
        if (!apple.category.empty()) {
            add("category", apple.category);
        }
        if (!apple.provisioning.empty()) {
            add("provisioning", apple.provisioning);
        }
        if (!apple.entitlements.empty()) {
            add("entitlements", apple.entitlements);
        }
        if (!apple.notary_profile.empty()) {
            add("notaryProfile", apple.notary_profile);
        }
        if (!apple.orientations.empty()) {
            out += first ? "\n    \"orientations\": [" : ",\n    \"orientations\": [";
            first = false;
            for (std::size_t i = 0; i < apple.orientations.size(); ++i) {
                if (i != 0) {
                    out += ", ";
                }
                out += "\"" + json_escape(apple.orientations[i]) + "\"";
            }
            out += "]";
        }
        if (!apple.dmg) {
            out += first ? "\n    \"dmg\": false" : ",\n    \"dmg\": false";
            first = false;
        }
        out += "\n  }";
    }
    out += "\n}\n";
    return out;
}

bool parse_project(const std::string& json, Project& project, std::string& error) {
    Project parsed;
    Parser parser(json);
    if (!parser.parse(parsed, error)) {
        return false;
    }
    if (!valid_project(parsed, error)) {
        return false;
    }
    project = std::move(parsed);
    return true;
}

int version_code(const std::string& version) {
    int parts[3] = {0, 0, 0};
    int index = 0;
    int value = 0;
    bool any = false;
    for (std::size_t i = 0; i <= version.size() && index < 3; ++i) {
        if (i == version.size() || version[i] == '.') {
            if (any) {
                parts[index] = value;
            }
            ++index;
            value = 0;
            any = false;
        } else if (version[i] >= '0' && version[i] <= '9') {
            any = true;
            value = value * 10 + (version[i] - '0');
            if (value > 99) {
                value = 99;
            }
        }
    }
    const int code = parts[0] * 10000 + parts[1] * 100 + parts[2];
    return code == 0 ? 1 : code;
}

}  // namespace zui_tool
