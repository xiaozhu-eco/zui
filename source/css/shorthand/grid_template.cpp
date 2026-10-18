// The `grid-template` / `grid` shorthands (CSS Grid section 7.4).

#include "css/shorthand/shorthand.h"
#include "css/shorthand/internal.h"

#include "css/values.h"

#include <lexbor/css/css.h>

#include <cctype>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace zui {
namespace css {

using namespace shorthand_detail;

namespace shorthand_detail {

// The `grid-template` / `grid` shorthands set the explicit template, and `grid` also resets the
// implicit axis and the auto flow (CSS Grid section 7.4). Only the longhands the layout layer
// reads are emitted.
bool expand_grid_template(bool with_auto, std::string_view value, Parts& out) {
    const auto sides = split_slash(value);
    const std::string left = trim_copy(sides[0]);
    const std::string right = sides.size() > 1 ? trim_copy(sides[1]) : std::string();
    auto reset_implicit = [&]() {
        if (!with_auto) {
            return;
        }
        add(out, "grid-auto-rows", "auto");
        add(out, "grid-auto-columns", "auto");
        add(out, "grid-auto-flow", "row");
    };
    if (lower_copy(left) == "none" && right.empty()) {
        add(out, "grid-template-areas", "none");
        add(out, "grid-template-rows", "none");
        add(out, "grid-template-columns", "none");
        reset_implicit();
        return true;
    }
    // A string in the first part selects the areas form:
    //   [ <line-names>? <string> <track-size>? <line-names>? ]+ [ / <explicit-track-list> ]?
    // A string with no size leaves that row `auto`; the names around a string belong to the rows.
    const auto left_tokens = grid_tokens(left);
    bool has_string = false;
    for (const std::string& token : left_tokens) {
        if (!token.empty() && (token.front() == '"' || token.front() == '\'')) {
            has_string = true;
        }
    }
    if (has_string) {
        std::string rows;
        std::string areas;
        bool pending_size = false;
        for (const std::string& token : left_tokens) {
            if (token.empty()) {
                continue;
            }
            if (token.front() == '[') {
                if (pending_size) {
                    rows += "auto ";
                    pending_size = false;
                }
                rows += token + " ";
            } else if (token.front() == '"' || token.front() == '\'') {
                if (pending_size) {
                    rows += "auto ";
                }
                if (!areas.empty()) {
                    areas.push_back(' ');
                }
                areas += token;
                pending_size = true;
            } else {
                rows += token + " ";
                pending_size = false;
            }
        }
        if (pending_size) {
            rows += "auto ";
        }
        add(out, "grid-template-areas", areas);
        add(out, "grid-template-rows", trim_copy(rows));
        if (!right.empty()) {
            add(out, "grid-template-columns", right);
        }
        reset_implicit();
        return true;
    }
    // `[ auto-flow && dense? ] <grid-auto-rows>? / <grid-template-columns>` and its mirror.
    auto contains = [](const std::string& haystack, const char* needle) {
        return lower_copy(haystack).find(needle) != std::string::npos;
    };
    if (contains(left, "auto-flow")) {
        if (right.empty()) {
            return false;
        }
        std::string rows;
        bool dense = false;
        for (const std::string& token : left_tokens) {
            const std::string low = lower_copy(token);
            if (low == "auto-flow") {
                continue;
            }
            if (low == "dense") {
                dense = true;
                continue;
            }
            rows += token + " ";
        }
        add(out, "grid-template-areas", "none");
        add(out, "grid-template-rows", "none");
        add(out, "grid-template-columns", right);
        add(out, "grid-auto-rows", rows.empty() ? "auto" : trim_copy(rows));
        add(out, "grid-auto-columns", "auto");
        // `auto-flow` on the left names the implicit axis and keeps the flow along rows.
        add(out, "grid-auto-flow", dense ? "row dense" : "row");
        return true;
    }
    if (contains(right, "auto-flow")) {
        if (left.empty()) {
            return false;
        }
        std::string columns;
        bool dense = false;
        for (const std::string& token : grid_tokens(right)) {
            const std::string low = lower_copy(token);
            if (low == "auto-flow") {
                continue;
            }
            if (low == "dense") {
                dense = true;
                continue;
            }
            columns += token + " ";
        }
        add(out, "grid-template-areas", "none");
        add(out, "grid-template-columns", "none");
        add(out, "grid-template-rows", left);
        add(out, "grid-auto-columns", columns.empty() ? "auto" : trim_copy(columns));
        add(out, "grid-auto-rows", "auto");
        add(out, "grid-auto-flow", dense ? "column dense" : "column");
        return true;
    }
    if (right.empty()) {
        return false;
    }
    // `<grid-template-rows> / <grid-template-columns>`.
    add(out, "grid-template-areas", "none");
    add(out, "grid-template-rows", left);
    add(out, "grid-template-columns", right);
    reset_implicit();
    return true;
}

}  // namespace shorthand_detail

}  // namespace css
}  // namespace zui
