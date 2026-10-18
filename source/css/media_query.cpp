#include "css/values.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

#include "css/values_internal.h"
#include "css/media_internal.h"

namespace zui {
namespace css {

namespace values_detail {

bool is_ident_char(unsigned char c) { return std::isalnum(c) || c == '_' || c == '-'; }

char lower_char(unsigned char c) { return static_cast<char>(std::tolower(c)); }

std::string lower_copy(std::string_view s) {
    std::string o(s);
    for (char& c : o) {
        c = lower_char(static_cast<unsigned char>(c));
    }
    return o;
}

float absolute_px(float num, std::string_view unit) {
    if (unit.empty() || unit == "px") {
        return num;
    }
    if (unit == "em" || unit == "rem") {
        // Media Queries: relative units use the initial font size, not the root computed size.
        return num * 16.0f;
    }
    if (unit == "pt") {
        return num * (96.0f / 72.0f);
    }
    if (unit == "pc") {
        return num * 16.0f;
    }
    if (unit == "in") {
        return num * 96.0f;
    }
    if (unit == "cm") {
        return num * (96.0f / 2.54f);
    }
    if (unit == "mm") {
        return num * (96.0f / 25.4f);
    }
    // 1Q = 0.25mm (CSS Values). 4Q is one millimeter.
    if (unit == "q") {
        return num * (96.0f / 25.4f / 4.0f);
    }
    return std::numeric_limits<float>::quiet_NaN();
}

bool parse_mq_length(Scan& sc, const MediaState& media, float& px) {
    sc.skip();
    const size_t start = sc.i;
    if (sc.i < sc.s.size() && (sc.s[sc.i] == '+' || sc.s[sc.i] == '-')) {
        ++sc.i;
    }
    const size_t digits = sc.i;
    while (sc.i < sc.s.size() &&
           (std::isdigit(static_cast<unsigned char>(sc.s[sc.i])) || sc.s[sc.i] == '.')) {
        ++sc.i;
    }
    if (sc.i == digits) {
        sc.i = start;
        return false;
    }
    float num = 0;
    try {
        num = std::stof(std::string(sc.s.substr(start, sc.i - start)));
    } catch (...) {
        return false;
    }
    const size_t us = sc.i;
    while (sc.i < sc.s.size() && std::isalpha(static_cast<unsigned char>(sc.s[sc.i]))) {
        ++sc.i;
    }
    const std::string unit = lower_copy(sc.s.substr(us, sc.i - us));
    if (unit == "vw" || unit == "dvw" || unit == "svw" || unit == "lvw" || unit == "vi") {
        px = num * 0.01f * media.width;
        return true;
    }
    if (unit == "vh" || unit == "dvh" || unit == "svh" || unit == "lvh" || unit == "vb") {
        px = num * 0.01f * media.height;
        return true;
    }
    if (unit == "vmin") {
        px = num * 0.01f * std::min(media.width, media.height);
        return true;
    }
    if (unit == "vmax") {
        px = num * 0.01f * std::max(media.width, media.height);
        return true;
    }
    px = absolute_px(num, unit);
    if (unit.empty() && num != 0.0f) {
        return false;
    }
    return !std::isnan(px);
}

// `<integer>` for `(color)` / `(monochrome)`. A unit or a fraction is not an integer.
bool parse_mq_integer(std::string_view text, float& out) {
    if (text.empty()) {
        return false;
    }
    const std::string tmp(text);
    char* end = nullptr;
    const float num = std::strtof(tmp.c_str(), &end);
    if (end == tmp.c_str() || *end != '\0' || num < 0.f) {
        return false;
    }
    if (std::fabs(num - std::round(num)) > 0.001f) {
        return false;
    }
    out = num;
    return true;
}


struct Mq {
    Scan sc;
    const MediaState& media;
    bool failed = false;

