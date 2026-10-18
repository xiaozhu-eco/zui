// The `background` shorthand: its per-layer component tokens and layer parser.

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

bool is_bg_image_token(const std::string& l) {
    if (l == "none") {
        return true;
    }
    if (l.rfind("url(", 0) == 0 || l.rfind("image-set(", 0) == 0 ||
        l.rfind("-webkit-image-set(", 0) == 0 || l.rfind("element(", 0) == 0 ||
        l.rfind("paint(", 0) == 0 || l.rfind("cross-fade(", 0) == 0) {
        return true;
    }
    return l.rfind("linear-gradient(", 0) == 0 || l.rfind("radial-gradient(", 0) == 0 ||
           l.rfind("conic-gradient(", 0) == 0 || l.rfind("repeating-linear-gradient(", 0) == 0 ||
           l.rfind("repeating-radial-gradient(", 0) == 0 ||
           l.rfind("repeating-conic-gradient(", 0) == 0;
}

bool is_bg_repeat_token(const std::string& l) {
    return l == "repeat-x" || l == "repeat-y" || l == "repeat" || l == "space" || l == "round" ||
           l == "no-repeat";
}

bool is_bg_attachment_token(const std::string& l) {
    return l == "scroll" || l == "fixed" || l == "local";
}

bool is_bg_box_token(const std::string& l) {
    return l == "border-box" || l == "padding-box" || l == "content-box";
}

bool is_bg_position_keyword(const std::string& l) {
    return l == "left" || l == "right" || l == "top" || l == "bottom" || l == "center";
}

bool is_bg_size_keyword(const std::string& l) {
    return l == "auto" || l == "cover" || l == "contain";
}

bool is_bg_color_token(const std::string& t, const std::string& l) {
    if (l == "currentcolor" || l == "transparent") {
        return true;
    }
    if (l.rfind("var(", 0) == 0 || l.rfind("env(", 0) == 0 || l.rfind("color-mix(", 0) == 0 ||
        l.rfind("light-dark(", 0) == 0) {
        return true;
    }
    // Hex / named / rgb() / hsl() — only when the shared parser accepts them (`#ggg` is not a colour).
    return parse_css_color(t).has_value();
}

struct BgLayer {
    std::string image;
    std::string position;
    std::string size;
    std::string repeat;
    std::string attachment;
    std::string origin;
    std::string clip;
    std::string color;
    bool has_image = false;
    bool has_color = false;
};

bool parse_bg_layer(std::string_view text, bool is_final, BgLayer& layer) {
    const auto tok = tokens_with_slash(text);
    if (tok.empty()) {
        return false;
    }
    bool saw_origin = false;
    bool saw_second_box = false;
    bool saw_repeat = false;
    bool saw_attachment = false;
    bool saw_position = false;
    bool saw_size = false;
    for (size_t i = 0; i < tok.size();) {
        const std::string& t = tok[i];
        const std::string l = lower_copy(t);
        if (l == "/") {
            return false;
        }
        if (is_bg_image_token(l)) {
            if (layer.has_image) {
                return false;
            }
            layer.image = t;
            layer.has_image = true;
            ++i;
            continue;
        }
        if (is_bg_repeat_token(l)) {
            if (saw_repeat) {
                return false;
            }
            // `repeat-x` / `repeat-y` are complete; otherwise allow a second axis keyword.
            if (l == "repeat-x" || l == "repeat-y") {
                layer.repeat = l;
                saw_repeat = true;
                ++i;
                continue;
            }
            std::string rep = l;
            if (i + 1 < tok.size()) {
                const std::string l2 = lower_copy(tok[i + 1]);
                if (l2 == "repeat" || l2 == "space" || l2 == "round" || l2 == "no-repeat") {
                    rep.push_back(' ');
                    rep += l2;
                    ++i;
                }
            }
            layer.repeat = rep;
            saw_repeat = true;
            ++i;
            continue;
        }
        if (is_bg_attachment_token(l)) {
            if (saw_attachment) {
                return false;
            }
            layer.attachment = l;
            saw_attachment = true;
            ++i;
            continue;
        }
        if (l == "text") {
            layer.clip = "text";
            ++i;
            continue;
        }
        if (is_bg_box_token(l)) {
            if (!saw_origin) {
                layer.origin = l;
                layer.clip = l;
                saw_origin = true;
            } else if (!saw_second_box) {
                layer.clip = l;
                saw_second_box = true;
            } else {
                return false;
            }
            ++i;
            continue;
        }
        if (is_bg_color_token(t, l)) {
            if (!is_final || layer.has_color) {
                return false;
            }
            layer.color = t;
            layer.has_color = true;
            ++i;
            continue;
        }
        if (is_bg_position_keyword(l) || looks_like_length_pct(l)) {
            if (saw_position) {
                return false;
            }
            std::string pos = t;
            size_t j = i + 1;
            int pos_count = 1;
            while (j < tok.size() && pos_count < 4) {
                const std::string& n = tok[j];
                const std::string nl = lower_copy(n);
                if (nl == "/" || is_bg_image_token(nl) || is_bg_repeat_token(nl) ||
                    is_bg_attachment_token(nl) || is_bg_box_token(nl) || nl == "text" ||
                    is_bg_color_token(n, nl) || is_bg_size_keyword(nl)) {
                    break;
                }
                if (!(is_bg_position_keyword(nl) || looks_like_length_pct(nl))) {
                    break;
                }
                pos.push_back(' ');
                pos += n;
                ++pos_count;
                ++j;
            }
            layer.position = pos;
            saw_position = true;
            i = j;
            if (i < tok.size() && tok[i] == "/") {
                ++i;
                if (i >= tok.size() || saw_size) {
                    return false;
                }
                const std::string& s0 = tok[i];
                const std::string s0l = lower_copy(s0);
                if (!(is_bg_size_keyword(s0l) || looks_like_length_pct(s0l))) {
                    return false;
                }
                std::string size = s0;
                ++i;
                if (i < tok.size()) {
                    const std::string& s1 = tok[i];
                    const std::string s1l = lower_copy(s1);
                    if (is_bg_size_keyword(s1l) || looks_like_length_pct(s1l)) {
                        size.push_back(' ');
                        size += s1;
                        ++i;
                    }
                }
                layer.size = size;
                saw_size = true;
            }
            continue;
        }
        return false;
    }
    if (saw_size && !saw_position) {
        return false;
    }
    return true;
}

