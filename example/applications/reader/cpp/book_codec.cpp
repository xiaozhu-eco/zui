#include "book_detail.h"

#include <cstdint>
#include <fstream>
#include <iterator>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace reader {
bool starts_with(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}


void append_utf8(std::string& out, std::uint32_t cp) {
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

size_t cp_count(std::string_view s) {
    size_t n = 0;
    for (size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        size_t step = 1;
        if ((c & 0x80) == 0) {
            step = 1;
        } else if ((c & 0xe0) == 0xc0) {
            step = 2;
        } else if ((c & 0xf0) == 0xe0) {
            step = 3;
        } else if ((c & 0xf8) == 0xf0) {
            step = 4;
        }
        if (i + step > s.size()) {
            break;
        }
        i += step;
        ++n;
    }
    return n;
}

std::string first_cps(std::string_view s, size_t count) {
    size_t i = 0;
    size_t n = 0;
    while (i < s.size() && n < count) {
        const auto c = static_cast<unsigned char>(s[i]);
        size_t step = 1;
        if ((c & 0x80) == 0) {
            step = 1;
        } else if ((c & 0xe0) == 0xc0) {
            step = 2;
        } else if ((c & 0xf0) == 0xe0) {
            step = 3;
        } else if ((c & 0xf8) == 0xf0) {
            step = 4;
        }
        if (i + step > s.size()) {
            break;
        }
        i += step;
        ++n;
    }
    return std::string(s.substr(0, i));
}

bool valid_utf8(std::string_view s) {
    size_t i = 0;
    while (i < s.size()) {
        const auto c = static_cast<unsigned char>(s[i]);
        size_t need = 1;
        if (c <= 0x7f) {
            need = 1;
        } else if ((c & 0xe0) == 0xc0 && c >= 0xc2) {
            need = 2;
        } else if ((c & 0xf0) == 0xe0) {
            need = 3;
        } else if ((c & 0xf8) == 0xf0 && c <= 0xf4) {
            need = 4;
        } else {
            return false;
        }
        if (i + need > s.size()) {
            return false;
        }
        for (size_t k = 1; k < need; ++k) {
            if ((static_cast<unsigned char>(s[i + k]) & 0xc0) != 0x80) {
                return false;
            }
        }
        if (need == 3) {
            const auto c1 = static_cast<unsigned char>(s[i + 1]);
            if (c == 0xe0 && c1 < 0xa0) {
                return false;
            }
            if (c == 0xed && c1 >= 0xa0) {
                return false;
            }
        }
        if (need == 4) {
            const auto c1 = static_cast<unsigned char>(s[i + 1]);
            if (c == 0xf0 && c1 < 0x90) {
                return false;
            }
            if (c == 0xf4 && c1 >= 0x90) {
                return false;
            }
        }
        i += need;
    }
    return true;
}

std::string utf16le_to_utf8(std::string_view bytes) {
    size_t i = 0;
    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xff &&
        static_cast<unsigned char>(bytes[1]) == 0xfe) {
        i = 2;
    }
    std::string out;
    while (i + 1 < bytes.size()) {
        std::uint32_t cu =
            static_cast<unsigned char>(bytes[i]) | (static_cast<unsigned char>(bytes[i + 1]) << 8);
        i += 2;
        if (cu >= 0xd800 && cu <= 0xdbff && i + 1 < bytes.size()) {
            const std::uint32_t low = static_cast<unsigned char>(bytes[i]) |
                                      (static_cast<unsigned char>(bytes[i + 1]) << 8);
            if (low >= 0xdc00 && low <= 0xdfff) {
                cu = 0x10000 + (((cu - 0xd800) << 10) | (low - 0xdc00));
                i += 2;
            }
        }
        append_utf8(out, cu);
    }
    return out;
}