    bool media_in_parens(bool& value);
    bool media_condition(bool allow_or, bool& value);
    bool media_query(bool& value);
    bool media_query_list();
};

bool mf_lt_family(MfCmp cmp) { return cmp == MfCmp::Lt || cmp == MfCmp::Le; }

bool read_mf_cmp(Scan& sc, MfCmp& cmp) {
    sc.skip();
    if (sc.i >= sc.s.size()) {
        return false;
    }
    const char c = sc.s[sc.i];
    if (c == '<') {
        ++sc.i;
        if (sc.i < sc.s.size() && sc.s[sc.i] == '=') {
            ++sc.i;
            cmp = MfCmp::Le;
        } else {
            cmp = MfCmp::Lt;
        }
        return true;
    }
    if (c == '>') {
        ++sc.i;
        if (sc.i < sc.s.size() && sc.s[sc.i] == '=') {
            ++sc.i;
            cmp = MfCmp::Ge;
        } else {
            cmp = MfCmp::Gt;
        }
        return true;
    }
    if (c == '=') {
        ++sc.i;
        cmp = MfCmp::Eq;
        return true;
    }
    return false;
}


bool parse_signed_number(Scan& sc, float& num) {
    sc.skip();
    const size_t start = sc.i;
    if (sc.i < sc.s.size() && (sc.s[sc.i] == '+' || sc.s[sc.i] == '-')) {
        ++sc.i;
    }
    const size_t digits = sc.i;
    while (sc.i < sc.s.size() &&
           (std::isdigit(static_cast<unsigned char>(sc.s[sc.i])) || sc.s[sc.i] == '.')) {
        ++sc.i;
    }
    if (sc.i == digits) {
        sc.i = start;
        return false;
    }
    try {
        num = std::stof(std::string(sc.s.substr(start, sc.i - start)));
    } catch (...) {
        return false;
    }
    return true;
}

// A range bound is a length (`700px`), a ratio (`16/9`), or a bare number (`1`, meaning 1/1
// on aspect-ratio and an invalid length on width).

bool resolution_unit_dppx(float num, std::string_view unit, float& dppx) {
    if (!(num >= 0.0f)) {
        return false;
    }
    if (unit == "dppx" || unit == "x") {
        dppx = num;
        return true;
    }
    if (unit == "dpi") {
        dppx = num / 96.0f;
        return true;
    }
    if (unit == "dpcm") {
        dppx = num * 2.54f / 96.0f;
        return true;
    }
    return false;
}

bool parse_mf_bound(Scan& sc, const MediaState& media, MfBound& bound) {
    sc.skip();
    const size_t start = sc.i;
    float num = 0.f;
    if (!parse_signed_number(sc, num)) {
        return false;
    }
    const size_t after_num = sc.i;
    sc.skip();
    if (sc.i < sc.s.size() && sc.s[sc.i] == '/') {
        if (num < 0.0f) {
            return false;
        }
        ++sc.i;
        float den = 0.f;
        if (!parse_signed_number(sc, den) || !(den > 0.0f)) {
            return false;
        }
        sc.skip();
        if (sc.i < sc.s.size() && std::isalpha(static_cast<unsigned char>(sc.s[sc.i]))) {
            return false;
        }
        bound.kind = MfBound::Kind::Ratio;
        bound.num = num;
        bound.den = den;
        return true;
    }
    // A resolution unit is a dimension token: no space between the number and `dppx` / `x` / `dpi`.
    sc.i = after_num;
    if (sc.i < sc.s.size() && std::isalpha(static_cast<unsigned char>(sc.s[sc.i]))) {
        const size_t us = sc.i;
        while (sc.i < sc.s.size() && std::isalpha(static_cast<unsigned char>(sc.s[sc.i]))) {
            ++sc.i;
        }
        const std::string unit = lower_copy(sc.s.substr(us, sc.i - us));
        float dppx = 0.f;
        if (resolution_unit_dppx(num, unit, dppx)) {
            bound.kind = MfBound::Kind::Resolution;
            bound.px = dppx;
            return true;
        }
    }
    sc.i = start;
    float px = 0.f;
    if (parse_mq_length(sc, media, px)) {
        bound.kind = MfBound::Kind::Length;
        bound.px = px;
        // Unitless 0 is a length on width/height and the ratio 0/1 on aspect-ratio.
        // `0px` keeps the length kind because it has a unit.
        if (px == 0.0f) {
            bool unit = false;
            for (size_t k = start; k < sc.i; ++k) {
                if (std::isalpha(static_cast<unsigned char>(sc.s[k]))) {
                    unit = true;
                    break;
                }
            }
            if (!unit) {
                bound.kind = MfBound::Kind::Bare;
                bound.num = 0.0f;
                bound.den = 1.0f;
            }
        }
        return true;
    }
    sc.i = start;
    if (!parse_signed_number(sc, num) || num < 0.0f) {
        return false;
    }
    sc.skip();
    if (sc.i < sc.s.size() && std::isalpha(static_cast<unsigned char>(sc.s[sc.i]))) {
        return false;
    }
    bound.kind = MfBound::Kind::Bare;
    bound.num = num;
    bound.den = 1.f;
    return true;
}

bool parse_ratio_text(std::string_view text, float& num, float& den) {
    MediaState unused;
    Scan sc{text, 0};
    MfBound bound;
    if (!parse_mf_bound(sc, unused, bound) || !sc.eof()) {
        return false;
    }
    if (bound.kind != MfBound::Kind::Ratio && bound.kind != MfBound::Kind::Bare) {
        return false;
    }
    num = bound.num;
    den = bound.den;
    return true;
}

bool parse_resolution_text(std::string_view text, float& dppx) {
    MediaState unused;
    Scan sc{text, 0};
    MfBound bound;
    if (!parse_mf_bound(sc, unused, bound) || !sc.eof()) {
        return false;
    }
    if (bound.kind != MfBound::Kind::Resolution) {
        return false;
    }
    dppx = bound.px;
    return true;
}


bool Mq::media_in_parens(bool& value) {
    if (!sc.eat('(')) {
        failed = true;
        return false;
    }
    sc.skip();
    if (sc.ident_is("not") || (sc.i < sc.s.size() && sc.s[sc.i] == '(')) {
        if (!media_condition(true, value) || !sc.eat(')')) {
            failed = true;
            return false;
        }
        return true;
    }
    // Media Queries 4 range context. A value on the left is `<value> <cmp> <name>`
    // or the two-sided `<value> <cmp> <name> <cmp> <value>` (both comparisons the same way).
    // The value is a length on width/height, a ratio on aspect-ratio, or dppx/x/dpi on resolution.
    sc.skip();
    const bool value_first =
        sc.i < sc.s.size() && (sc.s[sc.i] == '+' || sc.s[sc.i] == '-' || sc.s[sc.i] == '.' ||
                               std::isdigit(static_cast<unsigned char>(sc.s[sc.i])));
    if (value_first) {
        MfBound left;
        MfCmp cmp = MfCmp::Eq;
        std::string name;
        if (!parse_mf_bound(sc, media, left) || !read_mf_cmp(sc, cmp) || !sc.read_ident(name)) {
            failed = true;
            return false;
        }
        bool two = false;
        MfCmp cmp2 = MfCmp::Eq;
        MfBound right;
        sc.skip();
        const size_t mark = sc.i;
        if (read_mf_cmp(sc, cmp2)) {
            two = true;
            if (cmp == MfCmp::Eq || cmp2 == MfCmp::Eq || mf_lt_family(cmp) != mf_lt_family(cmp2) ||
                !parse_mf_bound(sc, media, right)) {
                failed = true;
                return false;
            }
        } else {
            sc.i = mark;
        }
        if (!sc.eat(')')) {
            failed = true;
            return false;
        }
        if (!eval_axis_cmp(name, cmp, false, left, media, value)) {
            failed = true;
            return false;
        }
        if (two) {
            bool upper = false;
            if (!eval_axis_cmp(name, cmp2, true, right, media, upper)) {
                failed = true;
                return false;
            }
            value = value && upper;
        }
        return true;
    }
    std::string name;
    if (!sc.read_ident(name)) {
        failed = true;
        return false;
    }
    sc.skip();
    if (sc.eat(')')) {
        value = feature_matches(name, {}, true, media);
        return true;
    }
    if (!sc.eat(':')) {
        MfCmp cmp = MfCmp::Eq;
        MfBound bound;
        if (!read_mf_cmp(sc, cmp) || !parse_mf_bound(sc, media, bound) || !sc.eat(')')) {
            failed = true;
            return false;
        }
        if (!eval_axis_cmp(name, cmp, true, bound, media, value)) {
            failed = true;
            return false;
        }
        return true;
    }
    sc.skip();
    const size_t vs = sc.i;
    int depth = 1;
    while (sc.i < sc.s.size() && depth > 0) {
        if (sc.s[sc.i] == '(') {
            ++depth;
        } else if (sc.s[sc.i] == ')') {
            --depth;
            if (depth == 0) {
                break;
            }
        }
        ++sc.i;
    }
    if (depth != 0) {
        failed = true;
        return false;
    }
    std::string_view raw = sc.s.substr(vs, sc.i - vs);
    while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.back()))) {
        raw.remove_suffix(1);
    }
    if (!sc.eat(')')) {
        failed = true;
        return false;
    }
    value = feature_matches(name, raw, false, media);
    return true;
}

