#ifndef ZUI_LAYOUT_CACHE_MARGIN_STRUT_H
#define ZUI_LAYOUT_CACHE_MARGIN_STRUT_H

namespace zui {
namespace layout {

struct MarginStrut {
    float start = 0;
    float end = 0;
    bool adjoining = false;
    bool blocks_margin_collapse = false;
    // Clearance separated this box from the adjoining margin above it.
    bool clearance = false;
};

}  // namespace layout
}  // namespace zui

#endif
