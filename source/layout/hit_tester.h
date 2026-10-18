#ifndef ZUI_LAYOUT_HIT_TESTER_H
#define ZUI_LAYOUT_HIT_TESTER_H

#include "layout/box.h"

namespace zui {
namespace layout {

class HitTester {
public:
    Box* hit(Box* root, float x, float y) const;
};

}  // namespace layout
}  // namespace zui

#endif
