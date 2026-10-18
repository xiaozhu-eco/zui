#include "css/image_set.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace zui {
namespace css {
namespace {

bool is_ws(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

void skip(std::string_view s, size_t& i) {
    while (i < s.size()) {
        if (is_ws(s[i])) {
            ++i;
            continue;
        }
        if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '*') {
            i += 2;
            while (i + 1 < s.size() && !(s[i] == '*' && s[i + 1] == '/')) {
                ++i;
            }
            i = i + 1 < s.size() ? i + 2 : s.size();
            continue;
        }
        break;
    }
}

std::string_view trim_sv(std::string_view s) {
    while (!s.empty() && is_ws(s.front())) {
        s.remove_prefix(1);
    }
    while (!s.empty() && is_ws(s.back())) {
        s.remove_suffix(1);
    }
    return s;
}

bool starts_ci(std::string_view s, size_t i, std::string_view prefix) {
    if (i + prefix.size() > s.size()) {
        return false;
    }
    for (size_t k = 0; k < prefix.size(); ++k) {
        const unsigned char got = static_cast<unsigned char>(s[i + k]);
        const unsigned char want = static_cast<unsigned char>(prefix[k]);
        if (std::tolower(got) != want) {
            return false;
        }
    }
    return true;
}

std::string lower_copy(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

bool function_name_is(std::string_view text, std::string_view name) {
    if (!starts_ci(text, 0, name)) {
        return false;
    }
    size_t i = name.size();
    while (i < text.size() && is_ws(text[i])) {
        ++i;
    }
    return i < text.size() && text[i] == '(';
}

bool supported_image_function(std::string_view text) {
    return function_name_is(text, "url") || function_name_is(text, "image-set") ||
           function_name_is(text, "linear-gradient") ||
           function_name_is(text, "repeating-linear-gradient") ||
           function_name_is(text, "radial-gradient") ||
           function_name_is(text, "repeating-radial-gradient") ||
           function_name_is(text, "conic-gradient") ||
           function_name_is(text, "repeating-conic-gradient");
}

/// Same decoders as `<img srcset>`: PNG, JPEG, SVG. WebP and AVIF are skipped so a later
/// candidate still shows.
bool mime_supported(std::string_view type) {
    type = trim_sv(type);
    const size_t semi = type.find(';');
    if (semi != std::string_view::npos) {
        type = trim_sv(type.substr(0, semi));
    }
    const std::string mime = lower_copy(type);
    return mime == "image/png" || mime == "image/jpeg" || mime == "image/jpg" ||
           mime == "image/svg+xml" || mime == "image/svg";
}

bool consume_string(std::string_view s, size_t& i, std::string& inner) {
    if (i >= s.size() || (s[i] != '"' && s[i] != '\'')) {
        return false;
    }
    const char quote = s[i++];
    inner.clear();
    while (i < s.size()) {
        const char c = s[i++];
        if (c == '\\') {
            if (i >= s.size()) {
                return false;
            }
            inner.push_back(s[i++]);
            continue;
        }
        if (c == quote) {
            return true;
        }
        inner.push_back(c);
    }
    return false;
}

bool consume_function(std::string_view s, size_t& i, std::string& text) {
    const size_t start = i;
    if (i >= s.size() || !std::isalpha(static_cast<unsigned char>(s[i]))) {
        return false;
    }
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (std::isalnum(c) || c == '-' || c == '_') {
            ++i;
            continue;
        }
        break;
    }
    skip(s, i);
    if (i >= s.size() || s[i] != '(') {
        return false;
    }
    int depth = 0;
    char quote = 0;
    for (; i < s.size(); ++i) {
        const char c = s[i];
        if (quote != 0) {
            if (c == '\\' && i + 1 < s.size()) {
                ++i;
                continue;
            }
            if (c == quote) {
                quote = 0;
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '(') {
            ++depth;
        } else if (c == ')') {
            --depth;
            if (depth == 0) {
                ++i;
                text.assign(s.data() + start, i - start);
                return true;
            }
        }
    }
    return false;
}

void skip_to_comma(std::string_view s, size_t& i) {
    int depth = 0;
    char quote = 0;
    while (i < s.size()) {
        const char c = s[i];
        if (quote != 0) {
            if (c == '\\' && i + 1 < s.size()) {
                i += 2;
                continue;
            }
            if (c == quote) {
                quote = 0;
            }
            ++i;
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
            ++i;
            continue;
        }
        if (c == '(') {
            ++depth;
        } else if (c == ')' && depth > 0) {
            --depth;
        } else if (c == ',' && depth == 0) {
            return;
        }
        ++i;
    }
}

/// `1x`, `2dppx`, `96dpi`, `38dpcm`. The unit is glued to the number.
bool parse_resolution_at(std::string_view s, size_t i, float& dppx, size_t& end) {
    if (i >= s.size()) {
        return false;
    }
    size_t j = i;
    if (s[j] == '+' || s[j] == '-') {
        ++j;
    }
    const size_t num_begin = j;
    bool digit = false;
    bool dot = false;
    while (j < s.size()) {
        const char c = s[j];
        if (c >= '0' && c <= '9') {
            digit = true;
            ++j;
            continue;
        }
        if (c == '.' && !dot) {
            dot = true;
            ++j;
            continue;
        }
        break;
    }
    if (!digit || j == num_begin) {
        return false;
    }
    size_t u = j;
    while (u < s.size() && std::isalpha(static_cast<unsigned char>(s[u]))) {
        ++u;
    }
    if (u == j) {
        return false;
    }
    const std::string unit = lower_copy(s.substr(j, u - j));
    if (unit != "x" && unit != "dppx" && unit != "dpi" && unit != "dpcm") {
        return false;
    }
    const std::string num_text(s.substr(i, j - i));
    char* parsed_end = nullptr;
    const float num = std::strtof(num_text.c_str(), &parsed_end);
    if (parsed_end != num_text.c_str() + num_text.size()) {
        return false;
    }
    if (unit == "x" || unit == "dppx") {
        dppx = num;
    } else if (unit == "dpi") {
        dppx = num / 96.f;
    } else {
        dppx = num * 2.54f / 96.f;
    }
    end = u;
    return true;
}

bool consume_type(std::string_view s, size_t& i, std::string& mime) {
    if (!starts_ci(s, i, "type")) {
        return false;
    }
    size_t j = i + 4;
    skip(s, j);
    if (j >= s.size() || s[j] != '(') {
        return false;
    }
    ++j;
    skip(s, j);
    if (!consume_string(s, j, mime)) {
        return false;
    }
    skip(s, j);
    if (j >= s.size() || s[j] != ')') {
        return false;
    }
    i = j + 1;
    return true;
}

std::string url_quoted(std::string_view inner) {
    std::string out = "url(\"";
    for (char c : inner) {
        if (c == '\\' || c == '"') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    out += "\")";
    return out;
}

struct Option {
    std::string image;
    float density = 1.f;
};

std::string pick_density(std::vector<Option> options, float dpr) {
    if (!(dpr > 0.f)) {
        dpr = 1.f;
    }
    std::vector<Option> unique;
    unique.reserve(options.size());
    for (Option& option : options) {
        bool duplicate = false;
        for (const Option& kept : unique) {
            const float scale =
                std::max(1.f, std::max(std::fabs(kept.density), std::fabs(option.density)));
            if (std::fabs(kept.density - option.density) <= 1e-4f * scale) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            unique.push_back(std::move(option));
        }
    }
    if (unique.empty()) {
        return {};
    }
    std::stable_sort(unique.begin(), unique.end(),
                     [](const Option& a, const Option& b) { return a.density < b.density; });
    size_t winner = unique.size() - 1;
    for (size_t i = 0; i < unique.size(); ++i) {
        if (unique[i].density + 1e-4f >= dpr) {
            winner = i;
            break;
        }
    }
    if (winner > 0) {
        const float higher = unique[winner].density;
        const float lower = unique[winner - 1].density;
        if ((dpr - lower) < (higher - dpr)) {
            winner = winner - 1;
        }
    }
    return unique[winner].image;
}

std::string select_depth(std::string_view image, float dpr, int depth);

std::string select_body(std::string_view body, float dpr, int depth) {
    std::vector<Option> options;
    size_t i = 0;
    while (i < body.size()) {
        skip(body, i);
        if (i >= body.size()) {
            break;
        }
        if (body[i] == ',') {
            ++i;
            continue;
        }
        Option option;
        bool ok = false;
        bool supported = true;
        if (body[i] == '"' || body[i] == '\'') {
            std::string inner;
            if (consume_string(body, i, inner) && !inner.empty()) {
                option.image = url_quoted(inner);
                ok = true;
            }
        } else if (std::isalpha(static_cast<unsigned char>(body[i]))) {
            std::string function;
            if (consume_function(body, i, function) && supported_image_function(function)) {
                option.image.assign(trim_sv(function));
                ok = true;
            }
        }
        bool saw_resolution = false;
        bool saw_type = false;
        if (ok) {
            for (;;) {
                skip(body, i);
                if (i >= body.size() || body[i] == ',') {
                    break;
                }
                float density = 0.f;
                size_t end = i;
                if (!saw_resolution && parse_resolution_at(body, i, density, end)) {
                    if (!(density > 0.f)) {
                        ok = false;
                        break;
                    }
                    option.density = density;
                    saw_resolution = true;
                    i = end;
                    continue;
                }
                if (!saw_type && starts_ci(body, i, "type")) {
                    std::string mime;
                    if (!consume_type(body, i, mime)) {
                        ok = false;
                        break;
                    }
                    if (!mime_supported(mime)) {
                        supported = false;
                    }
                    saw_type = true;
                    continue;
                }
                ok = false;
                break;
            }
        }
        if (!ok) {
            skip_to_comma(body, i);
        } else if (supported && !option.image.empty()) {
            options.push_back(std::move(option));
        }
        skip(body, i);
        if (i < body.size() && body[i] == ',') {
            ++i;
        }
    }
    std::string chosen = pick_density(std::move(options), dpr);
    if (depth < 4 && is_image_set(chosen)) {
        return select_depth(chosen, dpr, depth + 1);
    }
    return chosen;
}

std::string select_depth(std::string_view image, float dpr, int depth) {
    image = trim_sv(image);
    if (!is_image_set(image)) {
        return {};
    }
    size_t open = 0;
    while (open < image.size() && image[open] != '(') {
        ++open;
    }
    if (open >= image.size() || image.back() != ')') {
        return {};
    }
    const std::string_view body = image.substr(open + 1, image.size() - open - 2);
    return select_body(body, dpr, depth);
}

}  // namespace

bool is_image_set(std::string_view image) {
    image = trim_sv(image);
    if (!starts_ci(image, 0, "image-set")) {
        return false;
    }
    size_t i = 9;
    if (i < image.size()) {
        const unsigned char next = static_cast<unsigned char>(image[i]);
        if (std::isalnum(next) || next == '-' || next == '_') {
            return false;
        }
    }
    while (i < image.size() && is_ws(image[i])) {
        ++i;
    }
    return i < image.size() && image[i] == '(' && !image.empty() && image.back() == ')';
}

std::string select_image_set(std::string_view image, float device_pixel_ratio) {
    const float dpr = device_pixel_ratio > 0.f ? device_pixel_ratio : 1.f;
    return select_depth(image, dpr, 0);
}

}  // namespace css
}  // namespace zui
