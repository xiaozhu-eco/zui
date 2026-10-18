#ifndef ZUI_LAYOUT_CACHE_CONTRIBUTION_H
#define ZUI_LAYOUT_CACHE_CONTRIBUTION_H

#include "layout/cache/box_geometry.h"
#include "layout/cache/break_token.h"
#include "layout/cache/float_list.h"
#include "layout/cache/margin_strut.h"
#include "layout/cache/overflow.h"

namespace zui {
namespace layout {

struct ContributionSnapshot {
    BoxGeometry geometry;
    MarginStrut margin_strut;
    Overflow overflow;
    FloatList floats;
    BreakToken break_token;
};

}  // namespace layout
}  // namespace zui

#endif
