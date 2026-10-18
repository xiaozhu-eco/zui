// Value tokenizers shared by the shorthand parsers: whitespace / comma / slash splitting and
// the grid token runs.

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

std::vector<std::string> tokens(std::string_view value) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < value.size()) {
        while (i < value.size() && std::isspace(static_cast<unsigned char>(value[i]))) {
            ++i;
        }
        if (i >= value.size()) {
            break;
        }
        const size_t start = i;
        size_t name = i;
        // CSS function names are idents, so hyphens count: linear-gradient(...), fit-content(...).
        while (name < value.size()) {
            const unsigned char c = static_cast<unsigned char>(value[name]);
            if (!(std::isalpha(c) || std::isdigit(c) || c == '-' || c == '_')) {
                break;
            }
            ++name;
        }
        if (name < value.size() && value[name] == '(') {
            int depth = 0;
            i = name;
            for (; i < value.size(); ++i) {
                if (value[i] == '(') {
                    ++depth;
                } else if (value[i] == ')') {
                    --depth;
                    if (depth == 0) {
                        ++i;
                        break;
                    }
                }
            }
        } else {
            while (i < value.size() && !std::isspace(static_cast<unsigned char>(value[i]))) {
                ++i;
            }
        }
        out.emplace_back(value.substr(start, i - start));
    }
    return out;
}

std::string lower_copy(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

std::string trim_copy(std::string_view s) {
    size_t begin = 0;
    size_t end = s.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(s[begin]))) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return std::string(s.substr(begin, end - begin));
}

/// CSS Values 4 section 3.2: the CSS-wide keywords every property accepts. Returns the
/// lowercased keyword, or nullptr for a value that is anything else.
const char* wide_keyword_of(std::string_view value) {
    static const char* const kKeywords[] = {"inherit", "initial", "unset", "revert",
                                            "revert-layer"};
    const std::string v = lower_copy(trim_copy(value));
    for (const char* keyword : kKeywords) {
        if (v == keyword) {
            return keyword;
        }
    }
    return nullptr;
}

// Split on the top-level `/` of a shorthand value; brackets, parentheses and quoted strings stay
// together.
std::vector<std::string> split_slash(std::string_view value) {
    std::vector<std::string> out;
    size_t start = 0;
    int depth = 0;
    char quote = 0;
    for (size_t i = 0; i < value.size(); ++i) {
        const char c = value[i];
        if (quote != 0) {
            quote = c == quote ? 0 : quote;
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '[' || c == '(') {
            ++depth;
        } else if (c == ']' || c == ')') {
            depth = depth > 0 ? depth - 1 : 0;
        } else if (c == '/' && depth == 0) {
            out.push_back(std::string(value.substr(start, i - start)));
            start = i + 1;
        }
    }
    out.push_back(std::string(value.substr(start)));
    return out;
}

// Split on whitespace, keeping `"area rows"`, `[line-names]` and `fn(...)` in one piece: the grid
// template grammar is defined in terms of those tokens, and a quoted string contains spaces.
std::vector<std::string> grid_tokens(std::string_view value) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < value.size()) {
        while (i < value.size() && std::isspace(static_cast<unsigned char>(value[i]))) {
            ++i;
        }
        if (i >= value.size()) {
            break;
        }
        const size_t start = i;
        if (value[i] == '"' || value[i] == '\'') {
            const char quote = value[i++];
            while (i < value.size() && value[i] != quote) {
                ++i;
            }
            if (i < value.size()) {
                ++i;
            }
        } else if (value[i] == '[') {
            while (i < value.size() && value[i] != ']') {
                ++i;
            }
            if (i < value.size()) {
                ++i;
            }
        } else {
            int depth = 0;
            while (i < value.size()) {
                const char c = value[i];
                if (c == '(') {
                    ++depth;
                } else if (c == ')') {
                    depth = depth > 0 ? depth - 1 : 0;
                } else if (depth == 0 && std::isspace(static_cast<unsigned char>(c))) {
                    break;
                }
                ++i;
            }
        }
        out.emplace_back(value.substr(start, i - start));
    }
    return out;
}

// Top-level commas; parentheses / brackets / quotes stay together (background layers).
std::vector<std::string> split_comma_top(std::string_view value) {
    std::vector<std::string> out;
    size_t start = 0;
    int depth = 0;
    char quote = 0;
    for (size_t i = 0; i < value.size(); ++i) {
        const char c = value[i];
        if (quote != 0) {
            quote = c == quote ? 0 : quote;
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '[' || c == '(') {
            ++depth;
        } else if (c == ']' || c == ')') {
            depth = depth > 0 ? depth - 1 : 0;
        } else if (c == ',' && depth == 0) {
            out.push_back(std::string(value.substr(start, i - start)));
            start = i + 1;
        }
    }
    out.push_back(std::string(value.substr(start)));
    return out;
}

// `tokens()` keeps `center/cover` glued; turn embedded top-level `/` into a `/` token.
std::vector<std::string> tokens_with_slash(std::string_view value) {
    std::vector<std::string> out;
    for (const std::string& t : tokens(value)) {
        if (t.find('/') != std::string::npos && t.find('(') == std::string::npos) {
            const auto parts = split_slash(t);
            for (size_t i = 0; i < parts.size(); ++i) {
                if (i > 0) {
                    out.emplace_back("/");
                }
                const std::string piece = trim_copy(parts[i]);
                if (!piece.empty()) {
                    out.push_back(piece);
                }
            }
        } else {
            out.push_back(t);
        }
    }
    return out;
}

bool looks_like_length_pct(const std::string& l) {
    if (l.empty()) {
        return false;
    }
    if (l == "0") {
        return true;
    }
    const unsigned char c0 = static_cast<unsigned char>(l[0]);
    if (!(std::isdigit(c0) || c0 == '.' || c0 == '+' || c0 == '-')) {
        return l.rfind("calc(", 0) == 0 || l.rfind("min(", 0) == 0 || l.rfind("max(", 0) == 0 ||
               l.rfind("clamp(", 0) == 0;
    }
    return l.find("px") != std::string::npos || l.find("em") != std::string::npos ||
           l.find("rem") != std::string::npos || l.find('%') != std::string::npos ||
           l.find("vw") != std::string::npos || l.find("vh") != std::string::npos ||
           l.find("vmin") != std::string::npos || l.find("vmax") != std::string::npos ||
           l.find("ch") != std::string::npos || l.find("ex") != std::string::npos ||
           l.find("lh") != std::string::npos || l.find("cm") != std::string::npos ||
           l.find("mm") != std::string::npos || l.find("in") != std::string::npos ||
           l.find("pt") != std::string::npos || l.find("pc") != std::string::npos ||
           l.find("q") != std::string::npos;
}

}  // namespace shorthand_detail

}  // namespace css
}  // namespace zui
