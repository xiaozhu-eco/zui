#include "book_pdf.h"

#include "book_detail.h"
#include "book_format.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <zlib.h>

namespace reader {
namespace {

constexpr std::size_t kMaxPdfBytes = 48ull * 1024ull * 1024ull;

std::string read_all_capped(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    in.seekg(0, std::ios::end);
    const auto sz = static_cast<std::size_t>(in.tellg());
    in.seekg(0, std::ios::beg);
    if (sz == 0 || sz > kMaxPdfBytes) {
        // Still try reading up to the cap for oversized files.
        std::string buf(kMaxPdfBytes, '\0');
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        buf.resize(static_cast<std::size_t>(in.gcount()));
        return buf;
    }
    std::string buf(sz, '\0');
    in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
    buf.resize(static_cast<std::size_t>(in.gcount()));
    return buf;
}

bool starts_pdf(const std::string& bytes) {
    return bytes.size() >= 5 && bytes.compare(0, 5, "%PDF-") == 0;
}

int count_pages_rough(const std::string& bytes) {
    // Prefer /Type /Pages ... /Count N
    int best = 0;
    for (size_t i = 0; i + 12 < bytes.size(); ++i) {
        if (bytes[i] != '/') {
            continue;
        }
        if (bytes.compare(i, 6, "/Count") == 0) {
            size_t j = i + 6;
            while (j < bytes.size() && std::isspace(static_cast<unsigned char>(bytes[j]))) {
                ++j;
            }
            if (j < bytes.size() && (std::isdigit(static_cast<unsigned char>(bytes[j])) || bytes[j] == '-')) {
                int val = 0;
                const bool neg = bytes[j] == '-';
                if (neg) {
                    ++j;
                }
                while (j < bytes.size() && std::isdigit(static_cast<unsigned char>(bytes[j]))) {
                    val = val * 10 + (bytes[j] - '0');
                    ++j;
                }
                if (!neg && val > best) {
                    best = val;
                }
            }
        }
    }
    if (best > 0) {
        return best;
    }
    // Fallback: count /Type /Page (not Pages)
    int pages = 0;
    for (size_t i = 0; i + 10 < bytes.size(); ++i) {
        if (bytes.compare(i, 5, "/Type") != 0) {
            continue;
        }
        size_t j = i + 5;
        while (j < bytes.size() && std::isspace(static_cast<unsigned char>(bytes[j]))) {
            ++j;
        }
        if (bytes.compare(j, 5, "/Page") == 0 &&
            (j + 5 >= bytes.size() || bytes[j + 5] != 's')) {
            ++pages;
        }
    }
    return pages > 0 ? pages : 1;
}

std::string flate_inflate(const std::string& src) {
    if (src.empty()) {
        return {};
    }
    z_stream strm{};
    if (inflateInit(&strm) != Z_OK) {
        return {};
    }
    strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(src.data()));
    strm.avail_in = static_cast<uInt>(src.size());
    std::string out;
    char buf[16384];
    int rc = Z_OK;
    while (rc != Z_STREAM_END) {
        strm.next_out = reinterpret_cast<Bytef*>(buf);
        strm.avail_out = sizeof(buf);
        rc = inflate(&strm, Z_NO_FLUSH);
        if (rc != Z_OK && rc != Z_STREAM_END) {
            inflateEnd(&strm);
            return {};
        }
        out.append(buf, sizeof(buf) - strm.avail_out);
        if (out.size() > kMaxPdfBytes) {
            inflateEnd(&strm);
            return {};
        }
    }
    inflateEnd(&strm);
    return out;
}

std::string pdf_string_literal(std::string_view s) {
    // Decode PDF literal string content (no surrounding parens).
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            const char n = s[++i];
            switch (n) {
                case 'n':
                    out.push_back('\n');
                    break;
                case 'r':
                    out.push_back('\r');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                case 'b':
                    out.push_back('\b');
                    break;
                case 'f':
                    out.push_back('\f');
                    break;
                case '(':
                case ')':
                case '\\':
                    out.push_back(n);
                    break;
                default:
                    if (n >= '0' && n <= '7') {
                        int v = n - '0';
                        for (int k = 0; k < 2 && i + 1 < s.size() && s[i + 1] >= '0' && s[i + 1] <= '7';
                             ++k) {
                            v = (v << 3) + (s[++i] - '0');
                        }
                        out.push_back(static_cast<char>(v));
                    } else {
                        out.push_back(n);
                    }
                    break;
            }
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

std::string hex_to_bytes(std::string_view hex) {
    std::string out;
    int nibble = -1;
    for (char c : hex) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            continue;
        }
        int v = -1;
        if (c >= '0' && c <= '9') {
            v = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            v = 10 + c - 'a';
        } else if (c >= 'A' && c <= 'F') {
            v = 10 + c - 'A';
        }
        if (v < 0) {
            continue;
        }
        if (nibble < 0) {
            nibble = v;
        } else {
            out.push_back(static_cast<char>((nibble << 4) | v));
            nibble = -1;
        }
    }
    return out;
}

