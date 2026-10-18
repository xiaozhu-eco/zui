#ifndef ZUI_INPUT_EDIT_MODEL_H
#define ZUI_INPUT_EDIT_MODEL_H

#include "layout/caret/caret_types.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace zui {
namespace input {

struct CompositionRange {
    std::size_t start = 0;
    std::size_t end = 0;
    std::string preview_text;
};

struct FormatRun {
    std::size_t start = 0;
    std::size_t end = 0;
    std::uint8_t style_bits = 0;  // bit0 bold, bit1 italic
};

/// IME composition and caret scratch for the focused editable. Document Selection is the
/// caret/highlight. A contenteditable host's authority is the DOM tree; value is flatten.
struct EditModel {
    std::string text;
    std::size_t anchor = 0;
    std::size_t focus = 0;
    std::optional<CompositionRange> composing;
    layout::CaretAffinity affinity = layout::CaretAffinity::Downstream;
    std::vector<FormatRun> format_runs;  // empty for plain input

    void clamp_selection() {
        if (anchor > text.size()) {
            anchor = text.size();
        }
        if (focus > text.size()) {
            focus = text.size();
        }
    }

    bool has_range_selection() const { return anchor != focus; }

    /// Delete [min(anchor,focus), max(anchor,focus)) when non-collapsed; leave caret at start.
    bool erase_selection() {
        clamp_selection();
        if (anchor == focus) {
            return false;
        }
        const std::size_t a = std::min(anchor, focus);
        const std::size_t b = std::max(anchor, focus);
        return erase_formatted(a, b);
    }

    /// Replace [start, end) with @p next; returns false when the slice already equals @p next.
    bool replace_range(std::size_t start, std::size_t end, std::string_view next) {
        if (start > text.size()) {
            start = text.size();
        }
        if (end > text.size()) {
            end = text.size();
        }
        if (end < start) {
            end = start;
        }
        const bool same =
            (end - start) == next.size() &&
            (next.empty() || text.compare(start, end - start, next.data(), next.size()) == 0);
        if (same) {
            focus = start + next.size();
            anchor = focus;
            return false;
        }
        if (format_runs.empty()) {
            text.erase(start, end - start);
            text.insert(start, next.data(), next.size());
            focus = start + next.size();
            anchor = focus;
            return true;
        }
        // Inherit mark of the character before the splice (spec 5.2 insert rule).
        const std::uint8_t mark = style_before(start);
        std::string styles = expand_format();
        if (styles.size() < text.size()) {
            styles.resize(text.size(), '\0');
        }
        text.erase(start, end - start);
        if (styles.size() >= end) {
            styles.erase(start, end - start);
        } else if (styles.size() > start) {
            styles.resize(start);
        }
        text.insert(start, next.data(), next.size());
        if (styles.size() < start) {
            styles.resize(start, '\0');
        }
        styles.insert(start, next.size(), static_cast<char>(mark));
        compress_format(styles);
        focus = start + next.size();
        anchor = focus;
        return true;
    }

    /// Spec 5.3: start composition at focus (after collapsing any selection).
    void begin_composition() {
        erase_selection();
        clamp_selection();
        CompositionRange range;
        range.start = focus;
        range.end = focus;
        range.preview_text.clear();
        composing = range;
    }

    /// Replace the open composition range with @p preview; focus = end.
    bool update_composition(std::string_view preview) {
        if (!composing.has_value()) {
            begin_composition();
        }
        CompositionRange& range = *composing;
        const bool changed = replace_range(range.start, range.end, preview);
        range.end = range.start + preview.size();
        range.preview_text.assign(preview.begin(), preview.end());
        focus = range.end;
        anchor = focus;
        return changed;
    }

    /// Commit: keep text (preview already applied) or replace with @p final_text; clear composing.
    bool commit_composition(std::string_view final_text) {
        if (!composing.has_value()) {
            return false;
        }
        CompositionRange range = *composing;
        if (final_text != std::string_view(range.preview_text)) {
            (void)replace_range(range.start, range.end, final_text);
        } else {
            focus = range.end;
            anchor = focus;
        }
        composing = std::nullopt;
        return true;
    }

    /// Cancel: delete [start, end); caret back to start.
    bool cancel_composition() {
        if (!composing.has_value()) {
            return false;
        }
        const std::size_t start = composing->start;
        (void)replace_range(composing->start, composing->end, "");
        composing = std::nullopt;
        focus = start;
        anchor = focus;
        return true;
    }

