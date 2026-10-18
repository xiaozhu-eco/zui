#ifndef ZUI_LAYOUT_CACHE_EXCLUSION_SPACE_H
#define ZUI_LAYOUT_CACHE_EXCLUSION_SPACE_H

#include "layout/cache/float_record.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace zui {
namespace layout {

class ExclusionSpace {
public:
    ExclusionSpace();
    explicit ExclusionSpace(std::vector<FloatRecord> bands);

    const std::vector<FloatRecord>& bands() const { return bands_; }
    std::uint64_t hash() const { return hash_; }
    bool equal(const ExclusionSpace& other) const;

    // New snapshot. The previous object stays unchanged.
    static std::shared_ptr<const ExclusionSpace> append(const std::shared_ptr<const ExclusionSpace>& base,
                                                         FloatRecord band);
    // Same pointer when `extra` is empty. Otherwise a new snapshot, base unchanged.
    static std::shared_ptr<const ExclusionSpace> adopt(const std::shared_ptr<const ExclusionSpace>& base,
                                                        const std::vector<FloatRecord>& extra);

private:
    std::vector<FloatRecord> bands_;
    std::uint64_t hash_ = 0;
};

}  // namespace layout
}  // namespace zui

#endif
