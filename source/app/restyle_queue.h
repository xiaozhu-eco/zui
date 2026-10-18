#ifndef ZUI_SOURCE_APP_RESTYLE_QUEUE_H
#define ZUI_SOURCE_APP_RESTYLE_QUEUE_H

// This frame's style invalidation: each A-tier write that can change computed style declares its
// subtree, and the frame cascades only that closure (docs/PERF.md I3). A dirty source that cannot
// name a root (hover, media, theme, resize, transitions) marks the whole document instead.

#include "html/document.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace zui {
namespace app_detail {

class RestyleQueue {
public:
    /// A style change that can reach any node: cascade the whole document.
    void mark(std::uint32_t reason) {
        dirty_ = true;
        reasons_ |= reason;
        global_ = true;
    }

    /// A style change whose effect cannot leave @p root's subtree (docs/PERF.md section 2.4).
    void mark_at(std::uint32_t reason, html::Node* root) {
        dirty_ = true;
        reasons_ |= reason;
        if (global_) {
            return;
        }
        if (root == nullptr) {
            global_ = true;
            return;
        }
        roots_.push_back(root);
    }

    /// Declare each of @p roots for one reason (a hover chain, a focus move).
    void mark_many(std::uint32_t reason, const std::vector<html::Node*>& roots) {
        for (html::Node* n : roots) {
            mark_at(reason, n);
        }
    }

    /// Forget a declared root that is about to be freed with @p root's subtree. Call this *before*
    /// the subtree is detached: the parent chain is still valid then, and without it a later
    /// `flush_style` walks a freed node.
    void drop_under(html::Node* root) {
        if (root == nullptr || roots_.empty()) {
            return;
        }
        const auto in_removed_subtree = [root](html::Node* n) {
            for (html::Node* p = n; p != nullptr; p = p->parent) {
                if (p == root) {
                    return true;
                }
            }
            return false;
        };
        roots_.erase(std::remove_if(roots_.begin(), roots_.end(), in_removed_subtree), roots_.end());
    }

    bool dirty() const { return dirty_; }
    void set_dirty(bool value) { dirty_ = value; }
    bool is_global() const { return global_; }
    void set_global(bool value) { global_ = value; }
    std::uint32_t reasons() const { return reasons_; }
    void reset_reasons() { reasons_ = 0; }
    std::vector<html::Node*>& roots() { return roots_; }
    const std::vector<html::Node*>& roots() const { return roots_; }
    void clear_roots() { roots_.clear(); }

private:
    bool dirty_ = false;
    bool global_ = false;
    std::uint32_t reasons_ = 0;
    std::vector<html::Node*> roots_;
};

}  // namespace app_detail
}  // namespace zui

#endif
