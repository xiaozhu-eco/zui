#include "paint/painter_internal.h"

#include "paint/display_list.h"

#include "perf/perf_budget.h"
#include "css/transition/transition.h"
#include "image/image.h"
#include "layout/clip_path.h"
#include "layout/overflow_clip.h"
#include "layout/paint_map.h"
#include "layout/paint_transform.h"
#include "layout/scrollbar_h.h"
#include "layout/scrollbar_v.h"
#include "layout/stacking.h"

#include "text/font_chain.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace zui {
namespace paint {

using namespace painter_detail;
namespace painter_detail {

/// A box whose own scroll offset moves the content painted under it: the document element - its
/// scroll moves the viewport's content even though it declares `overflow: visible` - or any box
/// the painter clips.
bool scroll_container(const layout::Box& box) {
    if (box.node == nullptr || box.style_ptr == nullptr) {
        return false;
    }
    if (box.parent == nullptr) {
        return true;
    }
    return layout::clips_overflow(box);
}

/// True when @p box or a descendant is `position: sticky` / `fixed`. Both are anchored to a scroll
/// other than the one a listing shifts with, so a walk that does not visit them cannot be recorded.
bool subtree_has_anchored(const layout::Box& box) {
    if (top_layer_member(box)) {
        // capability B: a top-layer member is anchored to the viewport by design and never enters a
        // listing. The base walk skips it, so a listing that would have held it stays usable.
        return false;
    }
    if (box.style_ptr != nullptr && (box.style().position_sticky || box.style().position_fixed)) {
        return true;
    }
    for (const layout::Box* child : box.children) {
        if (child != nullptr && subtree_has_anchored(*child)) {
            return true;
        }
    }
    return false;
}

}  // namespace painter_detail

bool Painter::recordable(const layout::Box& box) const {
    if (!scroll_container(box)) {
        return false;
    }
    for (const layout::Box* p = &box; p != nullptr; p = p->parent) {
        if (!p->geo.fragments.empty()) {
            // The layout split one of them: the walk paints that box once per fragment, and a
            // listing can only hold one of those.
            return false;
        }
    }
    // A sticky/fixed *ancestor* (album `.lightbox`, motion `.mask`, studio dialogs) does not move
    // when this box scrolls. Shifting this listing by this box's own offset is still correct; the
    // walk taints a listing that *contains* a sticky/fixed descendant (`subtree_has_anchored`).
    // Scroll offsets stay physical (`paint_map.cpp`), so a listing can only reconstruct the shift
    // for a horizontal writing mode.
    return box.style().writing_mode == layout::WritingMode::HorizontalTb;
}

void Painter::collect_outside_marks(const layout::Box& box, Record& record) const {
    for (const layout::Box* p = box.parent; p != nullptr; p = p->parent) {
        if (scroll_container(*p)) {
            record.outside.push_back(
                Record::Mark{p->node, p, p->node->scroll_x, p->node->scroll_y});
        }
    }
}

bool Painter::replay_record(const layout::Box& box, DisplayList& out, Rect clip,
                            bool has_clip) const {
    const auto it = records_.find(&box);
    if (it == records_.end()) {
        return false;
    }
    const Record& record = it->second;
    if (!record.usable || record.node != box.node || record.viewport_w != frame_w_ ||
        record.viewport_h != frame_h_) {
        return false;
    }
    const float scroll_x = box.node->scroll_x;
    const float scroll_y = box.node->scroll_y;
    if (std::fabs(scroll_x - record.scroll_x) > layout::kRetainedMargin ||
        std::fabs(scroll_y - record.scroll_y) > layout::kRetainedMargin) {
        // Scrolled past the margin the walk mapped and the listing covers: this frame pays for a
        // walk, which is what keeps the price of a jump the price of an ordinary frame.
        return false;
    }
    for (const Record::Mark& mark : record.outside) {
        if (mark.node != nullptr &&
            (mark.node->scroll_x != mark.x || mark.node->scroll_y != mark.y)) {
            return false;
        }
    }
    for (const Record::Mark& mark : record.inside) {
        if (mark.node != nullptr &&
            (mark.node->scroll_x != mark.x || mark.node->scroll_y != mark.y)) {
            // This listing flattened the nested scroller's content. Shifting the list would
            // also move siblings that do not scroll with it, so the container above is
            // re-recorded. The nested scroller keeps the listing of its own.
            return false;
        }
    }
    out.append_shifted(record.list, record.scroll_x - scroll_x, record.scroll_y - scroll_y, clip,
                       has_clip);
    // A parent that is still recording stores the offsets this replay was painted at.
    for (const Record::Mark& mark : record.inside) {
        Record::Mark now = mark;
        if (mark.node != nullptr) {
            now.x = mark.node->scroll_x;
            now.y = mark.node->scroll_y;
        }
        for (Scope& scope : scopes_) {
            scope.inside.push_back(now);
        }
    }
    ++retained_.reused;
    return true;
}

void Painter::capture_record(const layout::Box& box, DisplayList& out, std::size_t from,
                             std::size_t to, Rect clip, bool has_clip, Rect walk_clip,
                             bool walk_has_clip, const Scope& scope) const {
    Record& record = records_[&box];
    record.list.clear();
    record.outside.clear();
    record.inside = scope.inside;
    record.node = box.node;
    record.scroll_x = box.node->scroll_x;
    record.scroll_y = box.node->scroll_y;
    record.viewport_w = frame_w_;
    record.viewport_h = frame_h_;
    record.has_damage_clip = has_clip;
    record.walk_clip = walk_clip;
    record.walk_has_clip = walk_has_clip;
    if (has_clip) {
        // `listing_clip` already carries the retained margin the walk recorded with, so it is the
        // listing's extent as-is; widening it again would make one change erase every listing.
        record.damage_clip = clip;
    }
    collect_outside_marks(box, record);
    out.move_range(from, to, record.list);
    // The frame paints only the part of the listing its own clip can see. The listing keeps the
    // rest: that is exactly what the next scroll frame replays instead of walking for.
    out.append_shifted(record.list, 0.f, 0.f, clip, has_clip);
    if (scope.tainted) {
        // Something in the content is anchored to something other than this scroll - a `sticky` or
        // `fixed` box - so a plain shift would move it along with everything else. Keep the
        // refusal, not the listing: the next frame walks this container plainly instead of paying
        // for the wider window again.
        record.list.clear();
        record.usable = false;
        ++retained_.refused;
        return;
    }
    record.usable = true;
    ++retained_.walked;
}

namespace {

/// The most tiles one damage may dirty before the listing is dropped whole instead. A cap keeps
/// the narrow walk bounded: past it the frame is closer to a full re-record than to a localized
/// one, and the wholesale drop is the honest price.
constexpr std::size_t kMaxDirtyTiles = 8;

bool rects_touch(const Rect& a, float x, float y, float w, float h) {
    return x < a.x + a.w && a.x < x + w && y < a.y + a.h && a.y < y + h;
}

}  // namespace

bool Painter::damage_tiles(const layout::Box& root, const layout::Box& box, const Record& record,
                           const std::vector<html::PaintRect>& damage,
                           std::vector<Rect>& out) const {
    out.clear();
    if (!retained_on_ || !record.usable || !record.has_damage_clip || record.node == nullptr) {
        return false;
    }
    // Only the outermost listing is rebuilt tile by tile: the narrow walk starts at the frame root
    // with the clip this listing recorded through, which is the state `paint_box_at` has before any
    // scope is open. A listing nested in another one would need the outer scope's marks back.
    if (&box != &root) {
        return false;
    }
    // The rebuild reproduces device coordinates, so every offset the listing was recorded under
    // (its own and its ancestors') has to be the one it is replayed with.
    if (record.node->scroll_x != record.scroll_x || record.node->scroll_y != record.scroll_y) {
        return false;
    }
    for (const Record::Mark& mark : record.outside) {
        if (mark.node != nullptr &&
            (mark.node->scroll_x != mark.x || mark.node->scroll_y != mark.y)) {
            return false;
        }
    }
    if (record.viewport_w != frame_w_ || record.viewport_h != frame_h_ || damage.empty()) {
        return false;
    }
    const Rect clip = record.damage_clip;
    if (clip.w <= 0.f || clip.h <= 0.f) {
        return false;
    }
    const float cols_f = std::ceil(clip.w / kTilePx);
    const float rows_f = std::ceil(clip.h / kTilePx);
    const int cols = static_cast<int>(cols_f);
    const int rows = static_cast<int>(rows_f);
    if (cols <= 0 || rows <= 0) {
        return false;
    }
    const std::size_t total = static_cast<std::size_t>(cols) * static_cast<std::size_t>(rows);
    std::vector<bool> dirty(total, false);
    std::size_t dirty_count = 0;
    for (const html::PaintRect& d : damage) {
        if (!rects_touch(clip, d.x, d.y, d.w, d.h)) {
            continue;
        }
        const int c0 = static_cast<int>(std::floor((d.x - clip.x) / kTilePx));
        const int r0 = static_cast<int>(std::floor((d.y - clip.y) / kTilePx));
        const int c1 = static_cast<int>(std::floor((d.x + d.w - clip.x) / kTilePx));
        const int r1 = static_cast<int>(std::floor((d.y + d.h - clip.y) / kTilePx));
        for (int r = std::max(0, r0); r <= std::min(rows - 1, r1); ++r) {
            for (int c = std::max(0, c0); c <= std::min(cols - 1, c1); ++c) {
                const std::size_t idx = static_cast<std::size_t>(r) * static_cast<std::size_t>(cols) +
                                        static_cast<std::size_t>(c);
                if (!dirty[idx]) {
                    dirty[idx] = true;
                    ++dirty_count;
                }
            }
        }
    }
    // Nothing localized about this damage: the tiles cover the listing (or there are too many of
    // them to be worth a narrow walk), so the caller drops it whole, exactly as before.
    if (dirty_count == 0 || dirty_count == total || dirty_count > kMaxDirtyTiles) {
        return false;
    }
    out.reserve(dirty_count);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            if (!dirty[static_cast<std::size_t>(r) * static_cast<std::size_t>(cols) +
                       static_cast<std::size_t>(c)]) {
                continue;
            }
            const float x0 = clip.x + static_cast<float>(c) * kTilePx;
            const float y0 = clip.y + static_cast<float>(r) * kTilePx;
            const float x1 = std::min(clip.x + clip.w, x0 + kTilePx);
            const float y1 = std::min(clip.y + clip.h, y0 + kTilePx);
            out.push_back(Rect{x0, y0, x1 - x0, y1 - y0});
        }
    }
    return true;
}

