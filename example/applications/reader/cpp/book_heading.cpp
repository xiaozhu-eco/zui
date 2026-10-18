#include "book_detail.h"
namespace reader {

bool is_mark_cp(std::string_view cp) {
    for (const char* mark : kMarks) {
        if (cp == mark) return true;
    }
    return false;
}

bool same_chapter(const HeadingInfo& info, bool have_last, int last_num,
                  const std::string& last_rest) {
    if (!have_last || !info.has_num || info.num != last_num) {
        return false;
    }
    if (info.rest.empty() || last_rest.empty()) {
        return info.rest == last_rest;
    }
    return info.rest == last_rest || info.rest.find(last_rest) != std::string::npos ||
           last_rest.find(info.rest) != std::string::npos;
}

HeadingInfo heading_info(const std::string& line) {
    HeadingInfo info;
    const std::string text = normalize_head(line);
    info.title = text;
    if (text.empty() || cp_count(text) > 80) {
        return {};
    }
    if (text.find(kJuHao) != std::string::npos) {
        return {};
    }
    if (text.find("\xe7\xac\xac") != std::string::npos) {
        // "第 N / M 页"
        size_t i = 0;
        while (i + 3 <= text.size()) {
            if (text.compare(i, 3, kDi) == 0) {
                size_t j = i + 3;
                const int left = parse_chapter_number(text, j);
                while (j < text.size()) {
                    const size_t n = trim_prefix(text.substr(j));
                    if (n == 0) break;
                    j += n;
                }
                if (left >= 0 && j < text.size() && text[j] == '/') {
                    ++j;
                    const int right = parse_chapter_number(text, j);
                    while (j < text.size()) {
                        const size_t n = trim_prefix(text.substr(j));
                        if (n == 0) break;
                        j += n;
                    }
                    if (right >= 0 && j + 3 <= text.size() &&
                        text.compare(j, 3, "\xe9\xa1\xb5") == 0) {
                        return {};
                    }
                }
                break;
            }
            i += utf8_cp_len(text, i);
        }
    }
    if (looks_like_url(text)) {
        return {};
    }

    bool special = false;
    for (const char* head : kSpecialHeads) {
        const std::string_view h(head);
        if (!starts_with(text, h)) continue;
        if (text.size() == h.size()) {
            special = true;
            break;
        }
        const std::string_view next = utf8_cp_at(text, h.size());
        const char c = text[h.size()];
        if (c == ' ' || c == '\t' || c == ':' || c == '.' || c == '(' ||
            next == "\xef\xbc\x9a" || next == "\xe7\xac\xac" || next == "\xef\xbc\x88" ||
            next == "\xe3\x80\x90" || (c >= '0' && c <= '9')) {
            special = true;
            break;
        }
    }
    if (text == "\xe5\xba\x8f" || text == "\xe8\xb7\x8b") {
        special = true;
    }
    if (!special && text.size() >= 7) {
        std::string lead = text.substr(0, 7);
        for (char& c : lead) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }
        if (lead == "chapter" &&
            (text.size() == 7 || text[7] == ' ' || text[7] == '\t' ||
             (text[7] >= '0' && text[7] <= '9'))) {
            special = true;
        }
    }

    bool matched = special;
    if (!matched && starts_with(text, kDi)) {
        size_t i = std::char_traits<char>::length(kDi);
        const int num = parse_chapter_number(text, i);
        while (i < text.size()) {
            const size_t n = trim_prefix(text.substr(i));
            if (n == 0) break;
            i += n;
        }
        const std::string_view mark = utf8_cp_at(text, i);
        if (num >= 0 && is_mark_cp(mark)) {
            matched = true;
            info.has_num = true;
            info.num = num;
            i += mark.size();
            std::string rest = trim_copy(text.substr(i));
            std::string compact;
            for (char c : rest) {
                if (c != ' ' && c != '\t') compact.push_back(c);
            }
            info.rest = std::move(compact);
        }
    }
    if (!matched) {
        const std::string_view first = utf8_cp_at(text, 0);
        if (first == kJuan || first == kBu || first == kJi) {
            size_t i = first.size();
            const int num = parse_chapter_number(text, i);
            if (num >= 0) {
                if (i >= text.size()) {
                    matched = true;
                } else {
                    const char c = text[i];
                    const std::string_view next = utf8_cp_at(text, i);
                    if (c == ' ' || c == '\t' || c == ':' || c == '.' ||
                        next == "\xef\xbc\x9a") {
                        matched = true;
                    }
                }
            }
        }
    }
    if (!matched) {
        return {};
    }
    return info;
}

bool is_chapter_heading(const std::string& line) {
    return !heading_info(line).title.empty();
}


const char* meta_value(const std::string& line, const char* const* keys, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        const std::string_view key(keys[i]);
        if (starts_with(line, key)) {
            return keys[i];
        }
    }
    return nullptr;
}

}  // namespace reader
