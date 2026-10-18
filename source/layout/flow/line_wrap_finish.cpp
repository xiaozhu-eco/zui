#include "layout/flow/detail.h"
#include "layout/baseline.h"
#include "trace/trace.h"

#include <cmath>

namespace zui {
namespace layout {
namespace flow_detail {

void finish_wrapped_line(Box& box, const UsedEdges& edges, float content_inline, bool inline_known,
                         float cb_inline, Walk& walk, std::unique_ptr<InlineContent>& lines,
                         float& y, std::vector<Piece>& line, float& line_width, float& line_start,
                         std::size_t& line_source_chars, bool& pending_break_rect, bool collapse,
                         const LayoutStyle& style, const LayoutContext::FontEm& em,
                         const AxisSize& block_percent, bool fragmented, float frag_limit,
                         float frag_own_start, float own_chrome_block, int& line_count,
                         bool last_line, bool forced_empty, LayoutContext& cx, const LayoutKey& key,
                         float flow_inline, int resume_line, const FragmentationContext& frag,
                         const std::shared_ptr<const ExclusionSpace>& exclusion) {
    (void)content_inline;
    auto limits_at = [&](float at_y, float height, float& left, float& right) {
        if (!inline_known) {
            left = 0;
            right = 1.0e6f;
            return;
        }
        float_limits(*exclusion, at_y, at_y + std::max(height, em.ascent + em.descent), flow_inline,
                     left, right);
    };
        // A forced break on an empty line is a real line box: a blank textarea line, or a line
        // that is only `<br>`. Collapsible whitespace between blocks still produces no line.
        if (line.empty() && !forced_empty) {
            return;
        }
        // A collapsible space at a soft wrap hangs: it must not consume the measure or take a
        // justification opportunity (CSS Text 3). Counting it made every `text-align: justify`
        // gap short by that space divided across the line (`inline-text` `#justify`). The trace
        // already reports the hung glyph as 0.
        if (!last_line && collapse) {
            for (int n = static_cast<int>(line.size()) - 1; n >= 0; --n) {
                Piece& piece = line[static_cast<std::size_t>(n)];
                if (piece.is_float || piece.abs_static) {
                    continue;
                }
                if (piece.space) {
                    piece.hanging = true;
                }
                break;
            }
        }
        const float font_box = std::max(0.f, em.ascent + em.descent);
        const float strut = used_line_height(style, em);
        // CSS 2.1 section 9.4.2: a line box that holds nothing but collapsible white space is a
        // zero-height line box, so its strut must not contribute. Without this, a block container
        // whose only inline content is the whitespace between its block children -- the newlines
        // in `<div>\n  <div>a</div>\n  <div>b</div>\n</div>` -- grew a line taller than its
        // content, which is what made the reader's top bar 17px too tall.
        //
        // The same rule drops an inline *element* with no content and no chrome: `<div><span></span>
        // </div>` is 0 tall in Chrome, and so is the span. An inline box keeps the line only when it
        // holds text, an atomic child, or a non-zero margin, padding or border (measured:
        // `build/empty_inline.html` against headless Chrome).
        auto empty_inline_box = [](const Piece& piece) {
            if (piece.atomic || piece.is_float || piece.abs_static || piece.box == nullptr) {
                return false;
            }
            if (piece.source_chars > 0 || piece.advance > 0.01f) {
                return false;
            }
            const LayoutResult* result = piece.result;
            if (result == nullptr) {
                return false;
            }
            const BoxGeometry& g = result->geometry;
            const float margins = g.margin.inline_start + g.margin.inline_end +
                                  g.margin.block_start + g.margin.block_end;
            return g.border_box_inline <= 0.01f && g.border_box_block <= 0.01f &&
                   margins <= 0.01f;
        };
        // A glyph-less forced break still needs the font strut. A zero block size is not a
        // caret target, and it would collapse the blank line out of the block.
        bool contributes = forced_empty;
        if (!contributes) {
            for (const Piece& piece : line) {
                if (piece.is_float || piece.abs_static) {
                    continue;
                }
                if (!(piece.space && collapse) && !empty_inline_box(piece)) {
                    contributes = true;
                    break;
                }
            }
        }
        // The half-leading goes to the *ascent* rounded down and the descent takes the rest, so an
        // inline box stays exactly `line-height` tall while its content area sits where Chrome puts
        // it: `line-height: 20px` around a 33px font box starts that box 7px above the line box
        // (not 6.5), and a 23.2px line over a 24px font box starts at -1 (not -0.4). Measured on
        // `tests/difftest/cases/inline-metrics` and `example/tests/text`'s `.sample`.
        // A line walked *for* an inline box is not that line. Its parent already owns the strut.
        // Adding half here places a padded child (`<code>` inside `span.zmd-body`) against a
        // baseline `finish` then lowers, so the chip stays a pixel below the sibling glyphs.
        const bool inline_own_line =
            style.outer == DisplayOuter::Inline && !box.atomic_inline && !box.is_flex_item &&
            !box.is_grid_item && !style.floating && style.inner == DisplayInner::Flow;
        const float half =
            inline_own_line ? 0.f : std::floor((strut - font_box) * 0.5f);
        // CSS 2.1 section 10.8: every inline box is as tall as its own `line-height`, and its
        // content area (the font box) is centred in it by the half-leading - which may be
        // negative, so glyphs are free to stick out of the line box. Treating the font box as a
        // floor made every line at least the font's own box (Noto Sans at 16px is 21.76px), which
        // broke `line-height: 20px` and pushed every following line down by 2px.
        float ascent = contributes ? em.ascent + half : 0.f;
        float descent = contributes ? strut - ascent : 0.f;
        // Boxes aligned against the line box rather than the baseline (top / bottom / middle)
        // only set a lower bound for its height: they are placed inside it afterwards.
        float min_block = 0.f;
        for (const Piece& piece : line) {
            // An out-of-flow box takes no part in the line it would have been in (CSS 2.1 9.2.1.1):
            // it neither raises the line nor paints inside it. An absolutely positioned `.sr` file
            // input otherwise left a 16px line behind and grew the page's `scrollHeight` with it
            // (`build/sr_input_probe.html`, `example-album`).
            if (piece.is_float || piece.abs_static) {
                continue;
            }
            if (empty_inline_box(piece)) {
                continue;
            }
            if (!piece.atomic) {
                const float piece_half =
                    std::floor((piece.line_height - (piece.ascent + piece.descent)) * 0.5f);
                const bool on_baseline =
                    piece.align == VerticalAlign::Baseline ||
                    piece.align == VerticalAlign::TextBottom || piece.align == VerticalAlign::Sub ||
                    piece.align == VerticalAlign::Super || piece.align == VerticalAlign::Middle ||
                    piece.align == VerticalAlign::Length;
                if (on_baseline) {
                    // align_shift raises the line-height box. sub/super/middle move that box
                    // relative to the parent's baseline, so the line has to grow with it.
                    ascent = std::max(ascent, piece.ascent + piece_half + piece.align_shift);
                    descent = std::max(descent, piece.line_height - (piece.ascent + piece_half) -
                                                    piece.align_shift);
                } else {
                    // A box aligned against the line box (top / bottom) contributes its own
                    // `line-height` box as a lower bound and leaves the baseline alone; its
                    // content area is placed inside that box by the half-leading, which is why the
                    // reported rect is not the box the line was sized from.
                    min_block = std::max(min_block, piece.line_height);
                }
                continue;
            }
            const bool on_baseline =
                piece.align == VerticalAlign::Baseline ||
                piece.align == VerticalAlign::TextBottom || piece.align == VerticalAlign::Sub ||
                piece.align == VerticalAlign::Super || piece.align == VerticalAlign::Length;
            if (on_baseline) {
                ascent = std::max(ascent, piece.baseline - piece.align_shift);
                descent = std::max(descent, piece.block - piece.baseline + piece.align_shift);
            } else {
                min_block = std::max(min_block, piece.block);
            }
        }
        float line_block = std::max(min_block, std::max(0.f, ascent + descent));
        if (fragmented) {
            if (line_count < resume_line) {
                // This line belongs to an earlier fragment. Keep the cursor moving (the placements
                // of the boxes on it were written by the pass that did emit it), but this fragment
                // reports and paints only its own lines.
                y += line_block;
                line.clear();
                line_width = 0;
                line_source_chars = 0;
                ++line_count;
                return;
            }
            if (own_chrome_block + y + line_block > frag_own_start + frag_limit + 0.01f &&
                (line_count > resume_line || !frag.at_fragment_start)) {
                // The line does not fit and this fragment is not empty (or it is not the line that
                // opens it): the line starts the next fragment. The first line of an *empty*
                // fragment stays instead, even overflowing - it is the only way to make progress.
                // orphans: minimum lines left at the bottom of a fragment; widows: minimum lines
                // at the top of a continuation (CSS Fragmentation 3 / CSS 2.1). When fewer lines
                // have been placed here than that floor, overflow instead of opening the next
                // fragment early.
                const int lines_here = line_count - resume_line;
                const int keep_min =
                    resume_line > 0 ? std::max(style.orphans, style.widows) : style.orphans;
                if (style.break_inside == BreakInside::Avoid ||
                    (lines_here > 0 && lines_here < keep_min)) {
                    // break-inside:avoid, or orphans/widows floor: keep overflowing this fragment.
                } else {
                    walk.fragment_full = true;
                    walk.fragment_line = line_count;
                    walk.fragment_block = own_chrome_block + y;
                    walk.fragment_shortage =
                        own_chrome_block + y + line_block - (frag_own_start + frag_limit);
                    return;
                }
            }
        }
        // One line is a handful of pieces. A heap vector per line was debug allocator traffic
        // on the first walk (calendar ~126 lines, flow ~367). Above 64, fall back to the heap.
        constexpr std::size_t kLineStack = 64;
        float top_stack[kLineStack];
        int mode_stack[kLineStack];
        std::vector<float> top_heap;
        std::vector<int> mode_heap;
        const std::size_t line_n = line.size();
        float* tops = top_stack;
        int* modes = mode_stack;
        if (line_n > kLineStack) {
            top_heap.assign(line_n, 0.f);
            mode_heap.assign(line_n, 0);
            tops = top_heap.data();
            modes = mode_heap.data();
        } else {
            for (std::size_t i = 0; i < line_n; ++i) {
                tops[i] = 0.f;
                modes[i] = 0;
            }
        }
        const Piece* pieces_raw = line.data();
        for (std::size_t i = 0; i < line_n; ++i) {
            const Piece& piece = pieces_raw[i];
            // An empty inline box takes no room in the line, but it still has a position: the
            // vertical-align shift moves the 0x0 box Chrome reports (`LayoutCache.NormalFlow`).
            const float block =
                piece.atomic ? piece.block : (piece.block > 0.f ? piece.block : font_box);
            // A line no piece contributes to has no strut, so its `ascent` is 0 - but the boxes on
            // it are still placed against the font's own baseline. Using 0 put an empty `<span>`
            // 19px above the line's start (`build/float_line.html`), where Chrome reports y=0.
            const float line_ascent = contributes ? ascent : em.ascent;
            // A non-atomic inline (a padded `<code>` chip) paints on its own baseline.
            // That baseline is not always the font ascent the piece was measured with, so
            // align the box to the baseline it will actually paint.
            float inline_ascent = piece.ascent;
            if (!piece.atomic && piece.result != nullptr &&
                std::isfinite(piece.result->geometry.baselines.first)) {
                const BoxGeometry& g = piece.result->geometry;
                const float painted =
                    g.baselines.first - g.border.block_start - g.padding.block_start;
                if (painted > 0.f) {
                    inline_ascent = painted;
                }
            }
            float top = piece.atomic ? line_ascent - piece.baseline - piece.align_shift
                                     : line_ascent - inline_ascent - piece.align_shift;
            if (piece.align == VerticalAlign::Top || piece.align == VerticalAlign::TextTop) {
                // `vertical-align: top` aligns the box's *line-height* box with the line box top;
                // for an inline box the content area it reports is centred inside that by the
                // (floored) half-leading.
                top = piece.atomic
                          ? 0.f
                          : std::floor((piece.line_height - (piece.ascent + piece.descent)) * 0.5f);
                modes[i] = 1;
            } else if (piece.align == VerticalAlign::Bottom) {
                modes[i] = 2;
            } else if (piece.align == VerticalAlign::Middle && piece.atomic) {
                // An atomic inline still centres in the line box. A non-atomic `middle`
                // is a baseline shift (parent x-height), applied above.
                modes[i] = 3;
            } else if (piece.align == VerticalAlign::TextBottom) {
                top = ascent + em.descent - block;
                modes[i] = 4;
            }
            tops[i] = top;
        }
        // Everything that has to fit inside the line box is already in `ascent + descent` or in
        // `min_block`: a baseline-aligned box (every text piece among them) contributes through
        // its own line-height, and its content area is allowed to overflow the line box. Adding
        // the content area here as well is what used to make a `line-height: 20px` paragraph
        // line 22px tall.
        for (std::size_t i = 0; i < line_n; ++i) {
            const Piece& piece = pieces_raw[i];
            const float block =
                piece.atomic ? piece.block : (piece.block > 0.f ? piece.block : font_box);
            if (modes[i] == 2) {
                if (piece.atomic) {
                    tops[i] = line_block - block;
                } else {
                    const float half =
                        std::floor((piece.line_height - (piece.ascent + piece.descent)) * 0.5f);
                    tops[i] = line_block - piece.line_height + half;
                }
            } else if (modes[i] == 3) {
                const float box = piece.atomic ? block : piece.line_height;
                const float half =
                    piece.atomic
                        ? 0.f
                        : std::floor((piece.line_height - (piece.ascent + piece.descent)) * 0.5f);
                tops[i] = (line_block - box) * 0.5f + half;
            }
        }
        // The content area of every piece, measured from the line's block start. The line box is
        // `line_block` tall; a content area taller than its own line-height reaches past it and
        // shows up as scrollable overflow instead (LAYOUT section 4).
        float ink_bottom = 0.f;
        for (std::size_t i = 0; i < line_n; ++i) {
            const Piece& piece = pieces_raw[i];
            if (piece.is_float || piece.abs_static || empty_inline_box(piece)) {
                continue;
            }
            const float block =
                piece.atomic ? piece.block : (piece.block > 0.f ? piece.block : font_box);
            float reach = tops[i] + block;
            // An inline-block's glyphs can stick out of its border box. That ink is what grows
            // an ancestor's scrollHeight (text-font-fallback: the row is 21px, Chrome scrolls
            // to 22). `scrollable.h` is measured from the border edge.
            if (piece.atomic && piece.box != nullptr && piece.result != nullptr &&
                style_of(*piece.box).overflow_y == OverflowMode::Visible) {
                const float scrolled = piece.result->overflow.scrollable.h;
                if (scrolled > 0.f) {
                    reach = std::max(reach, tops[i] + piece.result->geometry.margin.block_start +
                                                scrolled);
                }
            }
            ink_bottom = std::max(ink_bottom, reach);
        }
        float content_width = 0;
        float hanging_width = 0;
        int spaces = 0;
        int glyphs = 0;
        for (const Piece& piece : line) {
            if (piece.is_float) {
                continue;
            }
            content_width += piece.advance;
            if (piece.hanging) {
                hanging_width += piece.advance;
            }
            if (piece.space && !piece.hanging) {
                ++spaces;
            }
            if (!piece.atomic) {
                ++glyphs;
            }
        }
        float left_edge = 0;
        float right_edge = inline_known ? flow_inline : content_width;
        limits_at(y, std::max(line_block, 0.01f), left_edge, right_edge);
        // Hanging spaces sit in `content_width` so paint can still step over them, but the
        // measure and the free space used for justify exclude `hanging_width`.
        const float occupied = content_width - hanging_width;
        float free = inline_known ? right_edge - (line_start + occupied) : 0.f;
        if (free < 0.f) {
            free = 0.f;
        }
        const bool rtl = key.space.direction == Direction::Rtl;
        bool pack_right = false;
        bool pack_center = style.text_align == TextAlign::Center;
        if (style.text_align == TextAlign::Right || style.text_align == TextAlign::End) {
            pack_right = style.text_align == TextAlign::Right || !rtl;
        } else if (style.text_align == TextAlign::Start) {
            pack_right = rtl;
        }
        const bool justify =
            style.text_align == TextAlign::Justify && !last_line && free > 0.01f && inline_known;
        float shift = 0;
        float extra = 0;
        bool extra_on_spaces = spaces > 0;
        if (justify) {
            const int gaps = spaces > 0 ? spaces : (glyphs > 1 ? glyphs - 1 : 0);
            if (gaps > 0) {
                extra = free / static_cast<float>(gaps);
            }
            pack_center = false;
            pack_right = false;
        } else if (inline_known && pack_center) {
            shift = free * 0.5f;
        } else if (inline_known && pack_right) {
            shift = free;
        }
        float cursor = line_start + shift;
        LineBox box_line;
        box_line.inline_start = cursor;
        box_line.block_offset = y;
        box_line.baseline = ascent;
        box_line.block_size = line_block;
        box_line.font_ascent = em.ascent;
        box_line.font_descent = em.descent;
        ShapedRun run;
        int glyph_seen = 0;
        for (std::size_t i = 0; i < line.size(); ++i) {
            const Piece& piece = line[i];
            float gap = 0;
            if (justify && extra > 0.f) {
                if (extra_on_spaces && piece.space && !piece.hanging) {
                    gap = extra;
                } else if (!extra_on_spaces && !piece.atomic && !piece.is_float && !piece.hanging &&
                           glyph_seen + 1 < glyphs) {
                    gap = extra;
                }
            }
            if (piece.abs_static && piece.box != nullptr) {
                // `cursor` is the content-box position. A containing block that reset the
                // origin frame wants the padding edge added; a descendant frame already
                // includes this box's content origin.
                // The line cursor is where the box's **margin box** would have sat, and the offsets
                // place the *border* box (CSS 2.1 sections 10.3.7 and 10.6.4), so the box's own
                // used margins move it away from the cursor. `auto` resolves to 0 here - the inset
                // is auto too, and an auto inset means "use the static position", not "centre
                // inside the containing block".
                const LayoutStyle& piece_style = style_of(*piece.box);
                const AxisSize margin_basis = AxisSize{inline_known, std::max(0.f, content_inline)};
                const float margin_inline =
                    piece_style.margin_left_auto
                        ? 0.f
                        : used_edge(piece_style, LengthProperty::MarginLeft, margin_basis);
                const float margin_block =
                    piece_style.margin_top_auto
                        ? 0.f
                        : used_edge(piece_style, LengthProperty::MarginTop, margin_basis);
                // Content-edge cursor. The block path used to add padding here when the
                // origin frame was still the containing block's padding edge, and
                // `placed_static` added the same padding from the pin on the next layout.
                LogicalPoint local;
                local.inline_offset = cursor + margin_inline;
                local.block_offset = y + margin_block;
                piece.box->has_static_local = true;
                piece.box->static_local = local;
                cx.note_out_of_flow(*piece.box, local);
                continue;
            }
            if (piece.atomic && piece.box != nullptr) {
                // An atomic (or the branch below's non-atomic inline chrome) advances
                // `cursor` without contributing glyphs. Flush the open run first so
                // text after the box starts a new ShapedRun at the post-box cursor.
                // Without this, glyphs after `<code>` / inline-block keep text-only
                // offsets and paint on top of the chip (zmd welcome overlap).
                if (!run.glyphs.empty()) {
                    box_line.runs.push_back(std::move(run));
                    run = ShapedRun{};
                }
                const float top = tops[i];
                const float margin_inline =
                    piece.result != nullptr ? piece.result->geometry.margin.inline_start : 0.f;
                const float margin_block =
                    piece.result != nullptr ? piece.result->geometry.margin.block_start : 0.f;
                piece.box->placement.cb_offset.inline_offset =
                    edges.padding_inline_start + cursor + margin_inline;
                piece.box->placement.cb_offset.block_offset =
                    edges.padding_block_start + y + top + margin_block;
                piece.box->placement.sticky_offset = {};
                apply_relative(*piece.box, cb_inline, inline_known, block_percent);
                remember_place(walk, piece.box);
                if (piece.result != nullptr) {
                    note_child_ink(walk, style, *piece.box, *piece.result, border_edges(style));
                }
                cursor += piece.advance + gap;
            } else if (!piece.is_float) {
                if (empty_inline_box(piece)) {
                    // No strut, no ink and no scroll size: the box holds no content and no chrome,
                    // so Chrome reports a 0x0 box and it must not reach into an ancestor's scroll
                    // size (measured: `example-reader`'s leftover `<noscript>`). The 0x0 box still
                    // has a position, and the placement below is what gives it one - the line's
                    // start (a float shifts it: `build/float_line.html`) and its vertical-align
                    // shift (`LayoutCache.NormalFlow`).
                }
                if (piece.box != nullptr) {
                    // Same flush as the atomic branch: this piece's advance is the inline
                    // box's margin/border box (padding included). Leaving the run open made
                    // following sibling text share one origin and overlap the box.
                    if (!run.glyphs.empty()) {
                        box_line.runs.push_back(std::move(run));
                        run = ShapedRun{};
                    }
                    // An inline box in this line: `cursor` is its border box origin, and `tops[i]`
                    // is where its font content area starts, so the box is placed at the content
                    // box origin minus its own padding and border (LAYOUT section 4: `cb_offset`
                    // is the border box origin).
                    const float chrome_block = piece.result != nullptr
                                                   ? piece.result->geometry.padding.block_start +
                                                         piece.result->geometry.border.block_start
                                                   : 0.f;
                    piece.box->placement.cb_offset.inline_offset =
                        edges.padding_inline_start + cursor;
                    piece.box->placement.cb_offset.block_offset =
                        edges.padding_block_start + y + tops[i] - chrome_block;
                    piece.box->placement.sticky_offset = {};
                    apply_relative(*piece.box, cb_inline, inline_known, block_percent);
                    remember_place(walk, piece.box);
                    if (!empty_inline_box(piece) && piece.result != nullptr) {
                        note_child_ink(walk, style, *piece.box, *piece.result, border_edges(style));
                    }
                    cursor += piece.advance + gap;
                    continue;
                }
                if (empty_inline_box(piece)) {
                    continue;
                }
                GlyphInline glyph = piece.glyph;
                if (run.glyphs.empty()) {
                    run.has_origin = true;
                    run.origin = cursor;
                    run.style_box = piece.style_box;
                } else if (run.style_box != piece.style_box) {
                    box_line.runs.push_back(std::move(run));
                    run = ShapedRun{};
                    run.has_origin = true;
                    run.origin = cursor;
                    run.style_box = piece.style_box;
                }
                glyph.offset = run.advance_inline;
                glyph.block_offset = tops[i];
                // `text-align: justify` widens the gaps, and the widened advance belongs to the
                // glyph: every consumer of a run's glyph advances - paint's pens, the difftest's
                // per-character advance channel, the ellipsis fit test - reads those, not the
                // line's cursor. Keeping the extra only in the run total left justified text
                // painted un-stretched. The run total is the same either way.
                glyph.advance += gap;
                run.advance_inline += glyph.advance;
                run.glyphs.push_back(glyph);
                if (!piece.glyph.forced_break) {
                    run.text += piece.chars;
                }
                cursor += piece.advance + gap;
                ++glyph_seen;
            }
        }
        // Line box size excludes hanging glyphs; shaped runs still carry their advances for paint.
        box_line.inline_size = std::max(0.f, cursor - box_line.inline_start - hanging_width);
        box_line.source_chars = line_source_chars;
        box_line.break_rect = pending_break_rect;
        pending_break_rect = false;
        if (!run.glyphs.empty()) {
            box_line.runs.push_back(std::move(run));
        }
        lines->lines.push_back(std::move(box_line));
        // Hanging pre-wrap spaces must not inflate scrollWidth (Chrome scroll_size stays at the
        // padding box).
        const float line_end = line_start + content_width - hanging_width;
        walk.line_inline = std::max(walk.line_inline, line_end);
        walk.content_inline = std::max(walk.content_inline, line_end);
        walk.ink_block = std::max(walk.ink_block, y + ink_bottom);
        y += line_block;
        if (fragmented && walk.fragment_shortage <= 0.f &&
            own_chrome_block + y > frag_own_start + frag_limit + 0.01f) {
            // A line that had nowhere else to go (the fragment's first) overflowed the
            // fragmentainer: this is how much taller the fragmentainer would have to be.
            walk.fragment_shortage = own_chrome_block + y - (frag_own_start + frag_limit);
        }
        line.clear();
        line_width = 0;
        line_source_chars = 0;
        ++line_count;
}

}  // namespace flow_detail
}  // namespace layout
}  // namespace zui
