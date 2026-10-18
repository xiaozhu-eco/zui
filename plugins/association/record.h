#ifndef ZUI_PLUGIN_ASSOCIATION_RECORD_H
#define ZUI_PLUGIN_ASSOCIATION_RECORD_H

// The opener record is pure data. Platform code writes it; tests can read the
// same fields without Explorer or xdg-mime.

#include <zui/plugin.h>

#include <string>
#include <string_view>

namespace zui {
namespace association_detail {

struct Record {
    std::string app_name;
    std::string prog_id;
    std::string executable;
    std::string extension;
    std::string mime;
    std::string scheme;
    std::string command;
    std::string desktop_name;
    std::string desktop_entry;
    std::string mimeapps;
};

enum class TypeKind { Empty, Extension, Mime, Scheme };

struct Classified {
    TypeKind kind = TypeKind::Empty;
    std::string value;
};

Record build_record(const plugin::OpenerSpec& spec);
plugin::OpenerReport report_from(const Record& record);
Classified classify(std::string_view type);
std::string executable_from_command(std::string_view command);

struct Prepared {
    Record record;
    plugin::OpenerReport report;
    bool ready = false;
};

Prepared prepare(const plugin::OpenerSpec& spec);

}  // namespace association_detail
}  // namespace zui

#endif
