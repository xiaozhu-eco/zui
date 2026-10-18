#include "layout/grid/detail.h"

namespace zui {
namespace layout {
namespace grid_detail {

const LayoutStyle& style_of(const Box& box)
{
    static const LayoutStyle kEmpty;
    return box.style_ptr != nullptr ? box.style() : kEmpty;
}

float length_or_percent(float value, bool is_percent, float basis, bool basis_known)
{
    if (!is_percent) {
        return value;
    }
    return basis_known ? basis * value / 100.f : 0.f;
}

// A specified size is the content box unless box-sizing says otherwise.
float border_from_specified(float specified, bool border_box, float chrome)
{
    return border_box ? specified : specified + chrome;
}

float content_from_specified(float specified, bool border_box, float chrome)
{
    return std::max(0.f, border_from_specified(specified, border_box, chrome) - chrome);
}

Edges logical_margin(WritingMode mode, Direction direction, const LayoutStyle& style,
                     const AxisSize& inline_basis)
{
    return physical_edges_to_logical(
        mode, direction,
        style.margin_top_auto ? 0.f
                              : used_edge(style.margin_top, style.margin_top_percent, inline_basis),
        style.margin_right_auto
            ? 0.f
            : used_edge(style.margin_right, style.margin_right_percent, inline_basis),
        style.margin_bottom_auto
            ? 0.f
            : used_edge(style.margin_bottom, style.margin_bottom_percent, inline_basis),
        style.margin_left_auto
            ? 0.f
            : used_edge(style.margin_left, style.margin_left_percent, inline_basis));
}

Edges padding_edges(WritingMode mode, Direction direction, const LayoutStyle& style,
                    const AxisSize& inline_basis)
{
    return physical_edges_to_logical(
        mode, direction, used_edge(style.padding_top, style.padding_top_percent, inline_basis),
        used_edge(style.padding_right, style.padding_right_percent, inline_basis),
        used_edge(style.padding_bottom, style.padding_bottom_percent, inline_basis),
        used_edge(style.padding_left, style.padding_left_percent, inline_basis));
}

Edges border_edges(WritingMode mode, Direction direction, const LayoutStyle& style)
{
    return physical_edges_to_logical(mode, direction, style.border_top, style.border_right,
                                     style.border_bottom, style.border_left);
}

bool scrolls(const LayoutStyle& style)
{
    return style.overflow_x != OverflowMode::Visible || style.overflow_y != OverflowMode::Visible;
}

bool out_of_flow(const Box& box)
{
    const LayoutStyle& style = style_of(box);
    return style.position_absolute || style.position_fixed;
}

TrackSizing sizing_of(const TrackSize& track)
{
    TrackSizing out;
    if (track.kind == TrackKind::MinMax) {
        out.min_kind = track.min_kind;
        out.min_percent = track.min_percent;
        out.min_px = track.min_px;
        out.max_kind = track.max_kind;
        out.max_percent = track.max_percent;
        out.max_px = track.max_px;
        out.fr = track.max_fr > 0 ? track.max_fr : 1;
    } else {
        switch (track.kind) {
            case TrackKind::Length:
                out.min_kind = TrackKind::Length;
                out.max_kind = TrackKind::Length;
                out.min_percent = track.length_percent;
                out.min_px = track.length_px;
                out.max_percent = track.length_percent;
                out.max_px = track.length_px;
                break;
            case TrackKind::Fr:
                out.min_kind = TrackKind::Auto;
                out.max_kind = TrackKind::Fr;
                out.fr = track.fr > 0 ? track.fr : 1;
                break;
            case TrackKind::MinContent:
                out.min_kind = TrackKind::MinContent;
                out.max_kind = TrackKind::MinContent;
                break;
            case TrackKind::MaxContent:
                // A single `max-content` is `minmax(max-content, max-content)`, not
                // `minmax(auto, max-content)`. The minimum is what stops the column shrinking
                // to the longest word.
                out.min_kind = TrackKind::MaxContent;
                out.max_kind = TrackKind::MaxContent;
                break;
            case TrackKind::FitContent:
                // fit-content(<length>) sizes like max-content with an automatic minimum, capped
                // by its argument: min-content <= size <= max(min-content, argument).
                out.min_kind = TrackKind::Auto;
                out.max_kind = TrackKind::MaxContent;
                out.has_cap = true;
                out.cap_percent = track.length_percent;
                out.cap_px = track.length_px;
                break;
            case TrackKind::Auto:
                out.min_kind = TrackKind::Auto;
                out.max_kind = TrackKind::Auto;
                break;
            case TrackKind::MinMax:
            default:
                out.min_kind = TrackKind::Auto;
                out.max_kind = TrackKind::MaxContent;
                break;
        }
    }
    // fr is not a valid min sizing function.
    if (out.min_kind == TrackKind::Fr) {
        out.min_kind = TrackKind::Auto;
    }
    return out;
}

Track make_track(const TrackSizing& sizing, float basis, bool basis_known)
{
    Track track;
    const bool min_length =
        sizing.min_kind == TrackKind::Length && (!sizing.min_percent || basis_known);
    if (min_length) {
        track.base =
            std::max(0.f, length_or_percent(sizing.min_px, sizing.min_percent, basis, basis_known));
    } else {
        // auto and min-content both start at zero and follow the contribution. auto is the
        // automatic minimum size, which is the min-content contribution of a block-level item.
        track.min_intrinsic = true;
    }
    if (sizing.max_kind == TrackKind::Fr) {
        track.is_fr = true;
        track.fr = sizing.fr > 0 ? sizing.fr : 1;
        track.limit = kInf;
        if (!min_length) {
            track.min_intrinsic = true;
        }
    } else if (sizing.max_kind == TrackKind::Length && (!sizing.max_percent || basis_known)) {
        track.limit =
            std::max(0.f, length_or_percent(sizing.max_px, sizing.max_percent, basis, basis_known));
    } else {
        // CSS Grid §11.4: an intrinsic max track sizing function starts at an infinite
        // growth limit. Flooring it to the base size here made minmax(40px, max-content)
        // subtract that 40px from the max-content contribution and keep the remainder.
        track.max_intrinsic = true;
        track.max_from_min_content = sizing.max_kind == TrackKind::MinContent;
        track.auto_max = sizing.max_kind == TrackKind::Auto;
        track.limit = kInf;
    }
    if (sizing.has_cap) {
        track.limit_cap = std::max(0.f, length_or_percent(sizing.cap_px, sizing.cap_percent, basis,
                                                           basis_known));
    }
    if (std::isfinite(track.limit) && track.limit < track.base) {
        track.limit = track.base;
    }
    return track;
}

// ---- Track contributions (CSS Grid section 11.5) ------------------------------------------

// How much of a span the tracks already cover: their current base sizes (or growth limits) plus
// the gaps between them. An item's contribution only moves the tracks that can still grow, and
// only by what the span is still short of. Taking the whole span, not just the fixed tracks, is
// what makes two items in one track satisfy the larger contribution instead of adding up.
float used_extent(const std::vector<Track>& tracks, int start, int span, float gap,
                  bool limit_side)
{
    float total = 0;
    for (int i = start; i < start + span && i < static_cast<int>(tracks.size()); ++i) {
        if (i > start) {
            total += gap;
        }
        const Track& track = tracks[static_cast<std::size_t>(i)];
        if (limit_side && std::isfinite(track.limit)) {
            total += track.limit;
        } else {
            total += track.base;
        }
    }
    return total;
}

int growable_count(const std::vector<Track>& tracks, int start, int span, bool limit_side)
{
    int count = 0;
    for (int i = start; i < start + span && i < static_cast<int>(tracks.size()); ++i) {
        const Track& track = tracks[static_cast<std::size_t>(i)];
        if (limit_side ? track.max_intrinsic : track.min_intrinsic) {
            ++count;
        }
    }
    return count;
}

// Section 11.5.1: the growth limits take the max-content contributions, after the tracks that
// already have a definite size and the gaps have been paid for.
void contribute_limits(std::vector<Track>& tracks, int start, int span, float contribution,
                       float gap)
{
    const int growable = growable_count(tracks, start, span, true);
    if (growable <= 0) {
        return;
    }
    const float share =
        std::max(0.f, contribution - used_extent(tracks, start, span, gap, true)) /
        static_cast<float>(growable);
    for (int i = start; i < start + span && i < static_cast<int>(tracks.size()); ++i) {
        Track& track = tracks[static_cast<std::size_t>(i)];
        if (!track.max_intrinsic) {
            continue;
        }
        // §11.5.1: an infinite growth limit becomes the base size plus the planned
        // increase. A finite one grows by that increase, so a later larger item
        // still reaches its contribution.
        if (!std::isfinite(track.limit)) {
            track.limit = track.base + share;
        } else {
            track.limit += share;
        }
    }
}

// Section 11.5.1: the base sizes take the min-content contributions. The extra space is shared in
// proportion to how much each track can still grow, so a track that has reached its growth limit
// stops taking space and the remaining tracks take the rest.
void contribute_bases(std::vector<Track>& tracks, int start, int span, float contribution,
                      float gap)
{
    float extra = contribution - used_extent(tracks, start, span, gap, false);
    if (extra <= 0.25f) {
        return;
    }
    std::vector<int> growable;
    for (int i = start; i < start + span && i < static_cast<int>(tracks.size()); ++i) {
        if (tracks[static_cast<std::size_t>(i)].min_intrinsic) {
            growable.push_back(i);
        }
    }
    if (growable.empty()) {
        return;
    }
    for (int round = 0; round < 4 && extra > 0.25f; ++round) {
        float finite_potential = 0;
        int finite_count = 0;
        for (int i : growable) {
            const Track& track = tracks[static_cast<std::size_t>(i)];
            if (std::isfinite(track.limit)) {
                finite_potential += std::max(0.f, track.limit - track.base);
                ++finite_count;
            }
        }
        if (finite_count == 0) {
            // Every growable track is unbounded; share the rest evenly.
            const float share = extra / static_cast<float>(growable.size());
            for (int i : growable) {
                tracks[static_cast<std::size_t>(i)].base += share;
            }
            return;
        }
        if (finite_potential <= 0.f) {
            // Every growable track is already at its growth limit. The base sizes still have to
            // reach the contribution, and step 4 below raises the limits to match.
            const float share = extra / static_cast<float>(growable.size());
            for (int i : growable) {
                tracks[static_cast<std::size_t>(i)].base += share;
            }
            return;
        }
        float given = 0;
        for (int i : growable) {
            Track& track = tracks[static_cast<std::size_t>(i)];
            if (!std::isfinite(track.limit)) {
                continue;
            }
            const float room = std::max(0.f, track.limit - track.base);
            if (room <= 0.f) {
                continue;
            }
            const float add = std::min(extra * room / finite_potential, room);
            track.base += add;
            given += add;
        }
        if (given <= 0.25f) {
            return;
        }
        extra -= given;
    }
}

// ---- Free space and distribution ----------------------------------------------------------

// Section 11.7: the free space goes to the fr tracks. A track whose contribution is larger than
// the share it would get freezes at that size and the rest of the fr tracks share what is left.
// With no definite available size the fr tracks keep their max-content contribution, which is what
// a shrink-to-fit container needs.
void solve_fr(std::vector<Track>& tracks, bool available_known, float available, float gap)
{
    const int count = static_cast<int>(tracks.size());
    std::vector<int> fr_indices;
    float non_fr = 0;
    float total_fr = 0;
    for (int i = 0; i < count; ++i) {
        if (i > 0) {
            non_fr += gap;
        }
        const Track& track = tracks[static_cast<std::size_t>(i)];
        if (track.is_fr) {
            fr_indices.push_back(i);
            total_fr += track.fr;
        } else {
            non_fr += track.base;
        }
    }
    if (fr_indices.empty() || total_fr <= 0) {
        return;
    }
    if (!available_known) {
        for (int i : fr_indices) {
            Track& track = tracks[static_cast<std::size_t>(i)];
            const float intrinsic = std::isfinite(track.limit) ? track.limit : track.base;
            track.base = std::max(track.base, intrinsic);
        }
        return;
    }
    std::vector<bool> frozen(tracks.size(), false);
    for (int round = 0; round < 8; ++round) {
        float fr_sum = 0;
        float frozen_space = 0;
        for (int i : fr_indices) {
            const Track& track = tracks[static_cast<std::size_t>(i)];
            if (frozen[static_cast<std::size_t>(i)]) {
                frozen_space += track.base;
            } else {
                fr_sum += track.fr;
            }
        }
        if (fr_sum <= 0) {
            break;
        }
        const float free = std::max(0.f, available - non_fr - frozen_space);
        // Section 11.7.1: the sum of the *not yet frozen* flex factors is clamped to 1 before it is
        // used as the divisor, so `0.5fr` can only take half of the free space. The clamp is applied
        // on every restart, not once up front: a frozen track changes the remaining sum.
        const float unit = free / std::max(fr_sum, 1.f);
        bool froze = false;
        for (int i : fr_indices) {
            if (frozen[static_cast<std::size_t>(i)]) {
                continue;
            }
            const Track& track = tracks[static_cast<std::size_t>(i)];
            if (track.base > unit * track.fr + 0.5f) {
                frozen[static_cast<std::size_t>(i)] = true;
                froze = true;
            }
        }
        if (!froze) {
            for (int i : fr_indices) {
                if (frozen[static_cast<std::size_t>(i)]) {
                    continue;
                }
                Track& track = tracks[static_cast<std::size_t>(i)];
                track.base = std::max(track.base, unit * track.fr);
            }
            return;
        }
    }
}

Pack pack_of(FlexPack pack, float free, int count)
{
    Pack out;
    if (count <= 0) {
        return out;
    }
    switch (pack) {
        case FlexPack::End:
            out.leading = free;
            break;
        case FlexPack::Center:
            out.leading = free * 0.5f;
            break;
        case FlexPack::SpaceBetween:
            out.between = count > 1 ? free / static_cast<float>(count - 1) : 0.f;
            break;
        case FlexPack::SpaceAround: {
            const float unit = free / (2.f * static_cast<float>(count));
            out.leading = unit;
            out.between = 2.f * unit;
            break;
        }
        case FlexPack::SpaceEvenly: {
            const float unit = free / static_cast<float>(count + 1);
            out.leading = unit;
            out.between = unit;
            break;
        }
        case FlexPack::Stretch:
        case FlexPack::Start:
        default:
            break;
    }
    return out;
}

// Section 12.8: at stretch (which is what `normal` means here) the leftover space is shared by the
// auto-sized tracks. Without an auto-sized track the leftover stays at the end.
void stretch_auto_tracks(std::vector<Track>& tracks, bool available_known, float available,
                         float gap, FlexPack pack)
{
    if (!available_known || pack != FlexPack::Stretch) {
        return;
    }
    int auto_tracks = 0;
    float used = 0;
    for (std::size_t i = 0; i < tracks.size(); ++i) {
        if (i > 0) {
            used += gap;
        }
        used += tracks[i].base;
        if (tracks[i].auto_max) {
            ++auto_tracks;
        }
    }
    const float free = available - used;
    if (auto_tracks == 0 || free <= 0.f) {
        return;
    }
    const float share = free / static_cast<float>(auto_tracks);
    for (Track& track : tracks) {
        if (track.auto_max) {
            track.base += share;
        }
    }
}

float sum_tracks(const std::vector<Track>& tracks, float gap)
{
    float total = 0;
    for (std::size_t i = 0; i < tracks.size(); ++i) {
        if (i > 0) {
            total += gap;
        }
        total += tracks[i].base;
    }
    return total;
}

std::vector<float> track_offsets(const std::vector<Track>& tracks, float gap, const Pack& pack)
{
    std::vector<float> offsets(tracks.size(), 0.f);
    float cursor = pack.leading;
    for (std::size_t i = 0; i < tracks.size(); ++i) {
        offsets[i] = cursor;
        cursor += tracks[i].base;
        if (i + 1 < tracks.size()) {
            cursor += gap + pack.between;
        }
    }
    return offsets;
}

float area_extent(const std::vector<Track>& tracks, int start, int span, float gap)
{
    float total = 0;
    for (int i = start; i < start + span && i < static_cast<int>(tracks.size()); ++i) {
        if (i > start) {
            total += gap;
        }
        total += tracks[static_cast<std::size_t>(i)].base;
    }
    return total;
}

}  // namespace grid_detail
}  // namespace layout
}  // namespace zui