std::string utf16be_to_utf8(std::string_view bytes) {
    std::string out;
    size_t i = 0;
    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xfe &&
        static_cast<unsigned char>(bytes[1]) == 0xff) {
        i = 2;
    }
    while (i + 1 < bytes.size()) {
        const unsigned cu =
            (static_cast<unsigned char>(bytes[i]) << 8) | static_cast<unsigned char>(bytes[i + 1]);
        i += 2;
        std::uint32_t cp = cu;
        if (cu >= 0xd800 && cu <= 0xdbff && i + 1 < bytes.size()) {
            const unsigned low =
                (static_cast<unsigned char>(bytes[i]) << 8) | static_cast<unsigned char>(bytes[i + 1]);
            if (low >= 0xdc00 && low <= 0xdfff) {
                cp = 0x10000 + (((cu - 0xd800) << 10) | (low - 0xdc00));
                i += 2;
            }
        }
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
        } else {
            out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
        }
    }
    return out;
}

void append_pdf_text_token(std::string& sink, std::string_view raw) {
    if (raw.size() >= 2 && static_cast<unsigned char>(raw[0]) == 0xfe &&
        static_cast<unsigned char>(raw[1]) == 0xff) {
        sink += utf16be_to_utf8(raw);
        return;
    }
    // Prefer UTF-8-ish printable; drop NULs.
    for (unsigned char c : raw) {
        if (c == 0) {
            continue;
        }
        if (c >= 0x20 || c == '\n' || c == '\r' || c == '\t') {
            sink.push_back(static_cast<char>(c));
        }
    }
}


void harvest_bt_literals(const std::string& content, std::string& text) {
    size_t i = 0;
    while (i < content.size()) {
        const size_t bt = content.find("BT", i);
        if (bt == std::string::npos) {
            break;
        }
        if (bt > 0 && std::isalnum(static_cast<unsigned char>(content[bt - 1]))) {
            i = bt + 2;
            continue;
        }
        const size_t et = content.find("ET", bt + 2);
        if (et == std::string::npos) {
            break;
        }
        for (size_t p = bt; p < et; ++p) {
            if (content[p] != '(') {
                continue;
            }
            size_t q = p + 1;
            int depth = 1;
            std::string lit;
            while (q < et && depth > 0) {
                if (content[q] == '\\' && q + 1 < et) {
                    lit.push_back('\\');
                    lit.push_back(content[q + 1]);
                    q += 2;
                    continue;
                }
                if (content[q] == '(') {
                    ++depth;
                } else if (content[q] == ')') {
                    --depth;
                    if (depth == 0) {
                        break;
                    }
                }
                if (depth > 0) {
                    lit.push_back(content[q]);
                }
                ++q;
            }
            append_pdf_text_token(text, pdf_string_literal(lit));
            p = q;
        }
        if (!text.empty() && text.back() != '\n') {
            text.push_back('\n');
        }
        i = et + 2;
    }
}


