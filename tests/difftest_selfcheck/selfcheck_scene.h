#pragma once

// Shared harness for the L6 self-checks. The cases were split out of the former
// single-file selfcheck into the sibling *_test.cpp files; this header keeps the
// scene fixture (`Scene`) and the measurement helpers they all build on.

// Architecture self-checks (TESTPLAN section 12) - the L6 layer.
//
// These asserts run inside the engine and never touch a browser. They are what stops the
// comparison pipeline from "passing" for the wrong reason: if a second layout silently rebuilt
// the tree, or a scroll re-ran layout, pixel parity could still look fine while the model is
// broken. Run through ctest, and folded into the difftest report as layer L6.

#include <gtest/gtest.h>

#include "css/engine/engine.h"
#include "css/engine/http_fetch.h"
#include "css/shorthand/shorthand.h"
#include "css/transition/transition.h"
#include "css/values.h"
#include "html/document.h"
#include "input/interaction.h"
#include "layout/box.h"
#include "layout/cache/box_cache.h"
#include "layout/cache/layout_key.h"
#include "layout/cache/layout_result.h"
#include "layout/clip_path.h"
#include "layout/engine.h"
#include "layout/layout_style.h"
#include "layout/paint_map.h"
#include "layout/stacking.h"
#include "layout/style_table.h"
#include "paint/display_list.h"
#include "text/font_chain.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace difftest_selfcheck {

inline std::string fixture(const char* leaf) {
    return std::string(ZUI_SOURCE_DIR "/tests/difftest/selfcheck/") + leaf;
}

/// The order- and address-independent form of "was this box laid out again?": the pinned Final of
/// @p box records a request for @p child that still matches @p child's *current* layout key.
///
/// Comparing result *pointers* flakes about one run in fifteen: results are freed and re-allocated
/// every frame and the allocator can hand the same address back, so a box that was re-laid out can
/// answer with the pointer the check captured before the change (FontArrivalKeepsContentVersion
/// documents the same lesson). A result that came from the cache still names the pre-change key.
inline bool published_records_current(zui::layout::Box* box, zui::layout::Box* child) {
    if (box == nullptr || box->cache == nullptr || child == nullptr) {
        return false;
    }
    const zui::layout::LayoutResult* published = box->cache->pinned_final();
    if (published == nullptr) {
        return false;
    }
    for (const zui::layout::ChildRequest& request : published->used_requests) {
        if (request.child == child &&
            zui::layout::key_for(*child, request.child_key.space).equal(request.child_key)) {
            return true;
        }
    }
    return false;
}

/// One loaded page plus the state the checks need to talk about pointers.
struct Scene {
    zui::html::Document doc;
    zui::css::Engine css;
    zui::css::Transitions motion;
    zui::layout::FreeTypeTextMetrics metrics;
    std::unique_ptr<zui::layout::LayoutEngine> layout;
    std::size_t cascade_count = 0;
    double clock = 0;  // seconds; `freeze` / `advance` in a case are milliseconds

    bool load(std::string& error, const char* leaf = "sample.html") {
        return load_path(error, fixture(leaf));
    }

    /// Loads @p path itself rather than `fixture(leaf)`. A case that needs a scratch document
    /// (one it writes and deletes) uses this so the fixture directory stays read-only.
    bool load_path(std::string& error, const std::string& path) {
        if (!zui::html::load_file(path, doc, error)) {
            return false;
        }
        if (!css.load_sheets(doc, error)) {
            return false;
        }
        layout = std::make_unique<zui::layout::LayoutEngine>(metrics);
        frame();
        return true;
    }

    zui::css::LengthBasis length_basis(const zui::html::Node* node) const {
        zui::css::LengthBasis b;
        if (node == nullptr || layout == nullptr) {
            return b;
        }
        if (node->parent != nullptr) {
            if (zui::layout::Box* parent = layout->principal_box(node->parent)) {
                b.inline_px = parent->geo.content_width;
                b.block_px = parent->geo.content_height;
                b.has_inline = true;
                b.has_block = true;
            }
        } else {
            b.inline_px = 800.f;
            b.block_px = 600.f;
            b.has_inline = true;
            b.has_block = true;
        }
        if (zui::layout::Box* box = layout->principal_box(const_cast<zui::html::Node*>(node))) {
            b.used_width = box->geo.content_width;
            b.used_height = box->geo.content_height;
            b.has_used_width = true;
            b.has_used_height = true;
        }
        return b;
    }

