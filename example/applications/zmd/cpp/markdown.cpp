#include "markdown.h"

#include <cctype>
#include <cstdlib>
#include <string>

#include "util.h"

namespace zmd {
namespace {

int heading_depth(std::string_view line, std::string_view* text) {
    std::size_t i = 0;
    while (i < line.size() && line[i] == '#') {
        ++i;
    }
    if (i == 0 || i > 6 || (i < line.size() && line[i] != ' ')) {
        return 0;
    }
    *text = util::trim(line.substr(i));
    return static_cast<int>(i);
}

bool is_rule(std::string_view line) {
    if (line.size() < 3) {
        return false;
    }
    char c = line[0];
    if (c != '-' && c != '*' && c != '_') {
        return false;
    }
    for (char ch : line) {
        if (ch != c && ch != ' ') {
            return false;
        }
    }
    return true;
}

/// An image or link target the preview fragment can reach. The fragment is a scratch file, so a
/// relative path has to be resolved against the document; a scheme, an absolute path (POSIX or
/// drive-lettered), and a bare fragment stay as they are.
std::string resolved_target(std::string_view url, std::string_view base) {
    if (base.empty() || url.empty() || url[0] == '/' || url[0] == '#' || url[0] == '\\') {
        return std::string(url);
    }
    if (url.size() > 1 && url[1] == ':') {
        return std::string(url);
    }
    if (url.find("://") != std::string_view::npos ||
        (url.size() >= 5 && url.compare(0, 5, "data:") == 0)) {
        return std::string(url);
    }
    std::string out(base);
    if (!out.empty() && out.back() != '/' && out.back() != '\\') {
        out.push_back('/');
    }
    out.append(url);
    return out;
}

bool is_table_row(std::string_view line) {
    return line.size() > 1 && line.front() == '|' && line.back() == '|';
}

bool is_table_rule(std::string_view line) {
    if (!is_table_row(line)) {
        return false;
    }
    for (char c : line) {
        if (c != '|' && c != '-' && c != ':' && c != ' ') {
            return false;
        }
    }
    return true;
}

std::size_t pipe_outside_code(std::string_view line, std::size_t from) {
    bool code = false;
    for (std::size_t i = from; i < line.size(); ++i) {
        if (line[i] == '`') {
            code = !code;
            continue;
        }
        if (line[i] == '|' && !code) {
            return i;
        }
    }
    return std::string_view::npos;
}

std::vector<std::string_view> table_cells(std::string_view line) {
    if (!line.empty() && line.front() == '|') {
        line.remove_prefix(1);
    }
    if (!line.empty() && line.back() == '|') {
        line.remove_suffix(1);
    }
    std::vector<std::string_view> cells;
    std::size_t begin = 0;
    for (;;) {
        std::size_t bar = pipe_outside_code(line, begin);
        cells.push_back(util::trim(bar == std::string_view::npos
                                       ? line.substr(begin)
                                       : line.substr(begin, bar - begin)));
        if (bar == std::string_view::npos) {
            break;
        }
        begin = bar + 1;
    }
    return cells;
}

/// A bullet line: marker, optional task box, body.
struct Bullet {
    bool matched = false;
    bool ordered = false;
    /// The ordinal an ordered marker spells (`3. `). 1 when the marker is not an ordered one.
    int number = 1;
    bool task = false;
    bool checked = false;
    std::string_view body;
};

/// Bytes in the UTF-8 codepoint at @p at. A stray continuation or truncated lead is one byte,
/// so a later emitter can still make progress instead of inserting a tag in the middle of a
/// well-formed character.
std::size_t utf8_cp_len(std::string_view s, std::size_t at) {
    if (at >= s.size()) {
        return 0;
    }
    const unsigned char c = static_cast<unsigned char>(s[at]);
    std::size_t n = 1;
    if ((c & 0x80) == 0) {
        n = 1;
    } else if ((c & 0xE0) == 0xC0) {
        n = 2;
    } else if ((c & 0xF0) == 0xE0) {
        n = 3;
    } else if ((c & 0xF8) == 0xF0) {
        n = 4;
    }
    if (at + n > s.size()) {
        return s.size() - at;
    }
    return n;
}

Bullet parse_bullet(std::string_view line) {
    Bullet out;
    if (line.size() > 1 && (line[0] == '-' || line[0] == '*' || line[0] == '+') &&
        line[1] == ' ') {
        out.matched = true;
        out.body = util::trim(line.substr(2));
    } else if (!line.empty() && std::isdigit(static_cast<unsigned char>(line[0]))) {
        std::size_t dot = line.find(". ");
        if (dot != std::string_view::npos && dot <= 3) {
            out.matched = true;
            out.ordered = true;
            // `3. ` starts the list at 3, the way a browser numbers `<ol start=3>`.
            out.number = std::atoi(std::string(line.substr(0, dot)).c_str());
            out.body = util::trim(line.substr(dot + 2));
        }
    }
    if (out.matched && out.body.size() > 3 && out.body[0] == '[' && out.body[2] == ']') {
        out.task = true;
        out.checked = out.body[1] == 'x' || out.body[1] == 'X';
        out.body = util::trim(out.body.substr(3));
    }
    return out;
}

}  // namespace

std::vector<Heading> Markdown::headings(std::string_view source) const {
    std::vector<Heading> out;
    bool fence = false;
    for (std::string_view raw : util::lines(source)) {
        std::string_view line = util::trim(raw);
        if (util::starts_with(line, "```")) {
            fence = !fence;
            continue;
        }
        if (fence) {
            continue;
        }
        std::string_view text;
        int depth = heading_depth(line, &text);
        if (depth == 0) {
            continue;
        }
        Heading heading;
        heading.depth = depth;
        for (char c : text) {
            if (c != '*' && c != '`') {
                heading.text.push_back(c);
            }
        }
        out.push_back(std::move(heading));
    }
    return out;
}

std::pair<int, int> Markdown::counts(std::string_view source) const {
    return util::counts(source);
}

std::string Markdown::inline_text(std::string_view text, std::string_view base, std::size_t abs,
                                  const MarkRanges* marks, std::vector<std::size_t>* vis,
                                  std::size_t* vis_after) const {
    std::string out;
    out.reserve(text.size() + 8);
    // The find hit that covers an absolute offset, or null. `marks` is sorted, so this is a
    // binary search per emitted byte rather than a scan per hit.
    auto covering = [&](std::size_t at) -> const std::pair<std::size_t, std::size_t>* {
        if (marks == nullptr || marks->empty()) {
            return nullptr;
        }
        auto it = std::upper_bound(
            marks->begin(), marks->end(), at,
            [](std::size_t value, const std::pair<std::size_t, std::size_t>& range) {
                return value < range.first;
            });
        if (it == marks->begin()) {
            return nullptr;
        }
        --it;
        return at < it->second ? &*it : nullptr;
    };
    // One DOM byte back to the source byte that produced it. An HTML entity (`&amp;`) is still
    // one DOM character. A code-span space is `&#160;` (U+00A0, two UTF-8 bytes).
    auto note = [&](std::size_t src, int dom_bytes) {
        if (vis != nullptr) {
            for (int n = 0; n < dom_bytes; ++n) {
                vis->push_back(src);
            }
        }
        if (vis_after != nullptr) {
            *vis_after = src + 1;
        }
    };
    // One UTF-8 codepoint, never a single continuation byte: a find `<mark>` between the three
    // bytes of `，` / `现` paints three .notdef glyphs.
    auto emit_cp = [&](std::size_t src, bool code_space) -> std::size_t {
        if (code_space) {
            out += "&#160;";
            note(src, 2);
            return 1;
        }
        const std::size_t n = utf8_cp_len(text, src - abs);
        if (n == 0) {
            return 1;
        }
        out += util::escape(text.substr(src - abs, n));
        for (std::size_t k = 0; k < n; ++k) {
            note(src + k, 1);
        }
        return n;
    };
    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '!' && i + 1 < text.size() && text[i + 1] == '[') {
            std::size_t close = text.find(']', i + 2);
            std::size_t open = close == std::string_view::npos ? std::string_view::npos
                                                               : text.find('(', close);
            std::size_t end = open == std::string_view::npos ? std::string_view::npos
                                                             : text.find(')', open);
            if (end != std::string_view::npos) {
                out += "<img src=\"" +
                       util::escape(resolved_target(text.substr(open + 1, end - open - 1), base)) +
                       "\" alt=\"" + util::escape(text.substr(i + 2, close - i - 2)) + "\">";
                i = end + 1;
                continue;
            }
        }
        if (text[i] == '[') {
            std::size_t close = text.find(']', i + 1);
            std::size_t open = close == std::string_view::npos ? std::string_view::npos
                                                               : text.find('(', close);
            std::size_t end = open == std::string_view::npos ? std::string_view::npos
                                                             : text.find(')', open);
            if (end != std::string_view::npos) {
                out += "<a href=\"" +
                       util::escape(resolved_target(text.substr(open + 1, end - open - 1), base)) +
                       "\">";
                for (std::size_t k = i + 1; k < close; ) {
                    k += emit_cp(abs + k, false);
                }
                out += "</a>";
                i = end + 1;
                continue;
            }
        }
        if (text[i] == '`') {
            std::size_t close = text.find('`', i + 1);
            if (close != std::string_view::npos && close > i + 1) {
                out += "<code>";
                for (std::size_t k = i + 1; k < close; ) {
                    const std::size_t at = abs + k;
                    const auto* span = covering(at);
                    if (span != nullptr && span->first == at) {
                        out += "<mark class=\"mdw-f-mark\">";
                    }
                    // A code span keeps its spaces literal, which is why it is not escaped in
                    // one piece (CSS Text 3: the run must not collapse).
                    const std::size_t n = emit_cp(at, text[k] == ' ');
                    if (span != nullptr && span->second <= at + n) {
                        out += "</mark>";
                    }
                    k += n;
                }
                out += "</code>";
                i = close + 1;
                continue;
            }
        }
        if (text[i] == '*' || text[i] == '_' || text[i] == '~') {
            char marker = text[i];
            bool doubled = i + 1 < text.size() && text[i + 1] == marker;
            std::size_t span = doubled ? 2 : 1;
            std::size_t close = text.find(std::string(span, marker), i + span);
            if (close != std::string_view::npos && close > i + span) {
                const char* tag = marker == '~' ? "del" : (doubled ? "strong" : "em");
                out += std::string("<") + tag + ">" +
                       inline_text(text.substr(i + span, close - i - span), base, abs + i + span,
                                   marks, vis, vis_after) +
                       "</" + tag + ">";
                i = close + span;
                continue;
            }
        }
        // ==高亮==, the mark extension the prototype's palette offers. The delimiters may not be
        // padded (CommonMark's flanking rule, the same reason `*` needs a neighbour): `a == b`
        // stays literal text.
        if (text[i] == '=' && i + 1 < text.size() && text[i + 1] == '=') {
            const std::size_t close = text.find("==", i + 2);
            // A delimiter may not be padded and the run may not be empty, so `a == b` stays text.
            const bool flanked = close != std::string_view::npos && close > i + 2 &&
                                 text[i + 2] != ' ' && text[close - 1] != ' ';
            if (flanked) {
                out += "<mark>" +
                       inline_text(text.substr(i + 2, close - i - 2), base, abs + i + 2, marks, vis,
                                   vis_after) +
                       "</mark>";
                i = close + 2;
                continue;
            }
        }
        const auto* span = covering(abs + i);
        if (span != nullptr && span->first == abs + i) {
            out += "<mark class=\"mdw-f-mark\">";
        }
        const std::size_t n = emit_cp(abs + i, false);
        if (span != nullptr && span->second <= abs + i + n) {
            out += "</mark>";
        }
        i += n;
    }
    return out;
}