void scrape_pdf_operators(const std::string& content, std::string& text) {
    for (size_t i = 0; i + 1 < content.size(); ++i) {
        if ((content[i] == 'T' && (content[i + 1] == 'd' || content[i + 1] == 'D' || content[i + 1] == '*'))) {
            if (!text.empty() && text.back() != '\n') {
                text.push_back('\n');
            }
        }
        if (content[i] != '(') {
            continue;
        }
        size_t j = i + 1;
        int depth = 1;
        std::string lit;
        while (j < content.size() && depth > 0) {
            if (content[j] == '\\' && j + 1 < content.size()) {
                lit.push_back('\\');
                lit.push_back(content[j + 1]);
                j += 2;
                continue;
            }
            if (content[j] == '(') {
                ++depth;
            } else if (content[j] == ')') {
                --depth;
                if (depth == 0) {
                    break;
                }
            }
            if (depth > 0) {
                lit.push_back(content[j]);
            }
            ++j;
        }
        bool show = false;
        for (size_t k = j + 1; k < content.size() && k < j + 48; ++k) {
            if (content[k] == 'T' && k + 1 < content.size() &&
                (content[k + 1] == 'j' || content[k + 1] == 'J')) {
                show = true;
                break;
            }
            if (content[k] == '\'' || content[k] == '"') {
                show = true;
                break;
            }
            if (content[k] == '(') {
                break;
            }
        }
        if (show) {
            append_pdf_text_token(text, pdf_string_literal(lit));
        }
        i = j;
    }
}

void extract_text_from_content(const std::string& content, std::string& text) {
    for (size_t i = 0; i < content.size(); ++i) {
        if (content[i] == '(') {
            // literal string
            size_t j = i + 1;
            int depth = 1;
            std::string lit;
            while (j < content.size() && depth > 0) {
                if (content[j] == '\\' && j + 1 < content.size()) {
                    lit.push_back('\\');
                    lit.push_back(content[j + 1]);
                    j += 2;
                    continue;
                }
                if (content[j] == '(') {
                    ++depth;
                } else if (content[j] == ')') {
                    --depth;
                    if (depth == 0) {
                        break;
                    }
                }
                if (depth > 0) {
                    lit.push_back(content[j]);
                }
                ++j;
            }
            // Look ahead for Tj / ' / "
            size_t k = j + 1;
            while (k < content.size() && std::isspace(static_cast<unsigned char>(content[k]))) {
                ++k;
            }
            if (k + 1 < content.size() &&
                ((content[k] == 'T' && content[k + 1] == 'j') || content[k] == '\'' ||
                 content[k] == '"')) {
                append_pdf_text_token(text, pdf_string_literal(lit));
                if (content[k] == '\'' || content[k] == '"') {
                    text.push_back('\n');
                }
            }
            i = j;
            continue;
        }
        if (content[i] == '<') {
            // hex string <...>
            size_t j = i + 1;
            if (j < content.size() && content[j] == '<') {
                continue;  // dict
            }
            std::string hex;
            while (j < content.size() && content[j] != '>') {
                hex.push_back(content[j++]);
            }
            size_t k = j + 1;
            while (k < content.size() && std::isspace(static_cast<unsigned char>(content[k]))) {
                ++k;
            }
            if (k + 1 < content.size() && content[k] == 'T' && content[k + 1] == 'j') {
                append_pdf_text_token(text, hex_to_bytes(hex));
            }
            i = j;
            continue;
        }
        // TJ array: [ ... ] TJ
        if (content[i] == '[') {
            size_t j = i + 1;
            int depth = 1;
            while (j < content.size() && depth > 0) {
                if (content[j] == '(') {
                    // skip string
                    ++j;
                    int d = 1;
                    while (j < content.size() && d > 0) {
                        if (content[j] == '\\' && j + 1 < content.size()) {
                            j += 2;
                            continue;
                        }
                        if (content[j] == '(') {
                            ++d;
                        } else if (content[j] == ')') {
                            --d;
                        }
                        ++j;
                    }
                    continue;
                }
                if (content[j] == '[') {
                    ++depth;
                } else if (content[j] == ']') {
                    --depth;
                    if (depth == 0) {
                        break;
                    }
                }
                ++j;
            }
            size_t k = j + 1;
            while (k < content.size() && std::isspace(static_cast<unsigned char>(content[k]))) {
                ++k;
            }
            if (k + 1 < content.size() && content[k] == 'T' && content[k + 1] == 'J') {
                // Re-scan the array for strings.
                for (size_t p = i + 1; p < j; ++p) {
                    if (content[p] == '(') {
                        size_t q = p + 1;
                        std::string lit;
                        int d = 1;
                        while (q < j && d > 0) {
                            if (content[q] == '\\' && q + 1 < j) {
                                lit.push_back('\\');
                                lit.push_back(content[q + 1]);
                                q += 2;
                                continue;
                            }
                            if (content[q] == '(') {
                                ++d;
                            } else if (content[q] == ')') {
                                --d;
                                if (d == 0) {
                                    break;
                                }
                            }
                            if (d > 0) {
                                lit.push_back(content[q]);
                            }
                            ++q;
                        }
                        append_pdf_text_token(text, pdf_string_literal(lit));
                        p = q;
                    } else if (content[p] == '<') {
                        size_t q = p + 1;
                        std::string hex;
                        while (q < j && content[q] != '>') {
                            hex.push_back(content[q++]);
                        }
                        append_pdf_text_token(text, hex_to_bytes(hex));
                        p = q;
                    }
                }
                text.push_back('\n');
            }
            i = j;
        }
    }
}

