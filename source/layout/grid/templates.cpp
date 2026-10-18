#include "layout/grid/detail.h"

namespace zui {
namespace layout {
namespace grid_detail {

TemplateExpansion expand_auto_repeat(const std::vector<TrackSize>& template_tracks,
                                     const std::vector<std::vector<std::string>>& template_names,
                                     float available, float gap, bool known)
{
    TemplateExpansion out;
    out.tracks = template_tracks;
    out.line_names = template_names;
    out.line_names.resize(out.tracks.size() + 1);
    std::size_t marker = template_tracks.size();
    for (std::size_t i = 0; i < template_tracks.size(); ++i) {
        if (template_tracks[i].kind == TrackKind::AutoFill ||
            template_tracks[i].kind == TrackKind::AutoFit) {
            marker = i;
            break;
        }
    }
    if (marker == template_tracks.size()) {
        return out;
    }
    const TrackSize& spec = template_tracks[marker];
    const std::vector<TrackSize>& repeated = spec.repeat;
    if (repeated.empty()) {
        return out;
    }
    // The size the repetition count uses is each track's *minimum*: a fixed length, or the minimum
    // side of a `minmax()` when that side is fixed. Intrinsic and `fr` tracks contribute nothing,
    // which is the spec's "indefinite size -> one repetition" (CSS Grid section 7.2.3.1).
    auto fixed_size = [&](const std::vector<TrackSize>& tracks) {
        float total = 0.f;
        for (const TrackSize& track : tracks) {
            if (track.kind == TrackKind::Length && !track.length_percent) {
                total += track.length_px;
            } else if (track.kind == TrackKind::MinMax && track.min_kind == TrackKind::Length &&
                       !track.min_percent) {
                total += track.min_px;
            }
        }
        return total;
    };
    // The tracks outside the repetition are every other track of the template; the marker itself
    // contributes no size. The gutters between all of the tracks count towards the total, so the
    // repetition count is the largest one whose `outside + n * repeated + gutters` still fits the
    // container (CSS Grid section 7.2.3.1).
    const float outside = fixed_size(template_tracks);
    const float one = fixed_size(repeated);
    const int outside_count = static_cast<int>(template_tracks.size()) - 1;
    const float per_repetition = one + gap * static_cast<float>(repeated.size());
    int repetitions = 1;
    if (known && per_repetition > 0.01f) {
        const float room = available - outside + gap * static_cast<float>(1 - outside_count);
        repetitions = static_cast<int>(std::floor(room / per_repetition));
        repetitions = std::max(1, repetitions);
    }
    out.repeat_begin = static_cast<int>(marker);
    out.repeat_tracks = static_cast<int>(repeated.size());
    out.repetitions = repetitions;
    out.auto_fit = spec.kind == TrackKind::AutoFit;
    std::vector<TrackSize> expanded;
    expanded.insert(expanded.end(), template_tracks.begin(), template_tracks.begin() + marker);
    // The line names repeat with the tracks, and a repetition's closing line is the next one's
    // opening line, so the two groups merge there (the same rule the fixed-count repeat uses when
    // the style is interned).
    const std::size_t name_lines = repeated.size() + 1;
    std::vector<std::vector<std::string>> names;
    if (marker < template_names.size()) {
        names.insert(names.end(), template_names.begin(), template_names.begin() + marker);
    }
    if (names.size() < name_lines) {
        names.resize(name_lines);
    }
    const auto repeated_line = [&](std::size_t line) {
        return line < spec.repeat_names.size() ? spec.repeat_names[line] : std::vector<std::string>{};
    };
    for (int k = 0; k < repetitions; ++k) {
        for (std::size_t t = 0; t < repeated.size(); ++t) {
            expanded.push_back(repeated[t]);
            const std::vector<std::string> opening = repeated_line(t);
            names.back().insert(names.back().end(), opening.begin(), opening.end());
            names.push_back(repeated_line(t + 1));
        }
    }
    expanded.insert(expanded.end(), template_tracks.begin() + marker + 1, template_tracks.end());
    // The line that follows the repetition in the source is the repetition's own closing line, so
    // its names merge into the last line; the lines after that carry over as they are.
    if (marker + 1 < template_names.size() && !names.empty()) {
        names.back().insert(names.back().end(), template_names[marker + 1].begin(),
                            template_names[marker + 1].end());
    }
    for (std::size_t i = marker + 2; i < template_names.size(); ++i) {
        names.push_back(template_names[i]);
    }
    out.tracks = std::move(expanded);
    out.line_names = std::move(names);
    out.line_names.resize(out.tracks.size() + 1);
    return out;
}

// The `grid-auto-*` size for an implicit track. The list repeats, and a track before the explicit
// grid walks it backwards, so the last entry is the one next to the explicit grid
// (CSS Grid section 7.5). `offset` is the distance from the explicit grid: -1 for the track just
// before it, 0 for the first one after it.
const TrackSize* auto_track_for(const std::vector<TrackSize>& list, int offset)
{
    if (list.empty()) {
        return nullptr;
    }
    const int n = static_cast<int>(list.size());
    int index = offset % n;
    if (index < 0) {
        index += n;
    }
    return &list[static_cast<std::size_t>(index)];
}

std::vector<Track> make_columns(const LayoutStyle& style, const std::vector<TrackSize>& template_tracks,
                                int count, float basis, bool known, int origin)
{
    std::vector<Track> columns;
    columns.reserve(static_cast<std::size_t>(count));
    const int template_count = static_cast<int>(template_tracks.size());
    for (int i = 0; i < count; ++i) {
        const int template_index = i - origin;
        if (template_index >= 0 && template_index < template_count) {
            columns.push_back(make_track(
                sizing_of(template_tracks[static_cast<std::size_t>(template_index)]), basis, known));
        } else if (const TrackSize* auto_track = auto_track_for(
                       style.grid_auto_columns,
                       template_index < 0 ? template_index + 1 : template_index - template_count)) {
            columns.push_back(make_track(sizing_of(*auto_track), basis, known));
        } else {
            // An implicit track is auto-sized.
            Track track;
            track.min_intrinsic = true;
            track.max_intrinsic = true;
            track.auto_max = true;
            columns.push_back(track);
        }
    }
    return columns;
}

std::vector<Track> make_rows(const LayoutStyle& style, const std::vector<TrackSize>& template_tracks,
                             int count, float basis, bool known, int origin)
{
    std::vector<Track> rows;
    rows.reserve(static_cast<std::size_t>(count));
    const int template_count = static_cast<int>(template_tracks.size());
    for (int i = 0; i < count; ++i) {
        const int template_index = i - origin;
        if (template_index >= 0 && template_index < template_count) {
            rows.push_back(make_track(
                sizing_of(template_tracks[static_cast<std::size_t>(template_index)]), basis, known));
        } else if (const TrackSize* auto_track = auto_track_for(
                       style.grid_auto_rows,
                       template_index < 0 ? template_index + 1 : template_index - template_count)) {
            rows.push_back(make_track(sizing_of(*auto_track), basis, known));
        } else {
            Track track;
            track.min_intrinsic = true;
            track.max_intrinsic = true;
            track.auto_max = true;
            rows.push_back(track);
        }
    }
    return rows;
}

// Solving restarts from the sizing functions every time, so running it twice in one compute (once
// inside measure for the area round, once in solve) gives the same answer instead of folding the
// contributions in twice.
// A `min-content` maximum takes the min-content contribution. Using the max-content
// contribution here made `grid-template-columns: max-content min-content` give both tracks the
// full phrase, so "hello world" never wrapped in the second column.
float limit_contribution(const std::vector<Track>& tracks, int start, int span, float min_size,
                         float max_size)
{
    bool saw_min = false;
    bool saw_max = false;
    for (int i = start; i < start + span && i < static_cast<int>(tracks.size()); ++i) {
        const Track& track = tracks[static_cast<std::size_t>(i)];
        if (!track.max_intrinsic) {
            continue;
        }
        if (track.max_from_min_content) {
            saw_min = true;
        } else {
            saw_max = true;
        }
    }
    if (saw_min && !saw_max) {
        return min_size;
    }
    return max_size;
}

// Section 11.6 Maximize Tracks: with free space left, grow every non-fr track toward its growth
// limit. The extra is shared in proportion to what each track can still take, so a track that
// reaches its limit stops and the remaining ones take the rest. With no free space nothing moves -
// that half is what the old unconditional `base = limit` skipped, and it is what made a container
// narrower than its tracks pull every auto / max-content column out to its maximum.
void maximize_tracks(std::vector<Track>& tracks, bool available_known, float available, float gap,
                     bool maximize_to_limits)
{
    // CSS Grid section 12.6 fixes the free space this step distributes by the sizing constraint:
    // it is infinite under a max-content constraint and zero under a min-content one. An auto-sized
    // grid that is asked for its intrinsic inline size is the max-content case - an item's
    // max-content contribution is exactly the growth limit the tracks were given - and skipping the
    // step left every `auto` track at its min-content base. A `place-items: center` button whose
    // text is "列表" came out one character wide (12px against Chrome's 24px) and its own text
    // overflowed the track that was supposed to hold it.
    if (!available_known && !maximize_to_limits) {
        return;
    }
    float free = available_known ? available - sum_tracks(tracks, gap) : kInf;
    for (int round = 0; round < 4 && free > 0.25f; ++round) {
        float potential = 0;
        for (const Track& track : tracks) {
            if (track.is_fr || !std::isfinite(track.limit)) {
                continue;
            }
            potential += std::max(0.f, track.limit - track.base);
        }
        if (potential <= 0.f) {
            return;
        }
        float given = 0;
        for (Track& track : tracks) {
            if (track.is_fr || !std::isfinite(track.limit)) {
                continue;
            }
            const float room = std::max(0.f, track.limit - track.base);
            if (room <= 0.f) {
                continue;
            }
            const float add = std::min(free * room / potential, room);
            track.base += add;
            given += add;
        }
        if (given <= 0.25f) {
            return;
        }
        free -= given;
    }
}

void size_columns(std::vector<Track>& columns, const std::vector<Track>& preset,
                  const std::vector<GridItem>& items, float gap, bool available_known,
                  float available, FlexPack pack, bool maximize_to_limits)
{
    columns = preset;
    // Growth limits, then base sizes, then maximize (sections 11.4 to 11.6).
    for (const GridItem& item : items) {
        contribute_limits(columns, item.col, item.col_span,
                          limit_contribution(columns, item.col, item.col_span, item.min_inline,
                                             item.max_inline),
                          gap);
    }
    for (Track& track : columns) {
        // §11.5 step 5: a track that never received an intrinsic contribution keeps
        // its base size. An infinite limit must not reach Maximize Tracks.
        if (!std::isfinite(track.limit)) {
            track.limit = track.base;
        }
        // fit-content() caps the growth limit.
        if (std::isfinite(track.limit_cap)) {
            track.limit = std::min(track.limit, track.limit_cap);
        }
        if (track.max_intrinsic && track.limit < track.base) {
            track.limit = track.base;
        }
    }
    for (const GridItem& item : items) {
        contribute_bases(columns, item.col, item.col_span, item.min_inline, gap);
    }
    for (Track& track : columns) {
        // Step 4: a base size never sits above its growth limit.
        if (track.max_intrinsic && track.limit < track.base) {
            track.limit = track.base;
        }
    }
    maximize_tracks(columns, available_known, available, gap, maximize_to_limits);
    solve_fr(columns, available_known, available, gap);
    stretch_auto_tracks(columns, available_known, available, gap, pack);
}

void size_rows(std::vector<Track>& rows, const std::vector<Track>& preset,
               const std::vector<GridItem>& items, float gap, bool available_known,
               float available, FlexPack pack, bool maximize_to_limits)
{
    rows = preset;
    // Growth limits, then base sizes, then maximize (sections 11.4 to 11.6), the same order the
    // column axis uses. The row base follows the minimum contribution; the max-content size only
    // caps what Maximize Tracks may add out of the container's free space.
    for (const GridItem& item : items) {
        contribute_limits(rows, item.row, item.row_span,
                          limit_contribution(rows, item.row, item.row_span, item.min_block,
                                             item.natural_block),
                          gap);
    }
    for (Track& track : rows) {
        if (!std::isfinite(track.limit)) {
            track.limit = track.base;
        }
        if (std::isfinite(track.limit_cap)) {
            track.limit = std::min(track.limit, track.limit_cap);
        }
        if (track.max_intrinsic && track.limit < track.base) {
            track.limit = track.base;
        }
    }
    for (const GridItem& item : items) {
        contribute_bases(rows, item.row, item.row_span, item.min_block, gap);
    }
    for (Track& track : rows) {
        if (track.max_intrinsic && track.limit < track.base) {
            track.limit = track.base;
        }
    }
    maximize_tracks(rows, available_known, available, gap, maximize_to_limits);
    solve_fr(rows, available_known, available, gap);
    stretch_auto_tracks(rows, available_known, available, gap, pack);
}

}  // namespace grid_detail
}  // namespace layout
}  // namespace zui
