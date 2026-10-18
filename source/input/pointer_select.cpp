#include "input/detail.h"
#include "input/interaction.h"

#include "layout/paint_transform.h"
#include "layout/point.h"
#include "layout/scrollbar_h.h"
#include "layout/scrollbar_metrics.h"
#include "layout/scrollbar_v.h"
#include "layout/viewport.h"
#include "trace/trace.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace zui {
namespace input {

using namespace input_detail;

html::Node* Interaction::press_select_popup(html::Node*& click_node, html::Node*& hit_select) {
    html::Node* select_changed = nullptr;
    html::Node* hit_option = nullptr;
    hit_select = nullptr;
    for (html::Node* cursor = click_node; cursor != nullptr; cursor = cursor->parent) {
        if (html::is_option(cursor) && hit_option == nullptr) {
            hit_option = cursor;
        }
        if (html::is_select(cursor)) {
            hit_select = cursor;
            break;
        }
    }
    if (pointer_.open_select_ != nullptr) {
        if (hit_option != nullptr && hit_option->parent == pointer_.open_select_ && !hit_option->disabled) {
            html::Node* select = pointer_.open_select_;
            html::select_commit(select, hit_option);
            close_select_popup();
            note_select_geometry(select);
            select_changed = select;
            hit_select = select;
            click_node = select;
        } else if (hit_select == pointer_.open_select_ && hit_option == nullptr) {
            close_select_popup();
        } else {
            html::Node* previous = pointer_.open_select_;
            close_select_popup();
            if (hit_select != nullptr && hit_select != previous && !hit_select->disabled) {
                open_select_popup(hit_select);
            }
        }
    } else if (hit_select != nullptr && !hit_select->disabled && hit_option == nullptr) {
        open_select_popup(hit_select);
    }
    return select_changed;
}

}  // namespace input
}  // namespace zui
