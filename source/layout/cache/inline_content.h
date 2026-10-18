#ifndef ZUI_LAYOUT_CACHE_INLINE_CONTENT_H
#define ZUI_LAYOUT_CACHE_INLINE_CONTENT_H

#include "layout/cache/line_box.h"

#include <vector>

namespace zui {
namespace layout {

struct InlineContent {
    std::vector<LineBox> lines;
};

}  // namespace layout
}  // namespace zui

#endif
