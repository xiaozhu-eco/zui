#ifndef ZUI_ELEMENT_BOX_H
#define ZUI_ELEMENT_BOX_H

namespace zui {

/// CSSOM View geometry for one element, in CSS px with the viewport origin at
/// the top-left. `border` matches `getBoundingClientRect()`; `client_*` matches
/// `clientWidth` / `clientHeight` (content box plus padding); `scroll_*` matches
/// `scrollWidth` / `scrollHeight` (the scrollable overflow extent). `content_*`
/// is the content box; CSSOM exposes it only through padding and box-sizing.
struct ElementBox {
    struct Rect {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
    };

    bool laid = false;
    Rect border;
    float content_width = 0.0f;
    float content_height = 0.0f;
    float client_width = 0.0f;
    float client_height = 0.0f;
    float scroll_width = 0.0f;
    float scroll_height = 0.0f;
};

}  // namespace zui

#endif  // ZUI_ELEMENT_BOX_H