bool expand_background(std::string_view value, Parts& out) {
    const auto layers_raw = split_comma_top(value);
    std::vector<std::string> layers;
    layers.reserve(layers_raw.size());
    for (const std::string& raw : layers_raw) {
        const std::string layer = trim_copy(raw);
        if (layer.empty()) {
            return false;
        }
        layers.push_back(layer);
    }
    if (layers.empty()) {
        return false;
    }

    // CSS Backgrounds 3 §2.1 / §2.2: each comma layer resets to initial, then takes what that
    // layer wrote. A colour-only final layer sets `background-color` and does not add an image.
    // The first image is the top layer. Omitted components stay at initial for that layer, so
    // `url(a) center, url(b)` is position `center, 0% 0%` rather than one shared `center`.
    struct LayerOut {
        std::string image = "none";
        std::string position = "0% 0%";
        std::string size = "auto";
        std::string repeat = "repeat";
        std::string attachment = "scroll";
        std::string origin = "padding-box";
        std::string clip = "border-box";
    };
    std::vector<LayerOut> painted;
    std::string color = "transparent";

    for (size_t li = 0; li < layers.size(); ++li) {
        const bool is_final = li + 1 == layers.size();
        BgLayer layer;
        if (!parse_bg_layer(layers[li], is_final, layer)) {
            return false;
        }
        if (layer.has_color) {
            color = layer.color;
        }
        const bool has_geom = layer.has_image || !layer.position.empty() || !layer.size.empty() ||
                              !layer.repeat.empty() || !layer.attachment.empty() ||
                              !layer.origin.empty() || !layer.clip.empty();
        // `background: red` is one layer (image none). `background: url(a), red` is one image.
        if (!has_geom && (!painted.empty() || !is_final)) {
            continue;
        }
        LayerOut slot;
        if (layer.has_image) {
            slot.image = layer.image;
        }
        if (!layer.position.empty()) {
            slot.position = layer.position;
        }
        if (!layer.size.empty()) {
            slot.size = layer.size;
        }
        if (!layer.repeat.empty()) {
            slot.repeat = layer.repeat;
        }
        if (!layer.attachment.empty()) {
            slot.attachment = layer.attachment;
        }
        if (!layer.origin.empty()) {
            slot.origin = layer.origin;
        }
        if (!layer.clip.empty()) {
            slot.clip = layer.clip;
        }
        painted.push_back(std::move(slot));
    }
    if (painted.empty()) {
        painted.push_back(LayerOut{});
    }

    auto join_list = [](const std::vector<LayerOut>& slots, auto member) {
        std::string joined;
        for (size_t i = 0; i < slots.size(); ++i) {
            if (i != 0) {
                joined += ", ";
            }
            joined += slots[i].*member;
        }
        return joined;
    };
    add(out, "background-image", join_list(painted, &LayerOut::image));
    add(out, "background-position", join_list(painted, &LayerOut::position));
    add(out, "background-size", join_list(painted, &LayerOut::size));
    add(out, "background-repeat", join_list(painted, &LayerOut::repeat));
    add(out, "background-attachment", join_list(painted, &LayerOut::attachment));
    add(out, "background-origin", join_list(painted, &LayerOut::origin));
    add(out, "background-clip", join_list(painted, &LayerOut::clip));
    add(out, "background-color", color);
    return true;
}

}  // namespace shorthand_detail

}  // namespace css
}  // namespace zui
