#include "layout/intern_grid.h"
#include "layout/style_literal.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace zui {
namespace layout {
namespace {

struct ParsedLength {
    bool valid = false;
    bool is_auto = false;
    bool is_percent = false;
    float value = 0;
};

}  // namespace

void read_grid(LayoutStyle& style, const html::Node* node) {
    if (node == nullptr) {
        return;
    }
    auto lower = [](std::string_view s) {
        std::string out(s);
        for (char& c : out) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return out;
    };
    auto parse_length_token = [&](std::string_view tok) {
        ParsedLength parsed;
        std::string trimmed(tok);
        while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front()))) {
            trimmed.erase(trimmed.begin());
        }
        while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back()))) {
            trimmed.pop_back();
        }
        if (trimmed.empty() || trimmed == "auto") {
            parsed.valid = true;
            parsed.is_auto = true;
            return parsed;
        }
        // `em` / `rem` / `calc()` resolve here too, against the grid container's font size: a track
        // sizing function only ever sees pixels.
        const css::ResolvedLength resolved =
            resolve_with_viewport(trimmed, font_size_px(node), root_font_size_px(node));
        if (!resolved.valid) {
            return parsed;
        }
        parsed.valid = true;
        parsed.is_percent = resolved.is_percent;
        parsed.value = resolved.px;
        return parsed;
    };
    auto fill_length_side = [&](TrackKind& kind, bool& percent, float& px, float& fr,
                                std::string_view tok) {
        std::string x = lower(tok);
        while (!x.empty() && std::isspace(static_cast<unsigned char>(x.front()))) {
            x.erase(x.begin());
        }
        while (!x.empty() && std::isspace(static_cast<unsigned char>(x.back()))) {
            x.pop_back();
        }
        if (x == "auto") {
            kind = TrackKind::Auto;
            return;
        }
        if (x == "min-content") {
            kind = TrackKind::MinContent;
            return;
        }
        if (x == "max-content") {
            kind = TrackKind::MaxContent;
            return;
        }
        if (!x.empty() && x.size() >= 2 && x.back() == 'r' && x[x.size() - 2] == 'f') {
            kind = TrackKind::Fr;
            fr = std::strtof(x.c_str(), nullptr);
            if (fr <= 0) {
                fr = 1;
            }
            return;
        }
        const ParsedLength parsed = parse_length_token(tok);
        if (parsed.valid && !parsed.is_auto) {
            kind = TrackKind::Length;
            percent = parsed.is_percent;
            px = parsed.value;
            return;
        }
        kind = TrackKind::Auto;
    };
    auto parse_track = [&](std::string_view tok) {
        TrackSize t;
        std::string x = lower(tok);
        if (x.rfind("minmax(", 0) == 0 && !tok.empty() && tok.back() == ')') {
            t.kind = TrackKind::MinMax;
            const std::string_view inner = tok.substr(7, tok.size() - 8);
            const auto comma = inner.find(',');
            if (comma != std::string_view::npos) {
                fill_length_side(t.min_kind, t.min_percent, t.min_px, t.min_fr, inner.substr(0, comma));
                fill_length_side(t.max_kind, t.max_percent, t.max_px, t.max_fr, inner.substr(comma + 1));
            }
            return t;
        }
        // fit-content(<length-percentage>): the argument is the upper bound of the track, and the
        // track otherwise sizes like max-content with an automatic minimum (section 7.2.3).
        if (x.rfind("fit-content(", 0) == 0 && !tok.empty() && tok.back() == ')') {
            const ParsedLength parsed = parse_length_token(tok.substr(12, tok.size() - 13));
            if (parsed.valid && !parsed.is_auto) {
                t.kind = TrackKind::FitContent;
                t.length_percent = parsed.is_percent;
                t.length_px = parsed.value;
            } else {
                t.kind = TrackKind::MaxContent;
            }
            return t;
        }
        fill_length_side(t.kind, t.length_percent, t.length_px, t.fr, tok);
        return t;
    };
    auto split_names = [](std::string_view raw) {
        std::vector<std::string> out;
        std::string cur;
        for (char c : raw) {
            if (std::isspace(static_cast<unsigned char>(c))) {
                if (!cur.empty()) {
                    out.push_back(cur);
                    cur.clear();
                }
                continue;
            }
            cur.push_back(c);
        }
        if (!cur.empty()) {
            out.push_back(cur);
        }
        return out;
    };
    struct ParsedTracks {
        std::vector<TrackSize> tracks;
        // One entry per line, tracks + 1; a line may carry several names. `[a b]` groups fill the
        // line they precede.
        std::vector<std::vector<std::string>> line_names;
    };
    std::function<ParsedTracks(std::string_view)> parse_tracks;
    parse_tracks = [&](std::string_view src) {
        ParsedTracks out;
        out.line_names.emplace_back();
        // `none` is the initial value and means the axis has no explicit tracks at all: the axis
        // is then made of implicit tracks, not of one `auto` track per axis.
        {
            std::string head_lower;
            for (char c : src) {
                if (!std::isspace(static_cast<unsigned char>(c))) {
                    head_lower.push_back(
                        static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                }
            }
            if (head_lower == "none") {
                return out;
            }
        }
        std::string cur;
        int depth = 0;
        std::vector<std::string> toks;
        auto flush = [&]() {
            if (!cur.empty()) {
                toks.push_back(cur);
                cur.clear();
            }
        };
        for (std::size_t i = 0; i < src.size(); ++i) {
            const char c = src[i];
            if (c == '[' && depth == 0) {
                // A line-name group: keep it as its own token so the names reach the line they
                // precede instead of being dropped (they are what `grid-column: header` resolves
                // against). Inside `repeat(...)` the group stays in the repetition's source text;
                // that text is tokenised again once the repetition is unpacked.
                flush();
                std::string names;
                ++i;
                while (i < src.size() && src[i] != ']') {
                    names.push_back(src[i]);
                    ++i;
                }
                toks.push_back("[" + names + "]");
                continue;
            }
            if (c == '(') {
                ++depth;
                cur.push_back(c);
            } else if (c == ')') {
                if (depth > 0) {
                    --depth;
                }
                cur.push_back(c);
            } else if (std::isspace(static_cast<unsigned char>(c)) && depth == 0) {
                flush();
            } else {
                cur.push_back(c);
            }
        }
        flush();
        // `subgrid <line-name-list>?` is the whole value (CSS Grid §7.2). Each bracket group
        // is one line, in order, with no track size between them. Mixed with a real track
        // size the declaration is invalid and drops, the same as `none`.
        {
            bool saw_subgrid = false;
            bool subgrid_invalid = false;
            for (const std::string& token : toks) {
                if (!token.empty() && token.front() == '[') {
                    continue;
                }
                if (lower(token) == "subgrid") {
                    if (saw_subgrid) {
                        subgrid_invalid = true;
                    }
                    saw_subgrid = true;
                    continue;
                }
                subgrid_invalid = true;
            }
            if (saw_subgrid) {
                ParsedTracks sub;
                if (subgrid_invalid) {
                    return sub;
                }
                TrackSize sentinel;
                sentinel.kind = TrackKind::Subgrid;
                sub.tracks.push_back(std::move(sentinel));
                for (const std::string& token : toks) {
                    if (token.empty() || token.front() != '[') {
                        continue;
                    }
                    const std::string inner = token.substr(1, token.size() - 2);
                    sub.line_names.push_back(split_names(inner));
                }
                if (sub.line_names.empty()) {
                    sub.line_names.emplace_back();
                }
                return sub;
            }
        }
        for (std::size_t i = 0; i < toks.size(); ++i) {
            const std::string token = toks[i];
            if (!token.empty() && token.front() == '[') {
                const std::string inner = token.substr(1, token.size() - 2);
                for (const std::string& name : split_names(inner)) {
                    out.line_names.back().push_back(name);
                }
                continue;
            }
            std::string x = lower(token);
            if (x.rfind("repeat(", 0) == 0 && toks[i].back() == ')') {
                const std::string inner = toks[i].substr(7, toks[i].size() - 8);
                const auto comma = inner.find(',');
                int n = 1;
                std::string rest;
                TrackKind auto_kind = TrackKind::Auto;
                if (comma != std::string::npos) {
                    const std::string count = lower(inner.substr(0, comma));
                    if (count.find("auto-fill") != std::string::npos) {
                        auto_kind = TrackKind::AutoFill;
                    } else if (count.find("auto-fit") != std::string::npos) {
                        auto_kind = TrackKind::AutoFit;
                    } else {
                        n = std::max(1, std::atoi(count.c_str()));
                    }
                    rest = inner.substr(comma + 1);
                }
                if (auto_kind != TrackKind::Auto) {
                    // How many repetitions fit depends on the container's size, so the template
                    // keeps a marker and the repeated list; the layout expands it (CSS Grid
                    // section 7.2.3.1). Leaving it as `repeat 1` (what atoi("auto-fill") used to
                    // give) silently produced a single track.
                    TrackSize marker;
                    marker.kind = auto_kind;
                    ParsedTracks inner = parse_tracks(rest);
                    marker.repeat = std::move(inner.tracks);
                    marker.repeat_names = std::move(inner.line_names);
                    out.tracks.push_back(std::move(marker));
                    out.line_names.emplace_back();
                    continue;
                }
                const ParsedTracks once = parse_tracks(rest);
                for (int k = 0; k < n; ++k) {
                    for (std::size_t t = 0; t < once.tracks.size(); ++t) {
                        out.tracks.push_back(once.tracks[t]);
                        // The names of the repeated list ride along with each repetition.
                        out.line_names.back().insert(out.line_names.back().end(),
                                                     once.line_names[t].begin(),
                                                     once.line_names[t].end());
                        out.line_names.emplace_back(once.line_names[t + 1]);
                    }
                }
            } else {
                out.tracks.push_back(parse_track(token));
                out.line_names.emplace_back();
            }
        }
        out.line_names.resize(out.tracks.size() + 1);
        return out;
    };
    auto split_ws = [&](std::string_view raw) {
        std::vector<std::string> parts;
        std::string cur;
        for (char c : raw) {
            if (std::isspace(static_cast<unsigned char>(c))) {
                if (!cur.empty()) {
                    parts.push_back(cur);
                    cur.clear();
                }
            } else {
                cur.push_back(c);
            }
        }
        if (!cur.empty()) {
            parts.push_back(cur);
        }
        return parts;
    };
    auto parse_line = [&](std::string_view raw) {
        GridLine g;
        const auto p = split_ws(raw);
        if (p.empty() || lower(p[0]) == "auto") {
            return g;
        }
        std::size_t at = 0;
        if (lower(p[0]) == "span") {
            g.is_span = true;
            ++at;
        }
        if (at >= p.size()) {
            return g;  // a bare `span` is auto
        }
        g.is_auto = false;
        // A number is a line number (negative counts back from the end of the explicit grid);
        // anything else is a line name. `span` takes either: `span 2` and `span header`.
        const std::string& token = p[at];
        char* end = nullptr;
        const long number = std::strtol(token.c_str(), &end, 10);
        if (end != token.c_str() && *end == '\0') {
            if (number == 0) {
                g.is_auto = true;  // 0 is not a valid line
                return g;
            }
            if (g.is_span) {
                g.span = static_cast<int>(std::max<long>(1, std::abs(number)));
                g.line = 0;
                return g;
            }
            g.line = static_cast<int>(number);
            g.span = 1;
            return g;
        }
        g.name = lower(token);
        g.line = 0;
        g.span = 1;
        return g;
    };
    auto parse_place = [&](const std::string* shorthand, const std::string* start,
                           const std::string* end, GridLine& a, GridLine& b) {
        if (shorthand != nullptr) {
            const auto slash = shorthand->find('/');
            if (slash == std::string::npos) {
                a = parse_line(*shorthand);
                b.is_auto = false;
                b.line = 0;
                b.span = 1;
            } else {
                a = parse_line(std::string_view(shorthand->c_str(), slash));
                b = parse_line(std::string_view(shorthand->c_str() + slash + 1));
            }
        }
        if (start != nullptr) {
            a = parse_line(*start);
        }
        if (end != nullptr) {
            b = parse_line(*end);
        }
    };
    if (const std::string* v = style_literal(node, "grid-template-columns")) {
        ParsedTracks parsed = parse_tracks(*v);
        style.grid_template_columns = std::move(parsed.tracks);
        style.grid_column_line_names = std::move(parsed.line_names);
    }
    if (const std::string* v = style_literal(node, "grid-template-rows")) {
        ParsedTracks parsed = parse_tracks(*v);
        style.grid_template_rows = std::move(parsed.tracks);
        style.grid_row_line_names = std::move(parsed.line_names);
    }
    if (const std::string* v = style_literal(node, "grid-auto-columns")) {
        style.grid_auto_columns = parse_tracks(*v).tracks;
    }
    if (const std::string* v = style_literal(node, "grid-auto-rows")) {
        style.grid_auto_rows = parse_tracks(*v).tracks;
    }
    // `grid-template-areas`: a list of quoted rows of area names, `.` being a null cell. Every row
    // has to carry the same number of cells and every name has to cover a rectangle, or the whole
    // declaration is dropped (CSS Grid section 7.3).
    if (const std::string* v = style_literal(node, "grid-template-areas")) {
        std::vector<std::vector<std::string>> areas;
        bool ok = true;
        for (std::size_t i = 0; i < v->size() && ok;) {
            const char quote = (*v)[i];
            if (quote != '"' && quote != '\'') {
                ++i;
                continue;
            }
            const std::size_t close = v->find(quote, i + 1);
            if (close == std::string::npos) {
                ok = false;
                break;
            }
            std::vector<std::string> cells;
            std::string token;
            auto flush_cell = [&]() {
                if (token.empty()) {
                    return;
                }
                const bool null_cell =
                    token.find_first_not_of('.') == std::string::npos;
                cells.push_back(null_cell ? std::string() : lower(token));
                token.clear();
            };
            for (std::size_t k = i + 1; k < close; ++k) {
                const char c = (*v)[k];
                if (std::isspace(static_cast<unsigned char>(c))) {
                    flush_cell();
                } else {
                    token.push_back(c);
                }
            }
            flush_cell();
            areas.push_back(std::move(cells));
            i = close + 1;
        }
        const std::size_t columns = areas.empty() ? 0 : areas.front().size();
        for (const std::vector<std::string>& row : areas) {
            if (row.size() != columns) {
                ok = false;
            }
        }
        if (ok && columns > 0) {
            style.grid_template_areas = std::move(areas);
        }
    }
    if (!style.grid_template_areas.empty()) {
        const std::size_t area_rows = style.grid_template_areas.size();
        const std::size_t area_columns = style.grid_template_areas.front().size();
        // The areas are part of the explicit grid, so a template that only gives areas still has
        // lines for them; the extra tracks are auto-sized (section 7.3).
        while (style.grid_template_columns.size() < area_columns) {
            style.grid_template_columns.emplace_back();
        }
        while (style.grid_template_rows.size() < area_rows) {
            style.grid_template_rows.emplace_back();
        }
        style.grid_column_line_names.resize(
            std::max(style.grid_column_line_names.size(), area_columns + 1));
        style.grid_row_line_names.resize(
            std::max(style.grid_row_line_names.size(), area_rows + 1));
        struct AreaBox {
            std::size_t row_start = 0;
            std::size_t row_end = 0;
            std::size_t column_start = 0;
            std::size_t column_end = 0;
        };
        std::vector<std::pair<std::string, AreaBox>> boxes;
        bool rectangular = true;
        for (std::size_t r = 0; r < area_rows; ++r) {
            for (std::size_t c = 0; c < area_columns; ++c) {
                const std::string& name = style.grid_template_areas[r][c];
                if (name.empty()) {
                    continue;
                }
                auto it = std::find_if(boxes.begin(), boxes.end(),
                                       [&](const auto& entry) { return entry.first == name; });
                if (it == boxes.end()) {
                    boxes.push_back({name, AreaBox{r, r, c, c}});
                    continue;
                }
                it->second.row_start = std::min(it->second.row_start, r);
                it->second.row_end = std::max(it->second.row_end, r);
                it->second.column_start = std::min(it->second.column_start, c);
                it->second.column_end = std::max(it->second.column_end, c);
            }
        }
        for (const auto& entry : boxes) {
            for (std::size_t r = entry.second.row_start; r <= entry.second.row_end && rectangular;
                 ++r) {
                for (std::size_t c = entry.second.column_start;
                     c <= entry.second.column_end && rectangular; ++c) {
                    if (style.grid_template_areas[r][c] != entry.first) {
                        rectangular = false;
                    }
                }
            }
        }
        if (!rectangular) {
            // A name that does not form a rectangle invalidates the declaration, names and all.
            style.grid_template_areas.clear();
        } else {
            for (const auto& entry : boxes) {
                style.grid_row_line_names[entry.second.row_start].push_back(entry.first + "-start");
                style.grid_row_line_names[entry.second.row_end + 1].push_back(entry.first + "-end");
                style.grid_column_line_names[entry.second.column_start].push_back(entry.first +
                                                                                  "-start");
                style.grid_column_line_names[entry.second.column_end + 1].push_back(entry.first +
                                                                                    "-end");
            }
        }
    }
    if (const std::string* v = style_literal(node, "grid-area")) {
        // `grid-area: <grid-line> [ / <grid-line> ]{0,3}`, in row-start / column-start / row-end /
        // column-end order; a missing value is copied from the one it pairs with. A name that
        // stands for a grid area resolves against the container's `<name>-start` / `<name>-end`
        // lines at layout time, where the areas are known (section 8.3).
        std::vector<std::string> parts;
        std::size_t from = 0;
        while (from <= v->size()) {
            const std::size_t slash = v->find('/', from);
            parts.push_back(v->substr(from, slash == std::string::npos ? std::string::npos
                                                                      : slash - from));
            if (slash == std::string::npos) {
                break;
            }
            from = slash + 1;
        }
        if (!parts.empty() && parts.size() <= 4) {
            const std::string& row_start = parts[0];
            const std::string& column_start = parts.size() >= 2 ? parts[1] : parts[0];
            const std::string& row_end = parts.size() >= 3 ? parts[2] : row_start;
            const std::string& column_end = parts.size() >= 4 ? parts[3] : column_start;
            style.grid_row_start = parse_line(row_start);
            style.grid_column_start = parse_line(column_start);
            style.grid_row_end = parse_line(row_end);
            style.grid_column_end = parse_line(column_end);
        }
    }
    // The explicit longhands win over `grid-area`: the cascade keeps one value per property, so
    // this is the only rule that can separate the two when a style gives both.
    parse_place(style_literal(node, "grid-column"), style_literal(node, "grid-column-start"),
                style_literal(node, "grid-column-end"), style.grid_column_start,
                style.grid_column_end);
    parse_place(style_literal(node, "grid-row"), style_literal(node, "grid-row-start"),
                style_literal(node, "grid-row-end"), style.grid_row_start, style.grid_row_end);
    if (const std::string* v = style_literal(node, "justify-items")) {
        if (*v == "start" || *v == "flex-start" || *v == "left") {
            style.justify_items = FlexAlign::Start;
        } else if (*v == "end" || *v == "flex-end" || *v == "right") {
            style.justify_items = FlexAlign::End;
        } else if (*v == "center") {
            style.justify_items = FlexAlign::Center;
        } else if (*v == "stretch") {
            style.justify_items = FlexAlign::Stretch;
        } else if (*v == "baseline" || *v == "first baseline") {
            style.justify_items = FlexAlign::Baseline;
        } else if (*v == "last baseline") {
            style.justify_items = FlexAlign::LastBaseline;
        }
    }
    if (const std::string* pack = style_literal(node, "align-content")) {
        style.align_content = parse_pack(*pack);
    }
    if (const std::string* v = style_literal(node, "grid-auto-flow")) {
        const std::string x = lower(*v);
        style.grid_auto_flow_column = x.find("column") != std::string::npos;
        style.grid_auto_flow_dense = x.find("dense") != std::string::npos;
    }
}

}  // namespace layout
}  // namespace zui
