#include "layout/cache/box_cache.h"

#include <algorithm>

namespace zui {
namespace layout {
namespace {

bool result_names(const LayoutResult& result, const std::unordered_set<const Box*>& boxes) {
    for (const ChildPlace& place : result.child_places) {
        if (place.box != nullptr && boxes.count(place.box) != 0) {
            return true;
        }
    }
    for (const ChildRequest& request : result.used_requests) {
        if (request.child != nullptr && boxes.count(request.child) != 0) {
            return true;
        }
    }
    return false;
}

}  // namespace

const LayoutResult* BoxCache::find(const LayoutKey& key) const {
    const auto it = slots_.find(key);
    return it == slots_.end() ? nullptr : it->second.get();
}

bool BoxCache::store(LayoutKey key, std::shared_ptr<LayoutResult> result) {
    if (key.space.request.kind == RequestKind::Final) {
        pin_ = result;
    }
    // `order_` holds the map's own keys, not copies: a LayoutKey carries a whole ConstraintSpace,
    // and copying it once per store was a third of the store cost on a 3.7k-box page. The map is
    // node-based, so a key stays put while other slots come and go.
    const auto inserted = slots_.find(key);
    if (inserted == slots_.end()) {
        const bool measure = key.space.request.kind != RequestKind::Final;
        auto [it, ok] = slots_.emplace(std::move(key), std::move(result));
        if (ok) {
            order_.push_back(&it->first);
            if (measure) {
                ++measures_;
            }
        }
        return false;
    }
    inserted->second = std::move(result);
    return true;
}

void BoxCache::drop_slots() {
    // `clear()` re-hashes. After `note_icb_length` the stored keys no longer live in the buckets
    // that hash names, and MSVC frees the node without unlinking the old bucket (key_policy.h).
    // Swapping with an empty map destroys the nodes from the list and does not hash.
    decltype(slots_) discarded;
    slots_.swap(discarded);
    order_.clear();
    measures_ = 0;
}

std::size_t BoxCache::drop_slots_naming(const std::unordered_set<const Box*>& boxes) {
    if (boxes.empty()) {
        return 0;
    }
    // Walk `slots_` directly. `order_` holds pointers into the map; hashing those keys through
    // `slots_.find(**it)` after a tree rebuild has been observed to read a freed ExclusionSpace
    // (UAF in ConstraintSpace::hash). Erasing by map iterator never re-hashes an existing key.
    std::size_t dropped = 0;
    for (auto it = slots_.begin(); it != slots_.end();) {
        if (it->second == nullptr || !result_names(*it->second, boxes)) {
            ++it;
            continue;
        }
        if (pin_ == it->second) {
            pin_.reset();
        }
        if (it->first.space.request.kind != RequestKind::Final) {
            --measures_;
        }
        const LayoutKey* key_ptr = &it->first;
        order_.erase(std::remove(order_.begin(), order_.end(), key_ptr), order_.end());
        it = slots_.erase(it);
        ++dropped;
    }
    if (pin_ != nullptr && result_names(*pin_, boxes)) {
        pin_.reset();
    }
    return dropped;
}

bool BoxCache::touch_final(const LayoutKey& key) {
    if (key.space.request.kind != RequestKind::Final) {
        return false;
    }
    const auto it = slots_.find(key);
    if (it != slots_.end() && it->second != nullptr && it->second != pin_) {
        pin_ = it->second;
        return true;
    }
    return false;
}

std::size_t BoxCache::measure_count() const { return measures_; }

std::size_t BoxCache::evict_measures(std::size_t limit) {
    if (measures_ <= limit) {
        return 0;
    }
    // `order_` is insertion order of pointers into `slots_`. A stale pointer (key already erased
    // from the map) must not be dereferenced or hashed: `slots_.erase(*key)` would run
    // LayoutKey::hash on freed ConstraintSpace/ExclusionSpace memory. Match by address instead.
    std::unordered_set<const LayoutKey*> live;
    live.reserve(slots_.size());
    for (const auto& slot : slots_) {
        live.insert(&slot.first);
    }
    order_.erase(std::remove_if(order_.begin(), order_.end(),
                                [&live](const LayoutKey* key) { return live.count(key) == 0; }),
                 order_.end());

    std::vector<const LayoutKey*> measures;
    measures.reserve(measures_);
    for (const LayoutKey* key : order_) {
        if (key->space.request.kind != RequestKind::Final) {
            measures.push_back(key);
        }
    }
    if (measures.size() <= limit) {
        measures_ = measures.size();
        return 0;
    }
    const std::size_t drop = measures.size() - limit;
    for (std::size_t i = 0; i < drop; ++i) {
        const LayoutKey* key = measures[i];
        order_.erase(std::remove(order_.begin(), order_.end(), key), order_.end());
        for (auto it = slots_.begin(); it != slots_.end(); ++it) {
            if (&it->first == key) {
                slots_.erase(it);
                break;
            }
        }
    }
    measures_ = 0;
    for (const LayoutKey* key : order_) {
        if (key->space.request.kind != RequestKind::Final) {
            ++measures_;
        }
    }
    return drop;
}

}  // namespace layout
}  // namespace zui