bool Mq::media_condition(bool allow_or, bool& value) {
    sc.skip();
    if (sc.eat_ident("not")) {
        bool inner = false;
        if (!media_in_parens(inner)) {
            failed = true;
            return false;
        }
        sc.skip();
        if (sc.ident_is("and") || sc.ident_is("or")) {
            failed = true;
            return false;
        }
        value = !inner;
        return true;
    }
    bool acc = false;
    if (!media_in_parens(acc)) {
        failed = true;
        return false;
    }
    sc.skip();
    if (sc.ident_is("and")) {
        while (sc.eat_ident("and")) {
            bool next = false;
            if (!media_in_parens(next)) {
                failed = true;
                return false;
            }
            acc = acc && next;
            sc.skip();
        }
        if (sc.ident_is("or")) {
            failed = true;
            return false;
        }
        value = acc;
        return true;
    }
    if (sc.ident_is("or")) {
        if (!allow_or) {
            failed = true;
            return false;
        }
        while (sc.eat_ident("or")) {
            bool next = false;
            if (!media_in_parens(next)) {
                failed = true;
                return false;
            }
            acc = acc || next;
            sc.skip();
        }
        value = acc;
        return true;
    }
    value = acc;
    return true;
}

bool type_matches(std::string_view type) {
    const std::string t = lower_copy(type);
    return t == "all" || t == "screen";
}

