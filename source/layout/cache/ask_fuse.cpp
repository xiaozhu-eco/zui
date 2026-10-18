#include "layout/cache/ask_fuse.h"

#include "layout/box.h"

#include <utility>

namespace zui {
namespace layout {

const LayoutResult* AskFuse::refuse(const Box& child, const LayoutKey& key, bool& capped) const {
    const auto it = slots_.find(&child);
    if (it == slots_.end() || it->second.count < 2 || it->second.second == nullptr) {
        return nullptr;
    }
    ++key_compares_;
    if (!key.equal(it->second.second_key)) {
        capped = true;
    }
    return it->second.second;
}

void AskFuse::record(Box& child, const LayoutKey& key, const LayoutResult* result) {
    Slot& slot = slots_[&child];
    ++slot.count;
    if (slot.count == 2) {
        slot.second_key = key;
        slot.second = result;
    }
}

void AskFuse::keep_latest(const Box& child, std::size_t last) { keep_from_[&child] = last; }

void AskFuse::drop_superseded(std::vector<ChildRequest>& requests) const {
    if (keep_from_.empty()) {
        return;
    }
    // A single pass over the frame's requests, done once when the frame hands them back. The old
    // version searched this vector for the child's last ask and compacted it after *every* cell, so
    // a 40x40 table moved 15.5M `ChildRequest`s (each carrying a whole LayoutKey) per layout. The
    // entries to drop sit just behind the ask that was read, so the shift is a move, not a copy.
    std::size_t write = 0;
    for (std::size_t i = 0; i < requests.size(); ++i) {
        const auto cut = keep_from_.find(requests[i].child);
        if (cut != keep_from_.end() && i < cut->second) {
            continue;
        }
        if (write != i) {
            requests[write] = std::move(requests[i]);
        }
        ++write;
    }
    requests.resize(write);
}

void AskFuse::reset(const Box& child) {
    slots_.erase(&child);
}

void AskFuse::clear() {
    slots_.clear();
    keep_from_.clear();
    key_compares_ = 0;
}

}  // namespace layout
}  // namespace zui
