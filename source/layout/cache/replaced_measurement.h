#ifndef ZUI_LAYOUT_CACHE_REPLACED_MEASUREMENT_H
#define ZUI_LAYOUT_CACHE_REPLACED_MEASUREMENT_H

namespace zui {
namespace layout {

struct ReplacedMeasurement {
    bool has_intrinsic_width = false;
    bool has_intrinsic_height = false;
    bool has_intrinsic_ratio = false;
    float intrinsic_width = 0;
    float intrinsic_height = 0;
    float intrinsic_ratio = 0;
    bool resource_ready = false;
    // The src was empty or the file did not decode. Distinct from "not ready yet": a failed
    // image has no intrinsic size, so an empty alt does not keep the 300x150 default object size.
    bool failed = false;
};

}  // namespace layout
}  // namespace zui

#endif