std::vector<std::string> inflate_all_streams(const std::string& bytes) {
    std::vector<std::string> streams;
    size_t i = 0;
    while (i < bytes.size()) {
        const size_t stream_pos = bytes.find("stream", i);
        if (stream_pos == std::string::npos) {
            break;
        }
        // Confirm keyword boundary
        if (stream_pos > 0 && std::isalnum(static_cast<unsigned char>(bytes[stream_pos - 1]))) {
            i = stream_pos + 6;
            continue;
        }
        size_t data = stream_pos + 6;
        if (data < bytes.size() && bytes[data] == '\r') {
            ++data;
        }
        if (data < bytes.size() && bytes[data] == '\n') {
            ++data;
        }
        const size_t end = bytes.find("endstream", data);
        if (end == std::string::npos) {
            break;
        }
        std::string raw = bytes.substr(data, end - data);
        if (!raw.empty() && raw.back() == '\n') {
            raw.pop_back();
        }
        if (!raw.empty() && raw.back() == '\r') {
            raw.pop_back();
        }
        // Look back for /FlateDecode in the preceding ~256 bytes.
        const size_t look = stream_pos > 512 ? stream_pos - 512 : 0;
        const std::string_view head(bytes.data() + look, stream_pos - look);
        std::string decoded;
        if (head.find("/FlateDecode") != std::string_view::npos ||
            head.find("/Fl") != std::string_view::npos) {
            decoded = flate_inflate(raw);
        }
        if (decoded.empty()) {
            decoded = raw;
        }
        if (!decoded.empty()) {
            streams.push_back(std::move(decoded));
        }
        i = end + 9;
    }
    return streams;
}

std::vector<std::string> extract_page_texts(const std::string& bytes, int page_count) {
    std::vector<std::string> streams = inflate_all_streams(bytes);
    std::vector<std::string> pages(static_cast<size_t>(std::max(1, page_count)));
    if (streams.empty()) {
        // Whole-file scrape into page 0.
        std::string all;
        extract_text_from_content(bytes, all);
        if (trim_copy(all).empty()) {
            harvest_bt_literals(bytes, all);
        }
        if (trim_copy(all).size() < 8) {
            scrape_pdf_operators(bytes, all);
        }
        pages[0] = std::move(all);
        return pages;
    }

    // Heuristic: distribute content streams across pages by order.
    // Many PDFs emit one (or few) content streams per page in order.
    if (static_cast<int>(streams.size()) >= page_count && page_count > 0) {
        for (int p = 0; p < page_count; ++p) {
            std::string text;
            // Map streams in blocks when more streams than pages.
            const size_t begin = static_cast<size_t>(p) * streams.size() / page_count;
            const size_t end = static_cast<size_t>(p + 1) * streams.size() / page_count;
            for (size_t s = begin; s < end; ++s) {
                extract_text_from_content(streams[s], text);
                if (trim_copy(text).empty()) {
                    harvest_bt_literals(streams[s], text);
                }
                if (trim_copy(text).size() < 8) {
                    scrape_pdf_operators(streams[s], text);
                }
                if (!text.empty() && text.back() != '\n') {
                    text.push_back('\n');
                }
            }
            pages[static_cast<size_t>(p)] = std::move(text);
        }
        return pages;
    }

    for (size_t s = 0; s < streams.size(); ++s) {
        std::string text;
        extract_text_from_content(streams[s], text);
        if (trim_copy(text).empty()) {
            harvest_bt_literals(streams[s], text);
        }
        if (trim_copy(text).size() < 8) {
            scrape_pdf_operators(streams[s], text);
        }
        const size_t page = std::min(s, pages.size() - 1);
        if (!pages[page].empty() && !text.empty()) {
            pages[page].push_back('\n');
        }
        pages[page] += text;
    }
    return pages;
}

