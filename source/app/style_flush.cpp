#include "app/impl.h"

namespace zui {

void App::Impl::DocumentPart::flush_style(App::Impl& self) {
    auto& document = self.document;
    auto& styles = self.styles;
    auto& layouter = self.layouter;
    auto& motion = self.motion;
    auto& animation = self.animation;
    auto& restyle_q_ = self.restyle_q_;
    auto& pending_relower = self.pending_relower;
    auto& pending_attach = self.pending_attach;
    auto& viewport = self.viewport;
    auto& geometry_dirty = self.geometry_dirty;
    auto& frame_visual = self.frame_visual;
    auto& diag_ = self.diag_;
    auto& host_media_known = self.host_media_known;
    auto& host_media = self.host_media;
    auto& stage = self.stage;
    auto& frame_t = self.frame_t;
    constexpr auto kReasonModel = App::Impl::kReasonModel;
    constexpr auto kReasonFragment = App::Impl::kReasonFragment;

        const bool structure_pending = !pending_relower.empty() || !pending_attach.empty();
        if (!restyle_q_.dirty() && !geometry_dirty && !structure_pending) {
            if (!(viewport.has_value() && layouter.root() == nullptr)) {
                return;
            }
        }
        frame_visual = true;
        const float vw = viewport ? viewport->width : 0;
        const float vh = viewport ? viewport->height : 0;
        if (zui::trace::active()) {
            zui::trace::counter("restyle.reason_bits", static_cast<double>(restyle_q_.reasons()));
            html::Node* flow = html::find(document, "flow");
            zui::trace::counter("dom.flow_chapter_pages",
                                static_cast<double>(diag_.count_matching_id(flow, "chapter_page")));
            html::Node* chapters = html::find(document, "chapters");
            zui::trace::counter("dom.toc_buttons", static_cast<double>(diag_.count_ch_buttons(chapters)));
        }
        const std::uint32_t style_reasons = restyle_q_.reasons();
        restyle_q_.reset_reasons();
        // A model or fragment write can remove the element an id-only `:has()` names. A class,
        // hover, or text write cannot, so the anchor stays put while that id is still in the tree.
        const bool structural_write = structure_pending ||
                                      (style_reasons & (kReasonModel | kReasonFragment)) != 0;
        // docs/PERF.md I3: cascade only the closure of this frame's declared writes. A frame whose
        // dirty source could not name a root (hover, media, resize, theme, a query round), or a
        // page whose selectors can see outside the changed subtree, walks the whole document.
        std::unordered_set<html::Node*> style_closure;
        const bool tight_closure = restyle_q_.dirty() && host_media_known &&
                                   !restyle_q_.is_global() && !restyle_q_.roots().empty() &&
                                   !styles.closure_is_global();
        if (tight_closure) {
            // The closure walk is a stage of its own: on a page whose selectors use `:has()`
            // it is the frame's most expensive step, and without a zone of its own the trace
            // showed the frame's time nowhere (docs/PERF.md section 6.2).
            zui::trace::Zone zone("style.closure", zui::trace::Category::App);
            style_closure = styles.closure_nodes(document, restyle_q_.roots(), structural_write);
        }
        restyle_q_.clear_roots();
        restyle_q_.set_global(false);
        const auto cascade_started = std::chrono::steady_clock::now();
        css::Engine::CascadeResult cr;
        // Geometry-only edits (a text change that does not flip :empty) must not walk the cascade.
        if (restyle_q_.dirty() || !host_media_known) {
            if (!host_media_known) {
                host_media = backends::query_host_media();
                host_media_known = true;
            }
            {
                zui::trace::Zone zone("cascade", zui::trace::Category::App);
                perf::set_style_bound(tight_closure ? style_closure.size() : 0);
                cr = styles.cascade(document, vw, vh, host_media,
                                    tight_closure ? &style_closure : nullptr);
            }
            {
                zui::trace::Zone sync_zone("transition.sync", zui::trace::Category::App);
                motion.set_length_basis(
                    [&layouter](const html::Node* n) { return length_basis_of(layouter, n); });
                motion.sync(cr, frame_t);
                animation.sync(document, styles.sheet_sources(), frame_t);
            }
        }
        stage.cascade += ms_since(cascade_started);
        // docs/PERF_REFACTOR.md stage 1 / docs/PERF.md section 8 item 9: a `Paint`-only diff does
        // not enter layout, but the painter reads radii / border styles out of the box's interned
        // LayoutStyle. Re-intern exactly those boxes here. This is not a layout - nothing is marked
        // dirty, so `laid_out` stays 0 - and the geometry the box already has stays valid.
        for (html::Node* painted : cr.paint_style) {
            layouter.refresh_paint_style(painted);
        }
        if (!viewport.has_value()) {
            restyle_q_.set_dirty(false);
            return;
        }
        const bool geometry = geometry_dirty || layouter.root() == nullptr ||
                              !pending_relower.empty() || !pending_attach.empty() ||
                              !cr.reattach.empty() || !cr.layout.empty();
        if (!geometry) {
            motion.resolve_pending();
            restyle_q_.set_dirty(false);
            return;
        }
        geometry_dirty = false;
        if (layouter.root() == nullptr) {
            pending_relower.clear();
            pending_attach.clear();
            const auto layout_started = std::chrono::steady_clock::now();
            layouter.layout(document, *viewport);
            motion.resolve_pending();
            stage.layout += ms_since(layout_started);
            for (int cq = 0; cq < 2; ++cq) {
                if (!layouter.frame_stats().container_size_changed) {
                    break;
                }
                // Only the descendants of the containers whose published size changed can
                // re-match an `@container` rule (docs/PERF.md I3).
                std::unordered_set<html::Node*> cq_closure;
                const bool cq_tight = !styles.closure_is_global() &&
                                      !layouter.frame_stats().container_changed.empty();
                if (cq_tight) {
                    cq_closure =
                        styles.closure_nodes(document, layouter.frame_stats().container_changed);
                }
                perf::set_style_bound(cq_tight ? cq_closure.size() : 0);
                const auto cq_cascade_started = std::chrono::steady_clock::now();
                css::Engine::CascadeResult cq_cr =
                    styles.cascade(document, vw, vh, host_media, cq_tight ? &cq_closure : nullptr);
                stage.cascade += ms_since(cq_cascade_started);
                motion.sync(cq_cr, frame_t);
                animation.sync(document, styles.sheet_sources(), frame_t);
                for (html::Node* n : cq_cr.layout) {
                    layouter.mark_needs_layout(n);
                }
                const auto cq_layout_started = std::chrono::steady_clock::now();
                layouter.layout(document, *viewport);
                motion.resolve_pending();
                stage.layout += ms_since(cq_layout_started);
            }
            restyle_q_.set_dirty(false);
            return;
        }
        std::unordered_set<html::Node*> rebuild = pending_relower;
        pending_relower.clear();
        std::unordered_set<html::Node*> attach = pending_attach;
        pending_attach.clear();
        // The cascade's reattach set goes down with every host the frame syncs: a child that is
        // still a child but changed `display` / `position` keeps its box and re-interns its
        // style, and `flush_style` never marks those nodes.
        const std::unordered_set<html::Node*> reattach(cr.reattach.begin(), cr.reattach.end());
        for (html::Node* n : cr.reattach) {
            attach.insert(n->parent != nullptr ? n->parent : n);
        }
        {
            zui::trace::Zone zone("tree_sync", zui::trace::Category::App);
            // A host that is inside another queued host's subtree is rebuilt by that outer
            // `relower_subtree` anyway, and rebuilding it a second time is not free: a `set_model`
            // for a hidden pane (no box of its own) escalates to the nearest ancestor that has one,
            // and the same host can be queued by several `set_model` calls in one handler - the
            // calendar's month grid was rebuilt thirteen times for one day click.
            for (html::Node* h : rebuild) {
                bool covered = false;
                for (html::Node* p = h->parent; p != nullptr; p = p->parent) {
                    if (rebuild.count(p) != 0) {
                        covered = true;
                        break;
                    }
                }
                if (!covered) {
                    layouter.relower_subtree(h);
                }
            }
            // `rebuild` keeps every queued host on purpose: a host that was skipped above is still
            // accounted for here, so it does not get a second `sync_children` pass on top of the
            // outer rebuild.
            // A host with no box is built by an ancestor that is already queued. Syncing it on its
            // own takes the no-box path in `sync_children`, which relowers the nearest boxed
            // ancestor and recreates that ancestor's other children (I4). Showing a hidden sibling
            // queues both the sibling and its parent; the parent sync builds only the new subtree.
            std::unordered_set<html::Node*> unboxed_attach;
            for (html::Node* h : attach) {
                if (h != nullptr && layouter.principal_box(h) == nullptr) {
                    unboxed_attach.insert(h);
                }
            }
            for (html::Node* h : attach) {
                if (rebuild.count(h) != 0) {
                    continue;
                }
                bool covered = false;
                if (unboxed_attach.count(h) != 0) {
                    for (html::Node* p = h->parent; p != nullptr; p = p->parent) {
                        if (attach.count(p) != 0 || rebuild.count(p) != 0) {
                            covered = true;
                            break;
                        }
                    }
                }
                if (!covered) {
                    layouter.sync_children(h, reattach);
                }
            }
        }
        {
            // The cascade's `layout` set is the second thing `flush_style` does that had no zone:
            // it walks the nodes whose style changed and dirties them on the box tree.
            zui::trace::Zone zone("mark_dirty", zui::trace::Category::App);
            for (html::Node* n : cr.layout) {
                layouter.mark_needs_layout(n);
            }
        }
        const auto layout_started = std::chrono::steady_clock::now();
        layouter.layout(document, *viewport);
        motion.resolve_pending();
        stage.layout += ms_since(layout_started);
        // LAYOUT.md §10: after query containers publish content-box sizes, re-cascade so
        // `@container` rules rematch, then lay out once more. Fuse at two rounds.
        for (int cq = 0; cq < 2; ++cq) {
            if (!layouter.frame_stats().container_size_changed) {
                break;
            }
            // A container-query round only re-matches the descendants of the containers whose
            // published size changed, so it uses its own closure - unless the first round had to
            // cascade the whole document (docs/PERF.md I3), which this round must match.
            std::unordered_set<html::Node*> cq_closure;
            const bool cq_tight = tight_closure && !styles.closure_is_global() &&
                                  !layouter.frame_stats().container_changed.empty();
            if (cq_tight) {
                cq_closure =
                    styles.closure_nodes(document, layouter.frame_stats().container_changed);
                style_closure.insert(cq_closure.begin(), cq_closure.end());
            }
            perf::set_style_bound(cq_tight ? style_closure.size() : 0);
            const auto cq_cascade_started = std::chrono::steady_clock::now();
            css::Engine::CascadeResult cq_cr =
                styles.cascade(document, vw, vh, host_media, (cq_tight ? &style_closure : nullptr));
            stage.cascade += ms_since(cq_cascade_started);
            motion.sync(cq_cr, frame_t);
            animation.sync(document, styles.sheet_sources(), frame_t);
            for (html::Node* n : cq_cr.reattach) {
                layouter.sync_children(
                    n->parent != nullptr ? n->parent : n,
                    std::unordered_set<html::Node*>{cq_cr.reattach.begin(), cq_cr.reattach.end()});
            }
            for (html::Node* n : cq_cr.layout) {
                layouter.mark_needs_layout(n);
            }
            const auto cq_layout_started = std::chrono::steady_clock::now();
            layouter.layout(document, *viewport);
            motion.resolve_pending();
            stage.layout += ms_since(cq_layout_started);
        }
        // The frame has written its new pins, so anything the box tree took out of the tree can be
        // freed now. Freeing also drops the slots that named those boxes - the slots of live boxes
        // above them included (`forget_boxes`). A live box that lost its answers has to be laid out
        // again before placement reads it: without a result it is published as 0x0, which left the
        // container of a re-stamped list collapsed.
        if (layouter.release_detached_boxes()) {
            const auto repair_started = std::chrono::steady_clock::now();
            layouter.layout(document, *viewport);
            motion.resolve_pending();
            stage.layout += ms_since(repair_started);
        }
        restyle_q_.set_dirty(false);
    
}

}  // namespace zui