    /// Style of the byte immediately before @p pos (insert inherits this). Offset 0 -> 0.
    std::uint8_t style_before(std::size_t pos) const {
        if (pos == 0 || text.empty()) {
            return 0;
        }
        const std::size_t at = std::min(pos, text.size()) - 1;
        for (const FormatRun& run : format_runs) {
            if (at >= run.start && at < run.end) {
                return run.style_bits;
            }
        }
        return 0;
    }

    /// Expand runs to a parallel byte string (empty runs => all plain zeros).
    std::string expand_format() const {
        std::string out(text.size(), '\0');
        for (const FormatRun& run : format_runs) {
            const std::size_t begin = std::min(run.start, out.size());
            const std::size_t end = std::min(run.end, out.size());
            for (std::size_t i = begin; i < end; ++i) {
                out[i] = static_cast<char>(run.style_bits);
            }
        }
        return out;
    }

    /// Rebuild runs from a parallel style byte string (full coverage, merge adjacent).
    void compress_format(std::string_view styles) {
        format_runs.clear();
        if (styles.empty()) {
            return;
        }
        bool any_mark = false;
        for (char ch : styles) {
            if (ch != '\0') {
                any_mark = true;
                break;
            }
        }
        if (!any_mark) {
            return;
        }
        std::size_t i = 0;
        while (i < styles.size()) {
            std::size_t j = i + 1;
            while (j < styles.size() && styles[j] == styles[i]) {
                ++j;
            }
            FormatRun run;
            run.start = i;
            run.end = j;
            run.style_bits = static_cast<std::uint8_t>(styles[i]);
            format_runs.push_back(run);
            i = j;
        }
    }

    /// Insert @p next at @p pos; new bytes inherit style_before(pos). Updates text + runs.
    void insert_formatted(std::size_t pos, std::string_view next) {
        if (pos > text.size()) {
            pos = text.size();
        }
        if (next.empty()) {
            focus = pos;
            anchor = focus;
            return;
        }
        const std::uint8_t mark = style_before(pos);
        if (format_runs.empty() && mark == 0) {
            text.insert(pos, next.data(), next.size());
            focus = pos + next.size();
            anchor = focus;
            return;
        }
        std::string styles = expand_format();
        if (styles.size() < text.size()) {
            styles.resize(text.size(), '\0');
        }
        text.insert(pos, next.data(), next.size());
        styles.insert(pos, next.size(), static_cast<char>(mark));
        compress_format(styles);
        focus = pos + next.size();
        anchor = focus;
    }

    /// Erase [start, end); merge adjacent equal style runs. Returns false if empty range.
    bool erase_formatted(std::size_t start, std::size_t end) {
        if (start > text.size()) {
            start = text.size();
        }
        if (end > text.size()) {
            end = text.size();
        }
        if (end < start) {
            end = start;
        }
        if (start == end) {
            return false;
        }
        std::string styles = expand_format();
        if (styles.size() < text.size()) {
            styles.resize(text.size(), '\0');
        }
        text.erase(start, end - start);
        if (styles.size() >= end) {
            styles.erase(start, end - start);
        } else if (styles.size() > start) {
            styles.resize(start);
        }
        compress_format(styles);
        focus = start;
        anchor = focus;
        return true;
    }

    /// Toggle @p mask bits on [from, to). If all set, clear; else set.
    void toggle_style(std::size_t from, std::size_t to, std::uint8_t mask) {
        if (mask == 0 || from >= to || from >= text.size()) {
            return;
        }
        to = std::min(to, text.size());
        std::string styles = expand_format();
        if (styles.size() < text.size()) {
            styles.resize(text.size(), '\0');
        }
        bool all = true;
        for (std::size_t i = from; i < to; ++i) {
            if ((static_cast<std::uint8_t>(styles[i]) & mask) == 0) {
                all = false;
                break;
            }
        }
        for (std::size_t i = from; i < to; ++i) {
            auto bits = static_cast<std::uint8_t>(styles[i]);
            bits = all ? static_cast<std::uint8_t>(bits & ~mask)
                       : static_cast<std::uint8_t>(bits | mask);
            styles[i] = static_cast<char>(bits);
        }
        compress_format(styles);
    }

    /// Load runs from a legacy per-byte format string (migration / seed).
    void load_format_bytes(std::string_view format_bytes) {
        std::string styles(text.size(), '\0');
        const std::size_t n = std::min(format_bytes.size(), styles.size());
        for (std::size_t i = 0; i < n; ++i) {
            styles[i] = format_bytes[i];
        }
        compress_format(styles);
    }
};

}  // namespace input
}  // namespace zui

#endif