std::string Markdown::render(std::string_view source, std::size_t caret, std::string_view base,
                             const MarkRanges* marks, RenderCounts* counts,
                             VisibleSources* sources) const {
    std::vector<std::string_view> all = util::lines(source);
    std::string out;
    bool in_list = false;
    bool ordered = false;
    bool in_task = false;
    bool in_quote = false;
    bool in_code = false;
    int heading_index = 0;
    int block_index = 0;
    std::size_t pos = 0;
    // The prototype marks the top-level block that holds the caret with `mdw-cur` (app.js
    // `updateCurrentBlock`): 聚焦模式 dims everything but that block. Source offsets identify it -
    // each top-level element records where its tag lands in `out` and which source span it covers,
    // and the span ends where the next top-level element begins.
    struct TopBlock {
        std::size_t at = 0;
        std::size_t beg = 0;
        std::size_t end = 0;
    };
    const bool has_caret = caret != static_cast<std::size_t>(-1);
    std::vector<TopBlock> tops;
    auto open_top = [&](std::size_t beg) {
        if (!has_caret) {
            return;
        }
        if (!tops.empty()) {
            tops.back().end = beg;
        }
        tops.push_back(TopBlock{out.size(), beg, 0});
    };

    auto close_list = [&] {
        if (in_list) {
            out += ordered ? "</ol>\n" : "</ul>\n";
            in_list = false;
            in_task = false;
        }
    };
    auto close_quote = [&] {
        if (in_quote) {
            out += "</blockquote>\n";
            in_quote = false;
        }
    };
    // The caret is a class on the block (`mdw-cur`), not a span in the line. A span in the
    // line is a box, and the line moves when it arrives.
    auto open = [&](const std::string& tag, const std::string& id, std::size_t beg,
                    std::size_t end) {
        out += "<" + tag + " id=\"" + id + "\" data-beg=\"" + std::to_string(beg) +
               "\" data-at=\"" + std::to_string(end) + "\">";
    };
    auto block = [&](const std::string& tag, const std::string& id, std::size_t beg,
                     std::size_t end, std::string_view body, std::size_t body_abs) {
        open(tag, id, beg, end);
        std::vector<std::size_t> vis;
        std::size_t after = body_abs;
        const std::string text = inline_text(body, base, body_abs, marks, &vis, &after);
        if (sources != nullptr) {
            (*sources)[id] = VisibleSource{std::move(vis), after};
        }
        // The body travels in an id-carrying inline span: the writing view converts a click's x
        // through the span's box, which is the text's own width. The block's box is the column's,
        // and a short line (a heading, a list item) mapped every click far too early.
        out += "<span class=\"zmd-body\" id=\"" + id + "l\">" + text;
        out += "</span>";
        out += "</" + tag + ">\n";
    };

    std::string code_id;
    std::size_t code_beg = 0;
    std::size_t code_at_mark = std::string::npos;
    std::vector<std::size_t> code_vis;
    bool code_line = false;
    std::size_t code_prev_end = 0;
    // The fence's innerText is part of the paper. It gets an id and a vis map so a source range
    // that crosses it lands on the glyphs after the fence, not short by the code.
    auto finish_code = [&](std::size_t end_at) {
        if (!in_code) {
            return;
        }
        const std::string end = std::to_string(end_at);
        if (code_at_mark != std::string::npos && code_at_mark < out.size()) {
            out.replace(code_at_mark, 1, end);
        }
        out += "</code></pre>\n";
        if (sources != nullptr) {
            (*sources)[code_id] = VisibleSource{std::move(code_vis), end_at};
        }
        in_code = false;
        code_line = false;
    };

    for (std::size_t i = 0; i < all.size(); ++i) {
        std::string_view raw = all[i];
        const std::size_t line_beg = pos;
        const std::size_t line_end = pos + raw.size();
        // `lines()` drops a CR that is still in the source. Stepping only one byte lands the
        // next line on the LF, and every following block's source map shifts by one byte per
        // CRLF. A hit on `assets/` then resolves to the characters before the chip.
        if (line_end < source.size() && source[line_end] == '\r') {
            pos = line_end + 2;
        } else {
            pos = line_end + 1;
        }
        std::string_view line = util::trim(raw);

        if (in_code) {
            if (util::starts_with(line, "```")) {
                finish_code(line_end);
            } else {
                // A newline inside `<code>` does not survive HTML parsing as a line box, so the
                // fence painted as one line on top of the next heading. `<br>` is the break the
                // line layout already honors. The vis entry is that source newline.
                if (code_line) {
                    out += "<br>";
                    code_vis.push_back(code_prev_end);
                }
                code_line = true;
                code_prev_end = line_end;
                // One vis entry per DOM byte. `escape` grows the HTML (`<` → `&lt;`) and the
                // parser shrinks it back, so the map follows the source byte, not the entity.
                for (std::size_t k = 0; k < raw.size(); ++k) {
                    code_vis.push_back(line_beg + k);
                }
                out += util::escape(raw);
            }
            continue;
        }
        if (util::starts_with(line, "```")) {
            close_list();
            close_quote();
            open_top(line_beg);
            code_id = "mdb" + std::to_string(block_index++);
            code_beg = line_beg;
            code_vis.clear();
            out += "<pre id=\"" + code_id + "\" data-beg=\"" + std::to_string(code_beg) +
                   "\" data-at=\"";
            code_at_mark = out.size();
            out += "0\"><code>";
            in_code = true;
            code_line = false;
            continue;
        }
        if (line.empty()) {
            close_list();
            close_quote();
            continue;
        }
        std::string_view head_text;
        int depth = heading_depth(line, &head_text);
        if (depth) {
            close_list();
            close_quote();
            std::string tag = "h" + std::to_string(depth);
            open_top(line_beg);
            block(tag, "mdh" + std::to_string(heading_index), line_beg, line_end, head_text,
                  line_beg + static_cast<std::size_t>(head_text.data() - raw.data()));
            ++heading_index;
            continue;
        }
        if (is_rule(line)) {
            close_list();
            close_quote();
            open_top(line_beg);
            out += "<hr>\n";
            continue;
        }
        if (line.front() == '>') {
            close_list();
            if (!in_quote) {
                open_top(line_beg);
                out += "<blockquote>\n";
                in_quote = true;
            }
            const std::string_view quoted = util::trim(line.substr(1));
            block("p", "mdb" + std::to_string(block_index++), line_beg, line_end, quoted,
                  line_beg + static_cast<std::size_t>(quoted.data() - raw.data()));
            continue;
        }
        if (is_table_row(line) && i + 1 < all.size() &&
            is_table_rule(util::trim(all[i + 1]))) {
            close_list();
            close_quote();
            open_top(line_beg);
            const std::string table_id = "mdb" + std::to_string(block_index++);
            std::vector<std::size_t> table_vis;
            std::size_t table_after = line_beg;
            std::size_t table_end = line_end;
            std::string inner = "\n<thead>\n<tr>";
            for (std::string_view cell : table_cells(line)) {
                inner += "<th>" +
                         inline_text(cell, base, line_beg + (cell.data() - raw.data()), marks,
                                     &table_vis, &table_after) +
                         "</th>";
            }
            inner += "</tr>\n</thead>\n<tbody>\n";
            // `pos` is the separator line. Body rows start after it, and each cell's vis entry
            // is that row's source byte — the header line's `raw` is a different buffer.
            std::size_t cursor = pos;
            if (i + 1 < all.size()) {
                cursor += all[i + 1].size() + 1;
            }
            for (i += 2; i < all.size(); ++i) {
                const std::string_view row_raw = all[i];
                const std::size_t row_beg = cursor;
                const std::size_t row_end = row_beg + row_raw.size();
                if (!is_table_row(util::trim(row_raw))) {
                    pos = row_beg;
                    break;
                }
                cursor = row_end + 1;
                table_end = row_end;
                inner += "<tr>";
                for (std::string_view cell : table_cells(util::trim(row_raw))) {
                    inner += "<td>" +
                             inline_text(cell, base, row_beg + (cell.data() - row_raw.data()),
                                         marks, &table_vis, &table_after) +
                             "</td>";
                }
                inner += "</tr>\n";
            }
            if (i >= all.size()) {
                pos = cursor;
            }
            open("table", table_id, line_beg, table_end);
            out += inner;
            if (sources != nullptr) {
                (*sources)[table_id] = VisibleSource{std::move(table_vis), table_after};
            }
            out += "</tbody>\n</table>\n";
            --i;
            continue;
        }
        Bullet bullet = parse_bullet(line);
        if (bullet.matched) {
            close_quote();
            if (bullet.task) {
                // Same checkbox row the sheet already paints: `.mdw-tasks` > `.mdw-task` >
                // label > input + span. `input:checked + span` is the strikethrough.
                if (!in_list || !in_task) {
                    close_list();
                    open_top(line_beg);
                    out += "<ul class=\"mdw-tasks\">\n";
                    in_list = true;
                    ordered = false;
                    in_task = true;
                }
                const std::string task_id = "mdb" + std::to_string(block_index++);
                out += "<li class=\"mdw-task\" id=\"" + task_id + "\" data-beg=\"" +
                       std::to_string(line_beg) + "\" data-at=\"" + std::to_string(line_end) +
                       "\"><label><input type=\"checkbox\"";
                if (bullet.checked) {
                    out += " checked";
                }
                out += ">";
                std::vector<std::size_t> vis;
                std::size_t after = line_beg + (bullet.body.data() - raw.data());
                const std::string body =
                    inline_text(bullet.body, base, after, marks, &vis, &after);
                if (sources != nullptr) {
                    (*sources)[task_id] = VisibleSource{std::move(vis), after};
                }
                out += "<span class=\"zmd-body\" id=\"" + task_id + "l\">" + body;
                out += "</span></label></li>\n";
                continue;
            }
            if (!in_list || ordered != bullet.ordered || in_task) {
                close_list();
                open_top(line_beg);
                if (bullet.ordered && bullet.number > 1) {
                    // The first marker's ordinal is the list's start (`<ol start=`), which the
                    // sheet's counters already read.
                    out += "<ol start=\"" + std::to_string(bullet.number) + "\">\n";
                } else {
                    out += bullet.ordered ? "<ol>\n" : "<ul>\n";
                }
                in_list = true;
                ordered = bullet.ordered;
            }
            block("li", "mdb" + std::to_string(block_index++), line_beg, line_end, bullet.body,
                  line_beg + static_cast<std::size_t>(bullet.body.data() - raw.data()));
            continue;
        }
        close_list();
        close_quote();
        open_top(line_beg);
        block("p", "mdb" + std::to_string(block_index++), line_beg, line_end, line,
              line_beg + static_cast<std::size_t>(line.data() - raw.data()));
    }
    if (in_code) {
        finish_code(source.size());
    }
    close_list();
    close_quote();
    // An empty source has no block, so the paper has nowhere to paint a caret. One blank
    // line is that place. The zwsp is a real text node (an empty span lays out 0×0) and
    // does not appear as a glyph the reader sees.
    if (block_index == 0 && heading_index == 0 && !in_code) {
        open_top(0);
        out += "<p id=\"mdb0\" data-beg=\"0\" data-at=\"0\"><span class=\"zmd-body\" "
               "id=\"mdb0l\">\xC2\xA0</span></p>";
        block_index = 1;
    }
    if (!tops.empty()) {
        tops.back().end = source.size() + 1;
        for (std::size_t i = tops.size(); i-- > 0;) {
            if (caret >= tops[i].beg && caret < tops[i].end) {
                const std::size_t tag_end = out.find_first_of(" >", tops[i].at);
                out.insert(tag_end, " class=\"mdw-cur\"");
                break;
            }
        }
    }
    if (counts != nullptr) {
        counts->headings = heading_index;
        counts->blocks = block_index;
    }
    // Newlines that only separate tags are not paper text. Leaving them in made innerText grow a
    // collapsed space between blocks, so a source range could not land on glyph offsets.
    std::string paper;
    paper.reserve(out.size());
    for (std::size_t i = 0; i < out.size(); ++i) {
        if (out[i] != '\n') {
            paper.push_back(out[i]);
            continue;
        }
        std::size_t b = paper.size();
        while (b > 0 && (paper[b - 1] == ' ' || paper[b - 1] == '\t')) {
            --b;
        }
        std::size_t f = i + 1;
        while (f < out.size() && (out[f] == ' ' || out[f] == '\t' || out[f] == '\n')) {
            ++f;
        }
        if (b > 0 && paper[b - 1] == '>' && f < out.size() && out[f] == '<') {
            paper.resize(b);
            i = f - 1;
            continue;
        }
        paper.push_back('\n');
    }
    return paper;
}

}  // namespace zmd
