#ifndef ZUI_LAYOUT_CACHE_IN_FLOW_H
#define ZUI_LAYOUT_CACHE_IN_FLOW_H

#include "layout/cache/axis.h"
#include "layout/cache/layout_result.h"

namespace zui {
namespace layout {

struct Box;
struct LayoutKey;
class LayoutContext;

// Builds the child constraint from the parent's solved border box, records static positions, and
// asks in-flow children along one axis. Block layout has its own pass in flow/flow.cpp and
// flex and grid have their own algorithms, so this stays the plain one-axis walk.
class InFlowPass {
public:
    static LayoutResult layout(Box& box, const LayoutKey& key, LayoutContext& cx, Axis main);
};

}  // namespace layout
}  // namespace zui

#endif
