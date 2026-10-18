#ifndef ZMD_MARKDOWN_H
#define ZMD_MARKDOWN_H

#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "heading.h"

namespace zmd {

/// `[start, end)` of one find hit, byte offsets into the source it renders. Sorted and
/// non-overlapping, which is what the inline pass binary-searches while it emits text.
using MarkRanges = std::vector<std::pair<std::size_t, std::size_t>>;

/// One block's visible text, byte by byte, back to the source byte that emitted it.
/// `at.size()` is the visible UTF-8 length. `after` is the source offset just past the
/// last visible byte (a click past the line's last glyph).
struct VisibleSource {
    std::vector<std::size_t> at;
    std::size_t after = 0;
};

/// Keyed by the block id the preview names (`mdb0`, `mdh0`).
using VisibleSources = std::unordered_map<std::string, VisibleSource>;

/// Markdown in, preview fragment and document facts out.
/// Pure: no state, no IO, no zui types, so it is testable on its own.
class Markdown {
public:
    std::vector<Heading> headings(std::string_view source) const;
    /// @p caret, when not npos, plants the writing caret in the block that holds it and marks
    /// that block's top-level element with `mdw-cur` - the class 聚焦模式 keeps lit while the
    /// sheet dims the rest.
    /// @p base, when not empty, resolves a relative image or link target against it: the preview
    /// is a scratch file outside the document's own directory, so `assets/pic.png` cannot reach
    /// the file the document means. The export passes no base and keeps the portable form.
    /// @p marks, when not null, wraps each hit in `<mark class="mdw-f-mark">` (the sheet's find
    /// mark) so the writing view shows what the find bar counted. A caret or a selection is not
    /// written into this HTML: the paper paints those from Document Selection and glyph edges.
    /// @p counts, when not null, receives how many `mdh{index}` headings and `mdb{index}` blocks
    /// the pass named. The ids are contiguous from zero, so a scan over the rendered page can stop
    /// there instead of at a fixed bound (or at the stale nodes a shorter render left behind).
    struct RenderCounts {
        int headings = 0;
        int blocks = 0;
    };
    std::string render(std::string_view source, std::size_t caret = static_cast<std::size_t>(-1),
                       std::string_view base = {}, const MarkRanges* marks = nullptr,
                       RenderCounts* counts = nullptr, VisibleSources* sources = nullptr) const;
    std::pair<int, int> counts(std::string_view source) const;

private:
    /// @p abs is where @p text starts in the source: the mark ranges are absolute, and the
    /// emitters below hand `inline_text` sub-views of the line they are walking.
    std::string inline_text(std::string_view text, std::string_view base, std::size_t abs,
                            const MarkRanges* marks, std::vector<std::size_t>* vis,
                            std::size_t* vis_after) const;
};

}  // namespace zmd

#endif
