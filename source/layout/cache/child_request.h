#ifndef ZUI_LAYOUT_CACHE_CHILD_REQUEST_H
#define ZUI_LAYOUT_CACHE_CHILD_REQUEST_H

#include "layout/cache/contribution.h"
#include "layout/cache/layout_key.h"

#include <cstdint>

namespace zui {
namespace layout {

struct Box;

struct ChildRequest {
    Box* child = nullptr;
    LayoutKey child_key;
    ContributionSnapshot snapshot;
    /// `BoxCache::epoch` of `child` when this request was recorded. A later hit may adopt a newer
    /// epoch once the snapshot still matches the child's result; the result is otherwise const
    /// while that check runs.
    mutable std::uint64_t child_epoch = 0;
};

}  // namespace layout
}  // namespace zui

#endif
