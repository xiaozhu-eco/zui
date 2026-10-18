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

bool expand_shorthand(const lxb_css_rule_declaration* decl, Parts& out) {
    if (decl == nullptr) {
        return false;
    }
    const auto* d = reinterpret_cast<const lxb_css_rule_declaration_t*>(decl);
    if (d->type == LXB_CSS_PROPERTY__UNDEF && d->u.undef != nullptr) {
        const lxb_css_entry_data_t* entry = lxb_css_property_by_id(d->u.undef->type);
        std::string name;
        if (entry != nullptr && entry->name != nullptr) {
            name.assign(reinterpret_cast<const char*>(entry->name), entry->length);
        }
        std::string_view value;
        if (d->u.undef->value.data != nullptr) {
            value = {reinterpret_cast<const char*>(d->u.undef->value.data),
                     d->u.undef->value.length};
        }
        if (!name.empty() && expand_custom(name, value, out)) {
            return true;
        }
        return false;
    }
    if (d->type == LXB_CSS_PROPERTY__CUSTOM && d->u.custom != nullptr &&
        d->u.custom->name.data != nullptr) {
        const std::string name(reinterpret_cast<const char*>(d->u.custom->name.data),
                               d->u.custom->name.length);
        std::string_view value;
        if (d->u.custom->value.data != nullptr) {
            value = {reinterpret_cast<const char*>(d->u.custom->value.data),
                     d->u.custom->value.length};
        }
        return expand_custom(name, value, out);
    }
    switch (d->type) {
        case LXB_CSS_PROPERTY_MARGIN: {
            const lxb_css_value_length_percentage_t sides[4] = {
                d->u.margin->top, d->u.margin->right, d->u.margin->bottom, d->u.margin->left};
            const char* names[4] = {"margin-top", "margin-right", "margin-bottom", "margin-left"};
            expand_edges(sides, names, out);
            return true;
        }
        case LXB_CSS_PROPERTY_PADDING: {
            const lxb_css_value_length_percentage_t sides[4] = {
                d->u.padding->top, d->u.padding->right, d->u.padding->bottom, d->u.padding->left};
            const char* names[4] = {"padding-top", "padding-right", "padding-bottom",
                                    "padding-left"};
            expand_edges(sides, names, out);
            return true;
        }
        case LXB_CSS_PROPERTY_BORDER:
            expand_border_all(*d->u.border, out);
            return true;
        case LXB_CSS_PROPERTY_BORDER_TOP:
            expand_border_side("top", *d->u.border_top, out);
            return true;
        case LXB_CSS_PROPERTY_BORDER_RIGHT:
            expand_border_side("right", *d->u.border_right, out);
            return true;
        case LXB_CSS_PROPERTY_BORDER_BOTTOM:
            expand_border_side("bottom", *d->u.border_bottom, out);
            return true;
        case LXB_CSS_PROPERTY_BORDER_LEFT:
            expand_border_side("left", *d->u.border_left, out);
            return true;
        case LXB_CSS_PROPERTY_FLEX:
            expand_flex(*d->u.flex, out);
            return true;
        case LXB_CSS_PROPERTY_FLEX_FLOW:
            add(out, "flex-direction", ser_ident(d->u.flex_flow->type_direction, "row"));
            add(out, "flex-wrap", ser_ident(d->u.flex_flow->wrap, "nowrap"));
            return true;
        default:
            return false;
    }
}

bool known_shorthand_name(std::string_view name) {
    return name == "margin" || name == "padding" || name == "border" || name == "border-top" ||
           name == "border-right" || name == "border-bottom" || name == "border-left" ||
           name == "border-width" || name == "border-style" || name == "border-color" ||
           name == "overflow" || name == "gap" || name == "inset" || name == "background" ||
           name == "font" || name == "border-radius" || name == "grid-template" || name == "grid" ||
           name == "list-style" || name == "place-items" || name == "place-content" ||
           name == "place-self" || name == "border-image" || name == "container" || name == "all";
}

bool deferred_shorthand(const lxb_css_rule_declaration* decl, std::string& name,
                        std::string& value) {
    if (decl == nullptr) {
        return false;
    }
    const auto* d = reinterpret_cast<const lxb_css_rule_declaration_t*>(decl);
    if (d->type != LXB_CSS_PROPERTY__UNDEF || d->u.undef == nullptr) {
        return false;
    }
    const lxb_css_entry_data_t* entry = lxb_css_property_by_id(d->u.undef->type);
    if (entry == nullptr || entry->name == nullptr || entry->length == 0) {
        return false;
    }
    name.assign(reinterpret_cast<const char*>(entry->name), entry->length);
    for (char& c : name) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (!known_shorthand_name(name)) {
        return false;
    }
    value.clear();
    if (d->u.undef->value.data != nullptr && d->u.undef->value.length > 0) {
        value.assign(reinterpret_cast<const char*>(d->u.undef->value.data),
                     d->u.undef->value.length);
    }
    return contains_substitution(value);
}

bool expand_shorthand_value(std::string name, std::string_view value, Parts& out) {
    return expand_custom(std::move(name), value, out);
}

bool expand_flex_text(std::string_view value, Parts& out) {
    return shorthand_detail::expand_flex_text(value, out);
}

}  // namespace css
}  // namespace zui