bool Painter::rebuild_record_tiles(const layout::Box& box, Record& record,
                                   const std::vector<Rect>& tiles) const {
    DisplayList fresh;
    const std::vector<Rect>* saved_narrow = narrow_rects_;
    const layout::Box* saved_rebuilding = rebuilding_box_;
    const bool saved_failed = narrow_failed_;
    const RetainedStats saved_retained = retained_;
    const std::size_t saved_scopes = scopes_.size();
    narrow_rects_ = &tiles;
    rebuilding_box_ = &box;
    narrow_failed_ = false;
    // A dummy scope makes the walk believe it is inside another listing, which is the state the
    // outer listing was recorded in: a nested container then computes the same widened clip it
    // recorded with, so its replay hands back the items this listing already holds.
    scopes_.push_back(Scope{});
    // The full walk the listing was recorded with, narrowed to the tiles: every box that cannot
    // reach one is skipped before it is counted, and the items of the ones that can are the items
    // the listing already holds.
    paint_box_shifted(box, fresh, record.walk_clip, record.walk_has_clip, 1.0f, 0.f, 0.f);
    const bool failed = narrow_failed_ || scopes_.size() != saved_scopes + 1;
    scopes_.resize(saved_scopes);
    narrow_rects_ = saved_narrow;
    rebuilding_box_ = saved_rebuilding;
    narrow_failed_ = saved_failed;
    retained_ = saved_retained;
    if (failed) {
        return false;
    }

    std::unordered_set<const layout::Box*> visited;
    visited.reserve(fresh.items().size());
    for (const DisplayItem& item : fresh.items()) {
        if (item.owner != nullptr) {
            visited.insert(item.owner);
        }
    }

    const std::vector<DisplayItem>& rebuilt = fresh.items();
    std::vector<DisplayItem> old = record.list.take_items();
    std::vector<DisplayItem> merged;
    merged.reserve(old.size());
    std::size_t next = 0;
    for (DisplayItem& item : old) {
        if (item.owner != nullptr && visited.count(item.owner) != 0) {
            // The visited boxes appear in the listing in the order the walk made them, so the next
            // rebuilt item has to belong to the box this one does. Anything else means the two
            // walks diverged (a shape this path cannot reproduce) and the caller drops the listing.
            if (next >= rebuilt.size() || rebuilt[next].owner != item.owner) {
                return false;
            }
            merged.push_back(rebuilt[next]);
            ++next;
        } else {
            merged.push_back(std::move(item));
        }
    }
    if (next != rebuilt.size()) {
        return false;
    }
    record.list.replace_items(std::move(merged));
    return true;
}