std::string page_title(int index) {
    // "第 N 页"
    return std::string("\xe7\xac\xac ") + std::to_string(index + 1) + " \xe9\xa1\xb5";
}

std::string page_html(const std::string& text) {
    std::ostringstream html;
    html << "<div class=\"pdf-page-card\">";
    const std::string trimmed = trim_copy(text);
    if (trimmed.empty()) {
        html << "<div class=\"pdf-empty\">"
                "<p class=\"pdf-empty-title\" id=\"pa0\">\xe6\x9c\xac\xe9\xa1\xb5\xe6\x97\xa0\xe5\x8f\xaf\xe6\x8f\x90\xe5\x8f\x96\xe6\x96\x87\xe5\xad\x97</p>"
                "<p class=\"pdf-empty-hint\">\xe5\x9b\xbe\xe5\x83\x8f\xe5\x9e\x8b / \xe5\xad\x97\xe4\xbd\x93\xe5\xb5\x8c\xe5\x85\xa5\xe7\x9a\x84 PDF "
                "\xe6\x9a\x82\xe4\xb8\x8d\xe6\x94\xaf\xe6\x8c\x81\xe6\x98\xbe\xe7\xa4\xba\xe5\x8e\x9f\xe6\x96\x87\xe3\x80\x82"
                "\xe5\x8f\xaf\xe4\xbb\xa5\xe7\x94\xa8\xe7\xbf\xbb\xe9\xa1\xb5 / \xe7\x9b\xae\xe5\xbd\x95\xe7\xbb\xa7\xe7\xbb\xad\xe9\x98\x85\xe8\xaf\xbb\xe3\x80\x82\xe6\x89\xbe\xe4\xb9\xa6\xe4\xbb\x8d\xe4\xbc\x9a\xe8\xb7\xb3\xe8\xbf\x87\xe6\x97\xa0\xe6\x96\x87\xe5\xad\x97\xe9\xa1\xb5\xe3\x80\x82</p>"
                "<img id=\"pdf_page_img\" class=\"pdf-page-img\" hidden alt=\"\"/>"
             "</div>";
    } else {
        html << "<div class=\"pdf-page-text\">";
        std::string line;
        int para_i = 0;
        for (size_t i = 0; i <= text.size(); ++i) {
            if (i == text.size() || text[i] == '\n') {
                std::string cleaned = trim_copy(line);
                if (!cleaned.empty()) {
                    std::string esc;
                    for (char c : cleaned) {
                        if (c == '&') {
                            esc += "&amp;";
                        } else if (c == '<') {
                            esc += "&lt;";
                        } else if (c == '>') {
                            esc += "&gt;";
                        } else {
                            esc.push_back(c);
                        }
                    }
                    html << "<p class=\"pdf-line\" id=\"pa" << para_i++ << "\">" << esc
                         << "</p>";
                }
                line.clear();
            } else if (text[i] != '\r') {
                line.push_back(text[i]);
            }
        }
        html << "</div><img id=\"pdf_page_img\" class=\"pdf-page-img\" hidden alt=\"\"/>";
    }
    html << "</div>";
    return html.str();
}