    void frame(double clock_ms = -1) {
        if (clock_ms >= 0) {
            clock = clock_ms / 1000.0;
        }
        const zui::css::Engine::CascadeResult cr = css.cascade(doc, 800, 600);
        motion.set_length_basis([this](const zui::html::Node* n) { return length_basis(n); });
        motion.sync(cr, clock);
        ++cascade_count;
        for (zui::html::Node* node : cr.layout) {
            layout->mark_needs_layout(node);
        }
        layout->layout(doc, zui::layout::Viewport{800, 600});
        for (int cq = 0; cq < 2; ++cq) {
            if (!layout->frame_stats().container_size_changed) {
                break;
            }
            const zui::css::Engine::CascadeResult cq_cr = css.cascade(doc, 800, 600);
            motion.sync(cq_cr, clock);
            ++cascade_count;
            for (zui::html::Node* n : cq_cr.layout) {
                layout->mark_needs_layout(n);
            }
            layout->layout(doc, zui::layout::Viewport{800, 600});
        }
        motion.resolve_pending();
        motion.sample(clock);
        if (motion.needs_layout()) {
            for (zui::html::Node* n : motion.layout_nodes()) {
                layout->mark_needs_layout(n);
            }
            layout->layout(doc, zui::layout::Viewport{800, 600});
        }
    }

    zui::layout::Box* box(const char* id) {
        zui::html::Node* node = zui::html::find(doc, id);
        return node != nullptr ? layout->principal_box(node) : nullptr;
    }

    static const zui::layout::LayoutResult* result(const zui::layout::Box* box) {
        return box != nullptr && box->cache != nullptr ? box->cache->pinned_final() : nullptr;
    }

    /// One frame, spelled the way `App::Impl::flush_style` spells it: cascade, sync the box tree
    /// of every host this frame owes (the `pending_attach` set plus the parents of the nodes the
    /// cascade reattaches), mark the nodes whose computed style changed, then lay out. The
    /// structural checks insert or remove a fragment between the cascade and the sync, so they
    /// cannot use `frame()`, which only cascades and lays out.
    void app_frame(const std::vector<zui::html::Node*>& hosts = {}) {
        const zui::css::Engine::CascadeResult cr = css.cascade(doc, 800, 600);
        motion.set_length_basis([this](const zui::html::Node* n) { return length_basis(n); });
        motion.sync(cr, clock);
        ++cascade_count;
        const std::unordered_set<zui::html::Node*> reattach(cr.reattach.begin(), cr.reattach.end());
        std::vector<zui::html::Node*> sync = hosts;
        const auto push_unique = [&sync](zui::html::Node* host) {
            if (host != nullptr && std::find(sync.begin(), sync.end(), host) == sync.end()) {
                sync.push_back(host);
            }
        };
        for (zui::html::Node* node : cr.reattach) {
            push_unique(node->parent != nullptr ? node->parent : node);
        }
        for (zui::html::Node* host : sync) {
            layout->sync_children(host, reattach);
        }
        for (zui::html::Node* node : cr.layout) {
            layout->mark_needs_layout(node);
        }
        layout->layout(doc, zui::layout::Viewport{800, 600});
        motion.resolve_pending();
        motion.sample(clock);
        if (motion.needs_layout()) {
            for (zui::html::Node* n : motion.layout_nodes()) {
                layout->mark_needs_layout(n);
            }
            layout->layout(doc, zui::layout::Viewport{800, 600});
        }
    }
};

inline void collect_boxes(const zui::layout::Box* box,
                   std::unordered_map<const zui::layout::Box*, bool>& out) {
    if (box == nullptr) {
        return;
    }
    out[box] = true;
    for (const zui::layout::Box* child : box->children) {
        collect_boxes(child, out);
    }
}



}  // namespace difftest_selfcheck
