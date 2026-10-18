#ifndef ZMD_CHROME_H
#define ZMD_CHROME_H

#include <string_view>

#include <zui/app.h>

#include "document.h"

namespace zmd {

/// The two rows around the sheet: the title row (file name, save dot) and the
/// status row (counts). Also owns the typing veil, which is one timer.
class Chrome {
public:
    explicit Chrome(zui::Document& page);
    ~Chrome();

    void set_title(std::string_view title);
    void set_counts(int words, int lines);
    void set_save_state(Document::Save state);
    /// A keystroke happened: breathe the save dot, drop the veil for a moment.
    void pulse();
    /// Drops the veil and its timer, so a page reloaded under a warm session starts clean: the
    /// class and the flag have to move together, or the next keystroke's veil is lost.
    void reset();
    /// Window lost or gained focus.
    void set_blurred(bool blurred);

private:
    zui::Document& page_;
    Document::Save shown_ = Document::Save::Saved;
    bool veiled_ = false;
    zui::TimerId veil_ = 0;
};

}  // namespace zmd

#endif
