// The transition shorthand timing list: string helpers, tokenizers and the parsed list of
// property names, durations, delays and easings (CSS Transitions 1 section 2).

#include "css/transition/transition.h"
#include "css/transition/internal.h"

#include "css/values.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

namespace zui {
namespace css {

using namespace transition_detail;

namespace transition_detail {

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.remove_suffix(1);
    }
    return s;
}

std::string lower(std::string_view s) {
    std::string o(s);
    for (char& c : o) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return o;
}

const std::string* map_get(const StyleMap* m, const char* k) {
    if (m == nullptr) {
        return nullptr;
    }
    auto it = m->find(k);
    if (it == m->end() || it->second.empty()) {
        return nullptr;
    }
    return it->second.ptr();
}

const std::string* node_get(const html::Node* n, const char* k) {
    return n == nullptr ? nullptr : n->style(k);
}

float parse_time(std::string_view tok) {
    tok = trim(tok);
    if (tok.empty()) {
        return 0;
    }
    std::string s = lower(tok);
    float mul = 1;
    if (s.size() > 2 && s.substr(s.size() - 2) == "ms") {
        mul = 0.001f;
        s.resize(s.size() - 2);
    } else if (!s.empty() && s.back() == 's' && (s.size() < 2 || s[s.size() - 2] != 'm')) {
        s.pop_back();
    }
    try {
        return std::stof(s) * mul;
    } catch (...) {
        return 0;
    }
}

bool is_time_token(std::string_view tok) {
    std::string s = lower(trim(tok));
    if (s.size() > 2 && s.substr(s.size() - 2) == "ms") {
        return true;
    }
    if (!s.empty() && s.back() == 's' && s.find("ease") == std::string::npos) {
        return true;
    }
    return false;
}

std::vector<std::string_view> split_comma(std::string_view s) {
    std::vector<std::string_view> out;
    int depth = 0;
    size_t start = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '(') {
            ++depth;
        } else if (s[i] == ')') {
            --depth;
        } else if (s[i] == ',' && depth == 0) {
            out.push_back(trim(s.substr(start, i - start)));
            start = i + 1;
        }
    }
    out.push_back(trim(s.substr(start)));
    return out;
}

std::vector<std::string> split_tokens(std::string_view s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) {
            ++i;
        }
        if (i >= s.size()) {
            break;
        }
        if (s[i] == ',') {
            ++i;
            continue;
        }
        const size_t b = i;
        if (s[i] == '(') {
            int depth = 0;
            while (i < s.size()) {
                if (s[i] == '(') {
                    ++depth;
                } else if (s[i] == ')') {
                    --depth;
                    ++i;
                    if (depth == 0) {
                        break;
                    }
                    continue;
                }
                ++i;
            }
        } else {
            while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i])) && s[i] != ',') {
                if (s[i] == '(') {
                    int depth = 0;
                    while (i < s.size()) {
                        if (s[i] == '(') {
                            ++depth;
                        } else if (s[i] == ')') {
                            --depth;
                            ++i;
                            if (depth == 0) {
                                break;
                            }
                            continue;
                        }
                        ++i;
                    }
                    break;
                }
                ++i;
            }
        }
        out.emplace_back(s.substr(b, i - b));
    }
    return out;
}

void parse_shorthand_item(std::string_view item, Timing& t) {
    auto toks = split_tokens(item);
    std::string prop = "all";
    float dur = 0;
    float delay = 0;
    Easing ease = parse_easing("ease");
    int times = 0;
    for (const std::string& tok : toks) {
        std::string ls = lower(tok);
        if (is_easing_token(ls)) {
            ease = parse_easing(tok);
            continue;
        }
        if (ls == "none" || ls == "all" || ls.find('-') != std::string::npos || ls == "opacity" ||
            ls == "transform" || ls == "color") {
            prop = ls;
            continue;
        }
        if (is_time_token(tok)) {
            if (times == 0) {
                dur = parse_time(tok);
            } else {
                delay = parse_time(tok);
            }
            ++times;
            continue;
        }
        prop = ls;
    }
    t.props.push_back(std::move(prop));
    t.durations.push_back(dur);
    t.delays.push_back(delay);
    t.eases.push_back(ease);
}

Timing parse_timing(const StyleMap* m) {
    Timing t;
    if (const std::string* sh = map_get(m, "transition")) {
        for (std::string_view item : split_comma(*sh)) {
            if (item.empty()) {
                continue;
            }
            parse_shorthand_item(item, t);
        }
    }
    if (const std::string* p = map_get(m, "transition-property")) {
        t.props.clear();
        for (std::string_view item : split_comma(*p)) {
            t.props.emplace_back(lower(item));
        }
    }
    if (const std::string* d = map_get(m, "transition-duration")) {
        t.durations.clear();
        for (std::string_view item : split_comma(*d)) {
            t.durations.push_back(parse_time(item));
        }
    }
    if (const std::string* d = map_get(m, "transition-delay")) {
        t.delays.clear();
        for (std::string_view item : split_comma(*d)) {
            t.delays.push_back(parse_time(item));
        }
    }
    if (const std::string* e = map_get(m, "transition-timing-function")) {
        t.eases.clear();
        for (std::string_view item : split_comma(*e)) {
            t.eases.push_back(parse_easing(item));
        }
    }
    return t;
}

/// Shorthands that expand to longhands for transition-property matching (CSS Transitions).
bool transition_prop_matches(std::string_view declared, std::string_view longhand) {
    if (declared == "all" || declared == longhand) {
        return true;
    }
    if (declared == "border-color" && (longhand == "border-top-color" ||
                                       longhand == "border-right-color" ||
                                       longhand == "border-bottom-color" ||
                                       longhand == "border-left-color")) {
        return true;
    }
    if (declared == "margin" && (longhand == "margin-top" || longhand == "margin-right" ||
                                 longhand == "margin-bottom" || longhand == "margin-left")) {
        return true;
    }
    if (declared == "padding" && (longhand == "padding-top" || longhand == "padding-right" ||
                                  longhand == "padding-bottom" || longhand == "padding-left")) {
        return true;
    }
    return false;
}

int prop_index(const Timing& t, std::string_view name) {
    if (t.props.empty()) {
        return 0;
    }
    for (size_t i = 0; i < t.props.size(); ++i) {
        if (t.props[i] == "none") {
            return -1;
        }
        if (transition_prop_matches(t.props[i], name)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

float at_or(const std::vector<float>& v, int i, float fallback) {
    if (v.empty() || i < 0) {
        return fallback;
    }
    return v[static_cast<size_t>(i) % v.size()];
}

Easing ease_at(const Timing& t, int i) {
    if (t.eases.empty() || i < 0) {
        return parse_easing("ease");
    }
    return t.eases[static_cast<size_t>(i) % t.eases.size()];
}

}  // namespace transition_detail

}  // namespace css
}  // namespace zui
