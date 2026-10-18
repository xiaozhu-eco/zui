#ifndef ZUI_LAYOUT_CACHE_REPLACED_REQUEST_H
#define ZUI_LAYOUT_CACHE_REPLACED_REQUEST_H

#include "layout/cache/available_size.h"
#include "layout/cache/direction.h"
#include "layout/cache/logical_size.h"
#include "layout/cache/percentage_basis.h"
#include "layout/cache/request.h"
#include "layout/cache/writing_mode.h"

namespace zui {
namespace layout {

struct ReplacedRequest {
    AvailableSize available;
    PercentageBasis percentage;
    Request request;
    WritingMode writing_mode = WritingMode::HorizontalTb;
    Direction direction = Direction::Ltr;
    LogicalSize icb;
};

}  // namespace layout
}  // namespace zui

#endif
