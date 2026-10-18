#ifndef ZMD_PALETTE_MODEL_H
#define ZMD_PALETTE_MODEL_H

#include <string>
#include <string_view>
#include <vector>

#include <zui/model.h>

#include "command.h"

namespace zmd {

/// Palette rows. Filtering happens here, not in the page, so both hosts agree on
/// what a query matches.
class PaletteModel : public zui::ListModel {
public:
    void set_items(std::vector<Command> items);
    void set_active(std::size_t row);
    /// Case-insensitive substring match on the label.
    void filter(std::string_view query);
    const Command* at(std::size_t row) const;
    std::size_t visible_count() const { return visible_.size(); }

    std::size_t row_count() const override;
    std::string value(std::size_t row, std::string_view key) const override;

private:
    std::vector<Command> all_;
    std::vector<Command> visible_;
    std::size_t active_ = 0;
};

}  // namespace zmd

#endif
