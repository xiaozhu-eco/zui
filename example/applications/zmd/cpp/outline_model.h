#ifndef ZMD_OUTLINE_MODEL_H
#define ZMD_OUTLINE_MODEL_H

#include <string>
#include <string_view>
#include <vector>

#include <zui/model.h>

#include "heading.h"

namespace zmd {

/// Rows of the outline panel. The page stamps one button per row from its
/// <template>; this class owns the data and which row is current.
class OutlineModel : public zui::ListModel {
public:
    void set_rows(std::vector<Heading> rows);
    void set_active(std::size_t row);
    const std::vector<Heading>& rows() const { return rows_; }
    std::size_t active() const { return active_; }

    std::size_t row_count() const override;
    std::string value(std::size_t row, std::string_view key) const override;

private:
    std::vector<Heading> rows_;
    std::size_t active_ = 0;
};

}  // namespace zmd

#endif
