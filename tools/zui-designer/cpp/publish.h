#ifndef ZUI_DESIGNER_PUBLISH_H
#define ZUI_DESIGNER_PUBLISH_H

#include "document.h"

#include <zui/app.h>

#include <string_view>

namespace designer {

void publish_tree(zui::App& app, Document& doc);
void swap_prefix(zui::App& app, std::string_view id, std::string_view prefix, std::string_view next);
void set_flag(zui::App& app, std::string_view id, std::string_view token, bool on);
void sync_geom(zui::App& app, const Node& node);
void sync_look(zui::App& app, const Node& node);

}  // namespace designer

#endif
