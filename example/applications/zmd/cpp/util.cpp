#include "util.h"

#include <algorithm>
#include <cctype>

namespace zmd::util {

bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

std::vector<std::string_view> lines(std::string_view text) {
    std::vector<std::string_view> out;
    std::size_t begin = 0;
    for (;;) {
        std::size_t end = text.find('\n', begin);
        std::string_view line = end == std::string_view::npos
                                    ? text.substr(begin)
                                    : text.substr(begin, end - begin);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        out.push_back(line);
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1;
    }
    return out;
}

std::string_view trim(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && is_space(text[begin])) {
        ++begin;
    }
    while (end > begin && is_space(text[end - 1])) {
        --end;
    }
    return text.substr(begin, end - begin);
}

std::string lower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

bool starts_with(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

std::string escape(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

std::size_t line_start(std::string_view text, std::size_t offset) {
    offset = std::min(offset, text.size());
    if (offset == 0) {
        return 0;
    }
    std::size_t pos = text.rfind('\n', offset - 1);
    return pos == std::string_view::npos ? 0 : pos + 1;
}

std::size_t line_end(std::string_view text, std::size_t offset) {
    std::size_t pos = text.find('\n', std::min(offset, text.size()));
    return pos == std::string_view::npos ? text.size() : pos;
}

std::pair<int, int> counts(std::string_view text) {
    int words = 0;
    int non_blank = 0;
    bool in_latin = false;
    bool line_has_text = false;
    std::size_t i = 0;
    while (i < text.size()) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == '\n') {
            if (line_has_text) {
                ++non_blank;
            }
            line_has_text = false;
            in_latin = false;
            ++i;
            continue;
        }
        if (c < 0x80) {
            if (std::isalnum(c)) {
                if (!in_latin) {
                    ++words;
                    in_latin = true;
                }
                line_has_text = true;
            } else {
                // Any separator ends a Latin word; only non-blank bytes are body text.
                in_latin = false;
                if (!is_space(static_cast<char>(c))) {
                    line_has_text = true;
                }
            }
            ++i;
            continue;
        }
        std::size_t width = c >= 0xF0 ? 4 : (c >= 0xE0 ? 3 : 2);
        if (width == 3) {
            ++words;  // one 3-byte code point (CJK) counts as one word
        }
        line_has_text = true;
        in_latin = false;
        i += width;
    }
    if (line_has_text) {
        ++non_blank;
    }
    return {words, non_blank};
}

}  // namespace zmd::util

