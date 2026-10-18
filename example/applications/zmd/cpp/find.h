#ifndef ZMD_FIND_H
#define ZMD_FIND_H

#include <string>
#include <string_view>
#include <vector>

#include <zui/app.h>

#include "later.h"

namespace zmd {

class Document;
class Editor;

/// Find and replace over the open document. The query lives here; the text lives
/// in Document, so this class owns no copy of it.
///
/// A query is scanned once into the hit offsets. The cursor is an index into that
/// list, which is what the counter shows (`3 / 7`) - not a byte offset, which is a
/// different number and would print as a wrong count.
class Find {
public:
    Find(zui::Document& page, Document& doc, Editor& editor);

    void open(bool with_replace);
    void close();
    bool is_open() const { return open_; }
    bool replacing() const { return replace_; }

    void set_query(std::string_view query);
    void set_replacement(std::string_view text);

    /// Moves to the next / previous match and selects it.
    bool next();
    bool previous();
    bool replace_one();
    int replace_all();

    int matches() const { return matches_; }
    /// 1-based number of the current hit, 0 when there is no match.
    int ordinal() const { return hits_.empty() ? 0 : static_cast<int>(cursor_) + 1; }
    /// The query and the hit offsets, so the preview can mark what the counter counted.
    const std::string& query() const { return query_; }
    const std::vector<std::size_t>& hits() const { return hits_; }

private:
    void recount();
    void report() const;
    /// Moves the cursor and selects the hit under it.
    bool select_cursor();

    zui::Document& page_;
    /// Focus waits a frame (the bar is `display: none` until the class lands); a session that dies
    /// before then leaves the callback queued, so it goes through the owner-guarded defer.
    Later later_;
    Document& doc_;
    Editor& editor_;
    std::string query_;
    std::string replacement_;
    std::vector<std::size_t> hits_;
    std::size_t cursor_ = 0;
    int matches_ = 0;
    bool open_ = false;
    bool replace_ = false;
};

}  // namespace zmd

#endif
