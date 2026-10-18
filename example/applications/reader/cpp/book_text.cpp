#include "book_detail.h"
namespace reader {

size_t trim_prefix(std::string_view s) {
    if (s.empty()) {
        return 0;
    }
    const auto c = static_cast<unsigned char>(s[0]);
    if (c == ' ' || c == '\t' || c == '\f' || c == '\v') {
        return 1;
    }
    if (s.size() >= 2 && c == 0xc2 && static_cast<unsigned char>(s[1]) == 0xa0) {
        return 2;
    }
    if (s.size() >= 3 && c == 0xe3 && static_cast<unsigned char>(s[1]) == 0x80 &&
        static_cast<unsigned char>(s[2]) == 0x80) {
        return 3;
    }
    return 0;
}

std::string trim_copy(std::string_view in) {
    size_t b = 0;
    while (b < in.size()) {
        const size_t n = trim_prefix(in.substr(b));
        if (n == 0) {
            break;
        }
        b += n;
    }
    size_t e = in.size();
    while (e > b) {
        size_t drop = 1;
        const auto c = static_cast<unsigned char>(in[e - 1]);
        if (c == ' ' || c == '\t' || c == '\f' || c == '\v' || c == '\r') {
            drop = 1;
        } else if (e - b >= 2 && static_cast<unsigned char>(in[e - 2]) == 0xc2 && c == 0xa0) {
            drop = 2;
        } else if (e - b >= 3 && static_cast<unsigned char>(in[e - 3]) == 0xe3 &&
                   static_cast<unsigned char>(in[e - 2]) == 0x80 && c == 0x80) {
            drop = 3;
        } else {
            break;
        }
        e -= drop;
    }
    return std::string(in.substr(b, e - b));
}

std::vector<std::string> split_lines(std::string_view text) {
    std::vector<std::string> lines;
    std::string cur;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\r') {
            lines.push_back(trim_copy(cur));
            cur.clear();
            if (i + 1 < text.size() && text[i + 1] == '\n') {
                ++i;
            }
            continue;
        }
        if (c == '\n') {
            lines.push_back(trim_copy(cur));
            cur.clear();
            continue;
        }
        if (c != '\0') {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) {
        lines.push_back(trim_copy(cur));
    }
    return lines;
}


std::string clean_line(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size();) {
        if (i + 1 < raw.size() && static_cast<unsigned char>(raw[i]) == 0xc2 &&
            static_cast<unsigned char>(raw[i + 1]) == 0xa0) {
            out.push_back(' ');
            i += 2;
            continue;
        }
        out.push_back(raw[i]);
        ++i;
    }
    out = trim_copy(out);
    std::string collapsed;
    collapsed.reserve(out.size());
    bool space = false;
    for (size_t i = 0; i < out.size();) {
        const size_t n = trim_prefix(std::string_view(out).substr(i));
        if (n > 0) {
            if (!collapsed.empty() && !space) {
                collapsed.push_back(' ');
                space = true;
            }
            i += n;
            continue;
        }
        collapsed.push_back(out[i]);
        space = false;
        ++i;
    }
    return collapsed;
}

std::string normalize_head(std::string_view line) {
    std::string text = clean_line(line);
    auto strip_prefix = [&](std::string_view mark) {
        if (starts_with(text, mark)) {
            text = trim_copy(std::string_view(text).substr(mark.size()));
            return true;
        }
        return false;
    };
    strip_prefix("\xe3\x80\x90") || strip_prefix("[") || strip_prefix("\xef\xbc\x88") ||
        strip_prefix("(");
    auto strip_suffix = [&](std::string_view mark) {
        if (text.size() >= mark.size() &&
            text.compare(text.size() - mark.size(), mark.size(), mark) == 0) {
            text = trim_copy(std::string_view(text).substr(0, text.size() - mark.size()));
            return true;
        }
        return false;
    };
    strip_suffix("\xe3\x80\x91") || strip_suffix("]") || strip_suffix("\xef\xbc\x89") ||
        strip_suffix(")");
    return text;
}

bool contains_ascii_ci(std::string_view text, std::string_view ascii) {
    if (ascii.size() > text.size()) {
        return false;
    }
    for (size_t i = 0; i + ascii.size() <= text.size(); ++i) {
        bool ok = true;
        for (size_t j = 0; j < ascii.size(); ++j) {
            char c = text[i + j];
            if (c >= 'A' && c <= 'Z') {
                c = static_cast<char>(c - 'A' + 'a');
            }
            if (c != ascii[j]) {
                ok = false;
                break;
            }
        }
        if (ok) {
            return true;
        }
    }
    return false;
}