bool Mq::media_query(bool& value) {
    sc.skip();
    if (sc.eof() || sc.s[sc.i] == ',') {
        value = true;
        return true;
    }
    if (sc.s[sc.i] == '(' || sc.ident_is("not")) {
        if (!media_condition(true, value)) {
            return false;
        }
        sc.skip();
        return true;
    }
    bool neg = false;
    if (sc.eat_ident("not")) {
        neg = true;
    } else {
        (void)sc.eat_ident("only");
    }
    std::string type;
    if (!sc.read_ident(type)) {
        failed = true;
        return false;
    }
    const std::string tl = lower_copy(type);
    if (tl == "and" || tl == "or" || tl == "not" || tl == "only") {
        failed = true;
        return false;
    }
    bool ok = type_matches(type);
    sc.skip();
    if (sc.eat_ident("and")) {
        bool cond = false;
        if (!media_condition(false, cond)) {
            return false;
        }
        ok = ok && cond;
    }
    value = neg ? !ok : ok;
    return true;
}

bool Mq::media_query_list() {
    if (sc.eof()) {
        return true;
    }
    bool any = false;
    for (;;) {
        bool q = false;
        const size_t mark = sc.i;
        if (!media_query(q)) {
            failed = true;
            return false;
        }
        if (sc.i == mark && !sc.eof() && sc.s[sc.i] != ',') {
            failed = true;
            return false;
        }
        any = any || q;
        sc.skip();
        if (sc.eof()) {
            return any;
        }
        if (!sc.eat(',')) {
            failed = true;
            return false;
        }
    }
}

size_t matching_paren(std::string_view s, size_t open) {
    int depth = 0;
    for (size_t i = open; i < s.size(); ++i) {
        if (s[i] == '(') {
            ++depth;
        } else if (s[i] == ')') {
            --depth;
            if (depth == 0) {
                return i;
            }
        }
    }
    return std::string_view::npos;
}

}  // namespace values_detail

using namespace values_detail;

bool media_matches(std::string_view query, const MediaState& media) {
    Mq mq{{query, 0}, media, false};
    if (mq.sc.eof()) {
        return true;
    }
    const bool ok = mq.media_query_list();
    return ok && !mq.failed && mq.sc.eof();
}

bool parse_container_prelude(std::string_view prelude, std::string& name,
                             std::string_view& condition) {
    name.clear();
    condition = {};
    Scan sc{prelude, 0};
    sc.skip();
    if (sc.eof()) {
        return true;
    }
    if (sc.i < sc.s.size() && sc.s[sc.i] != '(') {
        if (!sc.read_ident(name)) {
            return false;
        }
        sc.skip();
        if (sc.eof()) {
            return false;
        }
    }
    condition = sc.s.substr(sc.i);
    return true;
}

bool container_condition_matches(std::string_view condition, float width, float height) {
    Scan sc{condition, 0};
    sc.skip();
    if (sc.eof()) {
        return true;
    }
    MediaState media;
    media.width = width;
    media.height = height;
    media.size_query = true;
    Mq mq{{sc.s.substr(sc.i), 0}, media, false};
    bool value = false;
    if (!mq.media_condition(true, value) || mq.failed || !mq.sc.eof()) {
        return false;
    }
    return value;
}
}  // namespace css
}  // namespace zui
