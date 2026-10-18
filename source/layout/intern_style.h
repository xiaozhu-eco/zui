#ifndef ZUI_LAYOUT_INTERN_STYLE_H
#define ZUI_LAYOUT_INTERN_STYLE_H

#include "layout/box_role.h"
#include "layout/display.h"
#include "layout/layout_style.h"
#include "layout/viewport.h"

#include "html/document.h"

#include <memory>

namespace zui {
namespace layout {

/// `viewport` is the initial containing block: `vw` / `vh` and friends resolve against it
/// (CSS Values section 5.1). A zero viewport leaves those units invalid, which is what a caller
/// that has no viewport yet gets.
std::shared_ptr<const LayoutStyle> intern_style(const html::Node* node, BoxRole role,
                                                 DisplayOuter outer, DisplayInner inner,
                                                 Viewport viewport = {});

/// Intern a style built from `node` (the same cascade, font and inherited inline facts) with the
/// box's role, display and table part overridden. The anonymous boxes of the table structure have
/// no element of their own, so they take the parent's node and say what they are here.
std::shared_ptr<const LayoutStyle> intern_style_as(const html::Node* node, BoxRole role,
                                                   DisplayOuter outer, DisplayInner inner,
                                                   TablePart part, Viewport viewport = {});

}  // namespace layout
}  // namespace zui

#endif
