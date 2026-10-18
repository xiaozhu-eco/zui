#ifndef ZUI_LAYOUT_CACHE_REQUEST_H
#define ZUI_LAYOUT_CACHE_REQUEST_H

#include "layout/cache/axis.h"

namespace zui {
namespace layout {

enum class RequestKind { Final, MinContent, MaxContent, FitContent, FlexBase, FlexHypothetical };

struct Request {
    RequestKind kind = RequestKind::Final;
    Axis axis = Axis::Inline;
    bool needs_baseline = false;
};

}  // namespace layout
}  // namespace zui

#endif