bool looks_like_url(std::string_view text) {
    return contains_ascii_ci(text, "http:") || contains_ascii_ci(text, "https:") ||
           contains_ascii_ci(text, "www.") || contains_ascii_ci(text, ".com") ||
           contains_ascii_ci(text, ".vip") || contains_ascii_ci(text, ".org") ||
           text.find("\xe6\x9c\x80\xe6\x96\xb0\xe7\xbd\x91\xe5\x9d\x80") != std::string_view::npos;
}

bool is_cn_digit(std::string_view cp) {
    static constexpr const char* digits[] = {
        "\xe9\x9b\xb6", "\xe3\x80\x87", "\xe4\xb8\x80", "\xe4\xba\x8c", "\xe4\xb8\xa4",
        "\xe4\xb8\x89", "\xe5\x9b\x9b", "\xe4\xba\x94", "\xe5\x85\xad", "\xe4\xb8\x83",
        "\xe5\x85\xab", "\xe4\xb9\x9d", "\xe5\x8d\x81", "\xe7\x99\xbe", "\xe5\x8d\x83",
        "\xe4\xb8\x87",
    };
    for (const char* digit : digits) {
        if (cp == digit) {
            return true;
        }
    }
    return false;
}

int cn_digit_value(std::string_view cp) {
    if (cp == "\xe9\x9b\xb6" || cp == "\xe3\x80\x87") return 0;
    if (cp == "\xe4\xb8\x80") return 1;
    if (cp == "\xe4\xba\x8c" || cp == "\xe4\xb8\xa4") return 2;
    if (cp == "\xe4\xb8\x89") return 3;
    if (cp == "\xe5\x9b\x9b") return 4;
    if (cp == "\xe4\xba\x94") return 5;
    if (cp == "\xe5\x85\xad") return 6;
    if (cp == "\xe4\xb8\x83") return 7;
    if (cp == "\xe5\x85\xab") return 8;
    if (cp == "\xe4\xb9\x9d") return 9;
    return -1;
}

int cn_unit_value(std::string_view cp) {
    if (cp == "\xe5\x8d\x81") return 10;
    if (cp == "\xe7\x99\xbe") return 100;
    if (cp == "\xe5\x8d\x83") return 1000;
    if (cp == "\xe4\xb8\x87") return 10000;
    return 0;
}

size_t utf8_cp_len(std::string_view s, size_t i) {
    if (i >= s.size()) return 0;
    const auto c = static_cast<unsigned char>(s[i]);
    if ((c & 0x80) == 0) return 1;
    if ((c & 0xe0) == 0xc0) return 2;
    if ((c & 0xf0) == 0xe0) return 3;
    if ((c & 0xf8) == 0xf0) return 4;
    return 1;
}

std::string_view utf8_cp_at(std::string_view s, size_t i) {
    const size_t n = utf8_cp_len(s, i);
    if (n == 0 || i + n > s.size()) return {};
    return s.substr(i, n);
}

int parse_chapter_number(std::string_view text, size_t& i) {
    while (i < text.size()) {
        const size_t n = trim_prefix(text.substr(i));
        if (n == 0) break;
        i += n;
    }
    if (i < text.size() && text[i] >= '0' && text[i] <= '9') {
        int value = 0;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
            value = value * 10 + (text[i] - '0');
            ++i;
        }
        return value;
    }
    int acc = 0;
    int cur = 0;
    bool any = false;
    while (i < text.size()) {
        const std::string_view cp = utf8_cp_at(text, i);
        if (cp.empty()) break;
        const int digit = cn_digit_value(cp);
        const int unit = cn_unit_value(cp);
        if (digit >= 0) {
            cur = digit;
            any = true;
            i += cp.size();
            continue;
        }
        if (unit > 0) {
            if (cur == 0) cur = 1;
            if (unit == 10000) {
                acc = (acc + cur) * 10000;
            } else {
                acc += cur * unit;
            }
            cur = 0;
            any = true;
            i += cp.size();
            continue;
        }
        break;
    }
    if (!any) return -1;
    return acc + cur;
}

}  // namespace reader