void fill_chapters_from_texts(Book& book, const std::vector<std::string>& texts, int lo, int hi,
                              bool bodies) {
    book.chapters.resize(texts.size());
    book.chapter_count = static_cast<int>(texts.size());
    for (int i = 0; i < book.chapter_count; ++i) {
        Chapter& ch = book.chapters[static_cast<size_t>(i)];
        ch.title = page_title(i);
        ch.file_offset = static_cast<std::uint64_t>(i);
        if (!bodies || i < lo || i > hi) {
            continue;
        }
        ch.paragraphs.clear();
        const std::string& text = texts[static_cast<size_t>(i)];
        std::string line;
        for (size_t n = 0; n <= text.size(); ++n) {
            if (n == text.size() || text[n] == '\n') {
                const std::string cleaned = clean_line(line);
                if (!cleaned.empty()) {
                    ch.paragraphs.push_back(cleaned);
                }
                line.clear();
            } else if (text[n] != '\r') {
                line.push_back(text[n]);
            }
        }
        const bool blank = trim_copy(text).empty();
        if (ch.paragraphs.empty()) {
            ch.paragraphs.push_back(blank ? std::string(
                "\xe6\x9c\xac\xe9\xa1\xb5\xe6\x97\xa0\xe5\x8f\xaf\xe6\x8f\x90\xe5\x8f\x96\xe6\x96\x87\xe5\xad\x97")
                                         : text);
        }
        ch.html = page_html(text);
        ch.parsed = true;
    }
    book.chapters_indexed = true;
    book.format = BookFormat::Pdf;
}

}  // namespace

bool pdf_summary(const std::filesystem::path& path, Book& book) {
    const std::string bytes = read_all_capped(path);
    book.path = path;
    book.key = to_utf8(path.filename());
    book.title = to_utf8(path.stem());
    book.format = BookFormat::Pdf;
    book.chapters.clear();
    book.chars = 0;
    if (!starts_pdf(bytes)) {
        return false;
    }
    const int pages = count_pages_rough(bytes);
    book.chapter_count = std::max(1, pages);
    book.chapters.resize(static_cast<size_t>(book.chapter_count));
    for (int i = 0; i < book.chapter_count; ++i) {
        book.chapters[static_cast<size_t>(i)].title = page_title(i);
        book.chapters[static_cast<size_t>(i)].file_offset = static_cast<std::uint64_t>(i);
        book.chapters[static_cast<size_t>(i)].parsed = false;
    }
    book.chapters_indexed = true;
    book.index_loaded = true;
    return true;
}

bool load_pdf_window(Book& book, int center, int radius) {
    const std::string bytes = read_all_capped(book.path);
    if (!starts_pdf(bytes)) {
        return false;
    }
    book.format = BookFormat::Pdf;
    if (book.title.empty()) {
        book.title = to_utf8(book.path.stem());
    }
    const int pages = std::max(1, count_pages_rough(bytes));
    std::vector<std::string> texts = extract_page_texts(bytes, pages);
    if (static_cast<int>(texts.size()) < pages) {
        texts.resize(static_cast<size_t>(pages));
    }
    if (center < 0) {
        center = 0;
    }
    if (center >= pages) {
        center = pages - 1;
    }
    const int lo = (std::max)(0, center - radius);
    const int hi = (std::min)(pages - 1, center + radius);
    fill_chapters_from_texts(book, texts, lo, hi, true);
    // Mark outside window unparsed but keep titles.
    for (int i = 0; i < book.chapter_count; ++i) {
        if (i < lo || i > hi) {
            book.chapters[static_cast<size_t>(i)].parsed = false;
            book.chapters[static_cast<size_t>(i)].html.clear();
            book.chapters[static_cast<size_t>(i)].paragraphs.clear();
        }
    }
    book.index_loaded = true;
    return book.chapters[static_cast<size_t>(center)].parsed;
}

void ensure_pdf_index(Book& book) {
    if (book.chapters_indexed && book.chapter_count > 0) {
        return;
    }
    (void)pdf_summary(book.path, book);
}

}  // namespace reader