void Painter::paint_box_shifted(const layout::Box& box, DisplayList& out, Rect clip, bool has_clip,
                                float opacity, float dx, float dy) const {
    // docs/PERF_REFACTOR.md capability E: a narrow rebuild walks only the boxes that reach one of
    // the tiles its damage dropped. The test is the same one the ordinary walk makes
    // (`layout::overflow_disjoint` over the box's overflow rectangle), so a skipped box and every
    // box under it cannot have painted into a dropped tile. Skipping before `note_paint_box` is
    // what keeps `painted_walked` at the boxes the erased tiles could see.
    if (narrow_rects_ != nullptr) {
        bool reaches = false;
        for (const Rect& tile : *narrow_rects_) {
            if (!layout::overflow_disjoint(box, tile.x, tile.y, tile.w, tile.h)) {
                reaches = true;
                break;
            }
        }
        if (!reaches) {
            return;
        }
    }
    perf::note_paint_box();
    if (box.geo.fragments.empty()) {
        paint_box_at(box, out, clip, has_clip, opacity, dx, dy);
        return;
    }
    // A box the layout split across fragmentainers is painted once per fragment, clipped to that
    // fragment's slice: each slice shows the part of the box (and of its descendants' geometry)
    // that belongs to it, which is the arrangement `translate + clip` reproduces exactly.
    for (const layout::FragmentBox& fragment : box.geo.fragments) {
        const Rect slice{fragment.world_x, fragment.world_y, fragment.width, fragment.height};
        const Rect slice_clip = has_clip ? intersect_rect(clip, slice) : slice;
        if (slice_clip.w <= 0.f || slice_clip.h <= 0.f) {
            continue;
        }
        paint_box_at(box, out, slice_clip, true, opacity, dx + fragment.paint_dx,
                     dy + fragment.paint_dy);
    }
}

}  // namespace paint
}  // namespace zui
