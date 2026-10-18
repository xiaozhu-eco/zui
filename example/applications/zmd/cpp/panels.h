#ifndef ZMD_PANELS_H
#define ZMD_PANELS_H

#include <string>

#include "later.h"
#include <string_view>

#include <zui/app.h>

#include "heading.h"
#include "outline_model.h"

namespace zmd {

/// The two floating panels. Reveal itself is CSS (:hover); what lives here is
/// what the pointer cannot express: the pin state, the outline rows, the
/// current heading and the reading progress.
class Panels {
public:
    explicit Panels(zui::Document& page);

    void set_files_pinned(bool on);
    void set_outline_pinned(bool on);
    void toggle_files();
    void toggle_outline();
    bool files_pinned() const { return files_pinned_; }
    bool outline_pinned() const { return outline_pinned_; }

    void set_outline(std::vector<Heading> rows);
    void set_current_heading(std::size_t row);
    void select_heading(std::size_t row);
    /// Reading progress in 0..1, snapped to the sheet's 5% steps (`prog0` … `prog100`): the
    /// surface has no style setter, so the bar's height reads a `--w-progress` class step, the
    /// way the accent swatch and the radius slider already work.
    void set_progress(double ratio);
    void scroll_top();

private:
    zui::Document& page_;
    /// The outline model is re-stamped a frame after a row changes (the click frees the row it
    /// lands on); an owner-guarded defer drops that work when the session is gone.
    Later later_;
    OutlineModel outline_;
    bool files_pinned_ = false;
    bool outline_pinned_ = false;
    /// The `prog*` step currently on the bar; -1 before the first write.
    int progress_step_ = -1;
};

}  // namespace zmd

#endif
