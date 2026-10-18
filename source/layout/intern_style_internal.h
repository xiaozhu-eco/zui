#ifndef ZUI_SOURCE_LAYOUT_INTERN_STYLE_INTERNAL_H
#define ZUI_SOURCE_LAYOUT_INTERN_STYLE_INTERNAL_H

#include "html/document.h"
#include "css/values.h"

namespace zui {
namespace layout {

bool style_needs_font_rel(const StyleMap& style);
css::FontRelativeUnits font_rel_of(const html::Node* node);

}  // namespace layout
}  // namespace zui

#endif