#ifdef _WIN32
std::string codepage_to_utf8(unsigned cp, std::string_view bytes) {
    if (bytes.empty() || bytes.size() > static_cast<size_t>(0x7fffffff)) {
        return {};
    }
    const int in = static_cast<int>(bytes.size());
    const DWORD flags[] = {MB_ERR_INVALID_CHARS, 0};
    for (DWORD flag : flags) {
        const int wlen = MultiByteToWideChar(cp, flag, bytes.data(), in, nullptr, 0);
        if (wlen <= 0) {
            continue;
        }
        std::wstring wide(static_cast<size_t>(wlen), L'\0');
        if (MultiByteToWideChar(cp, flag, bytes.data(), in, wide.data(), wlen) <= 0) {
            continue;
        }
        const int n =
            WideCharToMultiByte(CP_UTF8, 0, wide.data(), wlen, nullptr, 0, nullptr, nullptr);
        if (n <= 0) {
            return {};
        }
        std::string out(static_cast<size_t>(n), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), wlen, out.data(), n, nullptr, nullptr);
        return out;
    }
    return {};
}
#endif

bool zh_title_less(std::string_view a, std::string_view b) {
#ifdef _WIN32
    // Windows NLS zh-CN collates Han text by pinyin, which is the order Chrome's ICU gives for
    // `localeCompare(..., "zh")` on this host. Guard the conversion: a bad byte just falls back.
    if (!a.empty() && !b.empty()) {
        const int na = static_cast<int>(a.size());
        const int nb = static_cast<int>(b.size());
        const int wa = MultiByteToWideChar(CP_UTF8, 0, a.data(), na, nullptr, 0);
        const int wb = MultiByteToWideChar(CP_UTF8, 0, b.data(), nb, nullptr, 0);
        if (wa > 0 && wb > 0) {
            std::wstring a16(static_cast<size_t>(wa), L'\0');
            std::wstring b16(static_cast<size_t>(wb), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, a.data(), na, a16.data(), wa);
            MultiByteToWideChar(CP_UTF8, 0, b.data(), nb, b16.data(), wb);
            const int r = CompareStringEx(L"zh-CN", 0, a16.data(), wa, b16.data(), wb, nullptr,
                                          nullptr, 0);
            if (r != 0) {
                return r == CSTR_LESS_THAN;
            }
        }
    }
#endif
    return a < b;
}

std::string to_utf8(const std::filesystem::path& path) {
#ifdef _WIN32
    const std::wstring wide = path.wstring();
    if (wide.empty()) {
        return {};
    }
    const int n = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                      nullptr, 0, nullptr, nullptr);
    if (n <= 0) {
        return {};
    }
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), n,
                        nullptr, nullptr);
    return out;
#else
    return path.string();
#endif
}

class BookBytesDecoder {
public:
    virtual ~BookBytesDecoder() = default;
    /// Rewrites @p bytes when this encoding matches. A false result may still
    /// have stripped a BOM, which is what the later code-page attempts expect.
    virtual bool take(std::string& bytes) const = 0;
};

class Utf16LeDecoder final : public BookBytesDecoder {
public:
    bool take(std::string& bytes) const override {
        if (bytes.size() < 2 || static_cast<unsigned char>(bytes[0]) != 0xff ||
            static_cast<unsigned char>(bytes[1]) != 0xfe) {
            return false;
        }
        bytes = utf16le_to_utf8(bytes);
        return true;
    }
};

class Utf8Decoder final : public BookBytesDecoder {
public:
    bool take(std::string& bytes) const override {
        if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xef &&
            static_cast<unsigned char>(bytes[1]) == 0xbb &&
            static_cast<unsigned char>(bytes[2]) == 0xbf) {
            bytes.erase(0, 3);
        }
        return valid_utf8(bytes);
    }
};

#ifdef _WIN32
class CodePageDecoder final : public BookBytesDecoder {
public:
    explicit CodePageDecoder(unsigned cp) : cp_(cp) {}

