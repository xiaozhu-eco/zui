#include "layout/flow/detail.h"
#include "layout/baseline.h"
#include "trace/trace.h"

namespace zui {
namespace layout {
namespace flow_detail {

void run_line_wrap(Box& box, const LayoutKey& key, LayoutContext& cx, const UsedEdges& edges,
                   float content_inline, bool inline_known, float& flow_inline, bool bfc,
                   const LogicalPoint& box_origin,
                   std::shared_ptr<const ExclusionSpace>& exclusion, std::vector<Piece>& pieces,
                   Box* outside_marker, const LayoutResult* outside_marker_result, Walk& walk,
                   std::unique_ptr<InlineContent>& lines, float& y, std::vector<Piece>& line,
                   float& line_width, float& line_start, std::size_t& line_source_chars,
                   bool& pending_break_rect, bool collapse, bool can_wrap, float indent,
                   const FragmentationContext& frag, bool fragmented, float frag_limit,
                   float frag_own_start, float own_chrome_block, int resume_line,
                   const LayoutStyle& style, const LayoutContext::FontEm& em,
                   const AxisSize& block_percent) {
    const float cb_inline = inline_known ? content_inline : 0.f;
    int line_count = 0;
    auto limits_at = [&](float at_y, float height, float& left, float& right) {
        if (!inline_known) {
            left = 0;
            right = 1.0e6f;
            return;
        }
        float_limits(*exclusion, at_y, at_y + std::max(height, em.ascent + em.descent), flow_inline,
                     left, right);
    };
    auto finish = [&](bool last_line = false, bool forced_empty = false) {
        finish_wrapped_line(box, edges, content_inline, inline_known, cb_inline, walk, lines, y,
                            line, line_width, line_start, line_source_chars, pending_break_rect,
                            collapse, style, em, block_percent, fragmented, frag_limit,
                            frag_own_start, own_chrome_block, line_count, last_line, forced_empty,
                            cx, key, flow_inline, resume_line, frag, exclusion);
    };
    auto open_line = [&](float at_y) {
        float left = 0;
        float right = 0;
        limits_at(at_y, std::max(used_line_height(style, em), 0.01f), left, right);
        line_start = left;
        if (line_count == 0) {
            line_start += indent;
        }
        line_width = 0;
        (void)right;
    };
    // True after a forced break opens a line, until a real piece is pushed onto it. The final
    // finish then keeps the empty line that follows a trailing newline.
    bool after_break = false;
    auto place_line_float = [&](const Piece& piece) {
        if (piece.result == nullptr || piece.box == nullptr) {
            return;
        }
        const float clear_y = cleared_block(*exclusion, piece.clear_side);
        FloatRecord record = place_float(*piece.result, piece.float_side, std::max(y, clear_y),
                                         flow_inline, *exclusion, false);
        {
            const std::string* shape =
                piece.box->node != nullptr ? piece.box->node->style("shape-outside") : nullptr;
            const std::string* margin =
                piece.box->node != nullptr ? piece.box->node->style("shape-margin") : nullptr;
            attach_shape_outside(record, shape != nullptr ? *shape : std::string_view{},
                                 margin != nullptr ? *margin : std::string_view{});
        }
        piece.box->placement.cb_offset.inline_offset = edges.padding_inline_start +
                                                       record.margin_box.x +
                                                       piece.result->geometry.margin.inline_start;
        piece.box->placement.cb_offset.block_offset = edges.padding_block_start +
                                                      record.margin_box.y +
                                                      piece.result->geometry.margin.block_start;
        piece.box->placement.sticky_offset = {};
        apply_relative(*piece.box, cb_inline, inline_known, block_percent);
        remember_place(walk, piece.box);
        exclusion = ExclusionSpace::append(exclusion, record);
        walk.placed.push_back(record);
        if (!bfc) {
            FloatRecord stored = record;
            stored.margin_box.x += box_origin.inline_offset;
            stored.margin_box.y += box_origin.block_offset;
            walk.floats.push_back(stored);
        }
        walk.has_float = true;
        walk.float_bottom = std::max(walk.float_bottom, record.margin_box.y + record.margin_box.h);
        if (record.margin_box.y <= y + em.ascent && !line.empty()) {
            float left = 0;
            float right = flow_inline;
            // Line-box height (not the font box alone) so shape-outside chords match the strut.
            limits_at(y, used_line_height(style, em), left, right);
            line_start = left;
            std::vector<Piece> overflow_pieces;
            while (!line.empty() && line_start + line_width > right + 0.01f) {
                overflow_pieces.push_back(line.back());
                line_width -= line.back().advance;
                line.pop_back();
            }
            if (!overflow_pieces.empty()) {
                finish();
                open_line(y);
                for (auto piece_it = overflow_pieces.rbegin(); piece_it != overflow_pieces.rend();
                     ++piece_it) {
                    line.push_back(*piece_it);
                    after_break = false;
                    line_width += piece_it->advance;
                }
            }
        } else if (line.empty()) {
            open_line(y);
        }
    };
    zui::trace::Zone wrap_zone("lines.wrap", zui::trace::Category::LayoutPhase);
    // `text-wrap: balance` retries this wrap at narrower widths. Pieces stay as built:
    // asking the text child again at each width is a new layout key, and the third one
    // trips the measure fuse. The box width does not change, only where lines break.
    const float full_inline = flow_inline;
    const std::shared_ptr<const ExclusionSpace> exclusion_before_wrap = exclusion;
    bool balance_search = false;
    bool balance_done = false;
    float balance_lo = 0.f;
    float balance_hi = full_inline;
    int balance_target = 0;
    int balance_steps = 0;
wrap_again:
    after_break = false;
    if (balance_search) {
        lines = std::make_unique<InlineContent>();
        y = 0;
        line.clear();
        line_width = 0;
        line_start = 0;
        line_source_chars = 0;
        pending_break_rect = false;
        line_count = 0;
        exclusion = exclusion_before_wrap;
        walk.line_inline = 0;
        walk.content_inline = 0;
        walk.content_block = 0;
        walk.ink_block = 0;
        walk.ink = Rect{};
        walk.float_bottom = 0;
        walk.has_float = false;
        walk.floats.clear();
        walk.placed.clear();
        walk.places.clear();
        walk.fragment_full = false;
        walk.fragment_line = -1;
        walk.fragment_block = 0;
        walk.fragment_shortage = 0;
        walk.all_through = true;
        walk.baselines = BaselineSet{};
    }
    open_line(0);
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        if (walk.fragment_full) {
            break;
        }
        const Piece& piece = pieces[i];
        if (piece.is_float) {
            line_source_chars += piece.source_chars;
            place_line_float(piece);
            continue;
        }
        if (piece.glyph.forced_break) {
            // The br's own box is the strut at the end of this line. It is not a glyph,
            // so it does not take advance and it is not one of the parent's text offsets.
            // An empty line (consecutive breaks, or a break before any glyph) still emits a
            // font strut so the caret has a place to land.
            const bool on_empty = line.empty();
            if (piece.box != nullptr) {
                piece.box->placement.cb_offset.inline_offset =
                    edges.padding_inline_start + line_start + line_width;
                piece.box->placement.cb_offset.block_offset = edges.padding_block_start + y;
                piece.box->placement.sticky_offset = {};
                apply_relative(*piece.box, cb_inline, inline_known, block_percent);
                remember_place(walk, piece.box);
            }
            line_source_chars += piece.source_chars;
            pending_break_rect = true;
            finish(false, on_empty);
            if (walk.fragment_full) {
                break;
            }
            open_line(y);
            after_break = true;
            continue;
        }
        float left = 0;
        float right = inline_known ? flow_inline : 1.0e6f;
        float room = 0;
        // The limits belong to the line the piece lands on: a float's band ends partway down a
        // column, so the line that starts below it is not shortened, and the space a wrap just
        // opened has to be measured at the new line's own block position. Recomputing after a
        // break keeps both - before this, a wrapped line inherited the limits of the line it
        // wrapped out of, and the first word of every line after a float was placed as if the
        // float were still beside it.
        auto refresh_limits = [&]() {
            left = 0;
            right = inline_known ? flow_inline : 1.0e6f;
            if (inline_known) {
                // CSS Shapes samples the float area against the line box; use the strut height so a
                // `line-height` longer than the font box matches Chrome's chord (not em ascent alone).
                limits_at(y,
                          std::max(used_line_height(style, em),
                                   std::max(em.ascent + em.descent, piece.block)),
                          left, right);
            }
            room = right - (line_start + line_width);
        };
        refresh_limits();
        // CSS 2.1 section 9.5: a line box that cannot hold its first unbreakable run is moved down
        // to the next position the floats leave free, instead of overflowing the gap they left.
        // Chrome does the same - a float on each side of a narrow container starts the text below
        // them rather than beside them - and the run is the word the wrap test below measures, not
        // a single glyph: a word that fits next to the float keeps its line there. A container
        // narrower than the run itself keeps it too (moving would make no progress), which is why
        // the loop stops as soon as the next boundary does not advance.
        if (can_wrap && inline_known && line_width <= 0.01f && !piece.is_float &&
            !piece.abs_static && !piece.glyph.forced_break) {
            float first_run = 0.f;
            for (std::size_t k = i; k < pieces.size(); ++k) {
                const Piece& next = pieces[k];
                if (next.is_float || next.glyph.forced_break) {
                    break;
                }
                const bool last_of_run = next.glyph.wrap_after;
                const bool hang_trailing_space =
                    style.white_space != WhiteSpace::BreakSpaces && last_of_run && next.space;
                if (!hang_trailing_space) {
                    first_run += next.advance;
                }
                if (last_of_run) {
                    break;
                }
            }
            const float line_height = std::max(used_line_height(style, em), 0.01f);
            for (int move = 0; move < 16 && first_run > room + 0.01f; ++move) {
                float edge = y;
                for (const FloatRecord& band : exclusion->bands()) {
                    const float top = band.margin_box.y;
                    const float bottom = top + band.margin_box.h;
                    if (bottom <= y + 0.01f || top >= y + line_height) {
                        continue;
                    }
                    edge = std::max(edge, bottom);
                }
                if (edge <= y + 0.01f) {
                    break;
                }
                y = edge;
                open_line(y);
                refresh_limits();
            }
        }
        // An absolute inline is not in the line for white-space collapsing. A marker left on
        // the line used to keep the space that follows it, which shifted the chips after the
        // theme radios by one space.
        // The last piece the line already holds, for white-space collapsing and for the break
        // decision. It is a pointer *into* `line`, so it has to be asked again after anything that
        // can clear or reflow the line: `finish()` clears it and `open_line()` can drop pieces back
        // through `place_line_float`. Reading a stale one runs past the vector's size into capacity
        // it no longer owns (ASan reports it as a container-overflow).
        auto inflow_piece = [&]() -> const Piece* {
            for (int n = static_cast<int>(line.size()) - 1; n >= 0; --n) {
                if (!line[static_cast<std::size_t>(n)].abs_static) {
                    return &line[static_cast<std::size_t>(n)];
                }
            }
            return nullptr;
        };
        const Piece* inflow = inflow_piece();
        const bool skip_space = collapse && piece.space && (inflow == nullptr || inflow->space);
        if (skip_space) {
            line_source_chars += piece.source_chars;
            continue;
        }
        // A break opportunity sits *before* a word, so the decision has to be taken when the word
        // starts, not when its first glyph stops fitting: checking only the incoming glyph let a
        // word whose first character happened to fit be force-fitted into the line. Measured:
        // "Justified text that has to fill the measure" (299.6px) was laid into a 260px box.
        if (can_wrap && inflow != nullptr && inflow->glyph.wrap_after && !piece.space &&
            !piece.abs_static && !piece.glyph.forced_break) {
            float word = 0.f;
            for (std::size_t k = i; k < pieces.size(); ++k) {
                const Piece& next = pieces[k];
                if (next.is_float || next.glyph.forced_break) {
                    break;
                }
                const bool last_of_word = next.glyph.wrap_after;
                // pre-wrap hangs a trailing space at a soft wrap so it does not count; break-spaces
                // keeps every preserved space in the measure (CSS Text 3 §3).
                const bool hang_trailing_space =
                    style.white_space != WhiteSpace::BreakSpaces && last_of_word && next.space;
                if (!hang_trailing_space) {
                    word += next.advance;
                }
                if (last_of_word) {
                    break;
                }
            }
            if (line_start + line_width + word > right + 0.01f) {
                finish();
                if (walk.fragment_full) {
                    break;
                }
                open_line(y);
                refresh_limits();
            }
        }
        // A break above finished the old line, so `inflow` names a piece that is no longer in
        // `line` - or nothing at all, which is the right answer for a piece that opens a line.
        inflow = inflow_piece();
        const bool break_here =
            piece.glyph.wrap_after || (inflow != nullptr && inflow->glyph.wrap_after);
        if (can_wrap && inflow != nullptr && !piece.abs_static && piece.advance > room + 0.01f &&
            break_here) {
            // A collapsible space that does not fit hangs on the line it overflowed.
            // The next line starts at the following character. Counting the space on
            // the new line shifts every soft wrap one codepoint early.
            if (collapse && piece.space) {
                // The space does not fit, but it still belongs to this line: the browser
                // reports it as a zero-advance hung glyph, and the next line starts at the
                // following character. Dropping the glyph kept the break index and lost
                // the advance (`text-wrap: balance` picks a width where this space is the
                // piece that overflows).
                line_source_chars += piece.source_chars;
                if (line.empty()) {
                    line_start = left;
                    if (line_count == 0) {
                        line_start += indent;
                    }
                }
                line.push_back(piece);
                after_break = false;
                finish();
                if (walk.fragment_full) {
                    break;
                }
                open_line(y);
                refresh_limits();
                continue;
            }
            // pre-wrap: preserved spaces hang (overflow this line). break-spaces: wrap them.
            if (style.white_space == WhiteSpace::PreWrap && piece.space) {
                line_source_chars += piece.source_chars;
                if (line.empty()) {
                    line_start = left;
                    if (line_count == 0) {
                        line_start += indent;
                    }
                }
                Piece hung = piece;
                hung.hanging = true;
                line.push_back(hung);
                after_break = false;
                line_width += piece.advance;
                continue;
            }
            finish();
            if (walk.fragment_full) {
                break;
            }
            open_line(y);
            refresh_limits();
        }
        // A break is allowed before an atomic inline that does not fit (CSS 2.1 section 9.4.2).
        // Without it the Close button stays on the line that already holds the end of the sentence.
        // A single-line inline box (the zmd `<code>` chip) is one piece, not a glyph, so the
        // word wrap above never sees it. If it does not fit, it moves to the next line instead
        // of hanging off the end of this one.
        if (can_wrap && !line.empty() && piece.box != nullptr && !piece.abs_static &&
            !piece.is_float && !piece.glyph.forced_break && piece.chars.empty() &&
            piece.advance > room + 0.01f) {
            finish();
            if (walk.fragment_full) {
                break;
            }
            open_line(y);
            refresh_limits();
        }
        if (can_wrap && !line.empty() && piece.atomic && !piece.abs_static && !piece.is_float &&
            !piece.glyph.forced_break && piece.advance > room + 0.01f) {
            finish();
            if (walk.fragment_full) {
                break;
            }
            open_line(y);
            refresh_limits();
        }
        line_source_chars += piece.source_chars;
        if (line.empty()) {
            line_start = left;
            if (line_count == 0) {
                line_start += indent;
            }
        }
        line.push_back(piece);
        after_break = false;
        line_width += piece.advance;
    }
    finish(true, after_break && line.empty());
    if (lines->lines.empty()) {
        walk.all_through = true;
    } else {
        walk.all_through = false;
        // A continuation fragment's content starts where the previous one stopped, so what the
        // fragment covers is only the part of `y` below that point.
        walk.content_block =
            fragmented ? std::max(0.f, y - std::max(0.f, frag_own_start - own_chrome_block)) : y;
        const LineBox& first = lines->lines.front();
        const LineBox& last = lines->lines.back();
        walk.baselines.first = border_edges(style).block_start + edges.padding_block_start +
                               first.block_offset + first.baseline;
        walk.baselines.last = border_edges(style).block_start + edges.padding_block_start +
                              last.block_offset + last.baseline;
    }
    if (outside_marker != nullptr) {
        float marker_block = 0.f;
        if (lines != nullptr && !lines->lines.empty()) {
            marker_block = lines->lines.front().block_offset;
        }
        place_outside_marker(*outside_marker, outside_marker_result, edges, marker_block, walk);
    }
    walk.lines = std::move(lines);
    if (establishes_bfc(box, style)) {
        walk.content_block = std::max(walk.content_block, walk.float_bottom);
        walk.floats.clear();
    }
    // CSS Text 4 `text-wrap: balance`: bisect the wrap width and keep the narrowest one
    // whose greedy line count does not grow. Chromium skips paragraphs longer than six
    // lines. Fragmented lines stay on the column walk. The retry reuses `pieces` so the
    // text child is not asked again (a third distinct key trips the measure fuse).
    if (!balance_done && inline_known && !fragmented && can_wrap) {
        const int produced =
            walk.lines == nullptr ? 0 : static_cast<int>(walk.lines->lines.size());
        if (!balance_search) {
            if (produced >= 2 && produced <= 6 && wants_text_wrap_balance(box)) {
                balance_lo = longest_unbreakable(walk.lines.get());
                if (balance_lo < 1.f) {
                    balance_lo = 1.f;
                }
                balance_hi = full_inline;
                if (balance_lo + 0.25f < balance_hi) {
                    balance_target = produced;
                    balance_search = true;
                    balance_steps = 1;
                    flow_inline = (balance_lo + balance_hi) * 0.5f;
                    goto wrap_again;
                }
            }
        } else {
            if (produced > 0 && produced <= balance_target && !walk.fragment_full) {
                balance_hi = flow_inline;
            } else {
                balance_lo = flow_inline;
            }
            if (balance_steps < 16 && balance_hi - balance_lo > 0.25f) {
                ++balance_steps;
                flow_inline = (balance_lo + balance_hi) * 0.5f;
                goto wrap_again;
            }
            if (flow_inline - balance_hi > 0.01f || balance_hi - flow_inline > 0.01f) {
                flow_inline = balance_hi;
                balance_done = true;
                goto wrap_again;
            }
        }
    }
}

}  // namespace flow_detail
}  // namespace layout
}  // namespace zui
