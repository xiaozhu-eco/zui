#include "plugins/association/record.h"

#include <string>

namespace zui {
namespace association_detail {
namespace {

bool key_safe(std::string_view text) {
    return text.find('\\') == std::string_view::npos && text.find('/') == std::string_view::npos;
}

std::string normalize_extension(std::string_view extension) {
    if (extension.empty() || extension == ".") {
        return {};
    }
    std::string out(extension);
    if (out[0] != '.') {
        out.insert(out.begin(), '.');
    }
    if (!key_safe(std::string_view(out).substr(1))) {
        return {};
    }
    return out;
}

std::string normalize_scheme(std::string_view scheme) {
    if (scheme.empty() || !key_safe(scheme) || scheme.find(' ') != std::string_view::npos ||
        scheme.find(':') != std::string_view::npos) {
        return {};
    }
    return std::string(scheme);
}

std::string desktop_file_name(std::string_view prog_id) {
    std::string out;
    out.reserve(prog_id.size());
    for (char c : prog_id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                        c == '.' || c == '_' || c == '-';
        out.push_back(ok ? c : '_');
    }
    if (out.empty()) {
        out = "opener";
    }
    out += ".desktop";
    return out;
}

void append_mime_token(std::string& list, std::string_view token) {
    if (token.empty()) {
        return;
    }
    list.append(token.begin(), token.end());
    list.push_back(';');
}

}  // namespace

Record build_record(const plugin::OpenerSpec& spec) {
    Record record;
    record.prog_id = key_safe(spec.prog_id) ? std::string(spec.prog_id) : std::string();
    record.executable = std::string(spec.executable);
    record.app_name = spec.app_name.empty() ? record.prog_id : std::string(spec.app_name);
    record.extension = normalize_extension(spec.extension);
    record.mime = key_safe(spec.mime) || spec.mime.find('/') != std::string_view::npos
                      ? std::string(spec.mime)
                      : std::string();
    if (record.mime.find('\\') != std::string::npos) {
        record.mime.clear();
    }
    record.scheme = normalize_scheme(spec.scheme);
    if (record.executable.empty() || record.prog_id.empty()) {
        return record;
    }
    record.command = "\"" + record.executable + "\" \"%1\"";
    record.desktop_name = desktop_file_name(record.prog_id);

    std::string mime_types;
    append_mime_token(mime_types, record.mime);
    if (!record.scheme.empty()) {
        append_mime_token(mime_types, "x-scheme-handler/" + record.scheme);
    }

    record.desktop_entry = "[Desktop Entry]\nType=Application\nName=" + record.app_name +
                           "\nExec=\"" + record.executable + "\" %f\n";
    if (!mime_types.empty()) {
        record.desktop_entry += "MimeType=" + mime_types + "\n";
    }
    if (!record.extension.empty()) {
        record.desktop_entry += "X-Zui-Extension=" + record.extension + "\n";
    }
    if (!record.mime.empty()) {
        record.desktop_entry += "X-Zui-Mime=" + record.mime + "\n";
    }
    if (!record.scheme.empty()) {
        record.desktop_entry += "X-Zui-Scheme=" + record.scheme + "\n";
    }

    record.mimeapps = "[Default Applications]\n";
    if (!record.mime.empty()) {
        record.mimeapps += record.mime + "=" + record.desktop_name + "\n";
    }
    if (!record.scheme.empty()) {
        record.mimeapps += "x-scheme-handler/" + record.scheme + "=" + record.desktop_name + "\n";
    }
    return record;
}

plugin::OpenerReport report_from(const Record& record) {
    plugin::OpenerReport report;
    report.app_name = record.app_name;
    report.executable = record.executable;
    report.command = record.command;
    report.extension = record.extension;
    report.mime = record.mime;
    report.scheme = record.scheme;
    report.desktop_name = record.desktop_name;
    report.desktop_entry = record.desktop_entry;
    report.mimeapps = record.mimeapps;
    return report;
}

Classified classify(std::string_view type) {
    Classified out;
    if (type.empty()) {
        return out;
    }
    if (type.find('/') != std::string_view::npos) {
        out.kind = TypeKind::Mime;
        out.value = std::string(type);
        return out;
    }
    if (type.front() == '.' || (type.find('.') != std::string_view::npos && type.find("://") == std::string_view::npos)) {
        const std::string ext = normalize_extension(type);
        if (!ext.empty()) {
            out.kind = TypeKind::Extension;
            out.value = ext;
            return out;
        }
    }
    const std::string scheme = normalize_scheme(type);
    if (!scheme.empty()) {
        out.kind = TypeKind::Scheme;
        out.value = scheme;
    }
    return out;
}

std::string executable_from_command(std::string_view command) {
    if (command.empty()) {
        return {};
    }
    if (command.front() == '"') {
        const std::size_t end = command.find('"', 1);
        if (end == std::string_view::npos) {
            return std::string(command.substr(1));
        }
        return std::string(command.substr(1, end - 1));
    }
    const std::size_t end = command.find(' ');
    if (end == std::string_view::npos) {
        return std::string(command);
    }
    return std::string(command.substr(0, end));
}

Prepared prepare(const plugin::OpenerSpec& spec) {
    Prepared out;
    out.record = build_record(spec);
    out.report = report_from(out.record);
    if (out.record.executable.empty() || out.record.prog_id.empty() ||
        (out.record.extension.empty() && out.record.scheme.empty())) {
        out.report.ran = true;
        out.ready = false;
        return out;
    }
    out.ready = true;
    return out;
}

}  // namespace association_detail
}  // namespace zui