    bool take(std::string& bytes) const override {
        std::string converted = codepage_to_utf8(cp_, bytes);
        if (converted.empty()) {
            return false;
        }
        bytes = std::move(converted);
        return true;
    }

private:
    unsigned cp_;
};
#endif
std::string decode_book(std::string bytes) {
    // Each object owns one encoding the shelf actually meets. The first one that
    // accepts the bytes wins, in the same order the old cascade used: UTF-16 LE
    // BOM, then UTF-8 (BOM stripped even when the rest is not UTF-8), then
    // GB18030, then GBK. Anything left is returned unchanged.
    const Utf16LeDecoder utf16;
    if (utf16.take(bytes)) {
        return bytes;
    }
    const Utf8Decoder utf8;
    if (utf8.take(bytes)) {
        return bytes;
    }
#ifdef _WIN32
    const CodePageDecoder gb18030(54936);
    if (gb18030.take(bytes)) {
        return bytes;
    }
    const CodePageDecoder gbk(936);
    if (gbk.take(bytes)) {
        return bytes;
    }
#endif
    return bytes;
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

namespace {

/// A sample cut off at a fixed size may end inside a UTF-8 sequence. That tail is not
/// evidence the file is some other encoding.
bool acceptable_utf8_prefix(std::string_view s) {
    size_t i = 0;
    while (i < s.size()) {
        const auto c = static_cast<unsigned char>(s[i]);
        size_t need = 1;
        if (c <= 0x7f) {
            need = 1;
        } else if ((c & 0xe0) == 0xc0 && c >= 0xc2) {
            need = 2;
        } else if ((c & 0xf0) == 0xe0) {
            need = 3;
        } else if ((c & 0xf8) == 0xf0 && c <= 0xf4) {
            need = 4;
        } else {
            return false;
        }
        if (i + need > s.size()) {
            return true;
        }
        for (size_t k = 1; k < need; ++k) {
            if ((static_cast<unsigned char>(s[i + k]) & 0xc0) != 0x80) {
                return false;
            }
        }
        if (need == 3) {
            const auto c1 = static_cast<unsigned char>(s[i + 1]);
            if (c == 0xe0 && c1 < 0xa0) {
                return false;
            }
            if (c == 0xed && c1 >= 0xa0) {
                return false;
            }
        }
        if (need == 4) {
            const auto c1 = static_cast<unsigned char>(s[i + 1]);
            if (c == 0xf0 && c1 < 0x90) {
                return false;
            }
            if (c == 0xf4 && c1 >= 0x90) {
                return false;
            }
        }
        i += need;
    }
    return true;
}

}  // namespace

BookEncoding detect_book_encoding(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return BookEncoding::Raw;
    }
    constexpr std::size_t kSample = 256 * 1024;
    std::string buf(kSample, '\0');
    in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
    buf.resize(static_cast<std::size_t>(in.gcount()));
    if (buf.size() >= 2 && static_cast<unsigned char>(buf[0]) == 0xff &&
        static_cast<unsigned char>(buf[1]) == 0xfe) {
        return BookEncoding::Utf16Le;
    }
    if (buf.size() >= 3 && static_cast<unsigned char>(buf[0]) == 0xef &&
        static_cast<unsigned char>(buf[1]) == 0xbb &&
        static_cast<unsigned char>(buf[2]) == 0xbf) {
        return BookEncoding::Utf8;
    }
    if (acceptable_utf8_prefix(buf)) {
        return BookEncoding::Utf8;
    }
#ifdef _WIN32
    if (!buf.empty() && !codepage_to_utf8(54936, buf).empty()) {
        return BookEncoding::Gb18030;
    }
    if (!buf.empty() && !codepage_to_utf8(936, buf).empty()) {
        return BookEncoding::Gbk;
    }
#endif
    return BookEncoding::Raw;
}

std::string decode_file_line(BookEncoding encoding, std::string_view bytes) {
    if (bytes.empty()) {
        return {};
    }
    switch (encoding) {
        case BookEncoding::Utf16Le:
            return utf16le_to_utf8(bytes);
        case BookEncoding::Gb18030:
#ifdef _WIN32
            return codepage_to_utf8(54936, bytes);
#else
            return std::string(bytes);
#endif
        case BookEncoding::Gbk:
#ifdef _WIN32
            return codepage_to_utf8(936, bytes);
#else
            return std::string(bytes);
#endif
        case BookEncoding::Utf8:
        case BookEncoding::Raw:
            break;
    }
    return std::string(bytes);
}

}  // namespace reader
