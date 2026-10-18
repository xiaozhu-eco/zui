#include "util.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace zdb {
namespace {

std::string grouped(std::int64_t value) {
    const bool negative = value < 0;
    std::string digits = std::to_string(negative ? -value : value);
    std::string out;
    const int lead = static_cast<int>(digits.size() % 3);
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (static_cast<int>(i) - lead) % 3 == 0) {
            out.push_back(',');
        }
        out.push_back(digits[i]);
    }
    if (negative) {
        out.insert(out.begin(), '-');
    }
    return out;
}

std::string one_decimal(double value) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(1) << value;
    return stream.str();
}

std::string hex_byte(unsigned char byte) {
    static const char* kDigits = "0123456789abcdef";
    std::string out(2, '0');
    out[0] = kDigits[byte >> 4];
    out[1] = kDigits[byte & 0x0f];
    return out;
}

}  // namespace

std::string quote_ident(std::string_view name) {
    std::string out;
    out.push_back('"');
    for (char c : name) {
        if (c == '"') {
            out += "\"\"";
        } else {
            out.push_back(c);
        }
    }
    out.push_back('"');
    return out;
}

std::string lower_ascii(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

std::string trim_copy(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

std::string format_count(std::int64_t value) { return grouped(value); }

std::string format_size(std::int64_t bytes) {
    if (bytes <= 0) {
        return {};
    }
    if (bytes < 1024) {
        return std::to_string(bytes) + " B";
    }
    if (bytes < 1048576) {
        return one_decimal(static_cast<double>(bytes) / 1024.0) + " KB";
    }
    return one_decimal(static_cast<double>(bytes) / 1048576.0) + " MB";
}

std::string format_ms(double ms) {
    if (!std::isfinite(ms)) {
        return {};
    }
    if (ms < 10.0) {
        return one_decimal(ms) + " ms";
    }
    if (ms < 1000.0) {
        return std::to_string(static_cast<int>(std::lround(ms))) + " ms";
    }
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(2) << (ms / 1000.0) << " s";
    return stream.str();
}

std::string format_cell(const SqlValue& value) {
    constexpr std::size_t kCap = 240;
    if (value.kind == SqlKind::Null) {
        return "NULL";
    }
    if (value.kind == SqlKind::Blob) {
        const std::string size = format_size(static_cast<std::int64_t>(value.blob.size()));
        return "BLOB " + (size.empty() ? "0 B" : size);
    }
    std::string full;
    if (value.kind == SqlKind::Integer) {
        full = std::to_string(value.integer);
    } else if (value.kind == SqlKind::Real) {
        std::ostringstream stream;
        stream << std::setprecision(15) << value.real;
        full = stream.str();
    } else {
        full = value.text;
    }
    if (full.size() <= kCap) {
        return full;
    }
    std::size_t cut = kCap;
    while (cut > 0 && (static_cast<unsigned char>(full[cut]) & 0xc0) == 0x80) {
        --cut;
    }
    return full.substr(0, cut) + "…";
}

std::string inspect_value(const SqlValue& value) {
    constexpr std::size_t kTextCap = 100000;
    constexpr std::size_t kHexCap = 4096;
    if (value.kind == SqlKind::Null) {
        return "NULL";
    }
    if (value.kind == SqlKind::Integer) {
        return std::to_string(value.integer);
    }
    if (value.kind == SqlKind::Real) {
        std::ostringstream stream;
        stream << std::setprecision(15) << value.real;
        return stream.str();
    }
    if (value.kind == SqlKind::Blob) {
        const std::size_t n = std::min(value.blob.size(), kHexCap);
        std::string lines;
        for (std::size_t offset = 0; offset < n; offset += 16) {
            if (!lines.empty()) {
                lines.push_back('\n');
            }
            const std::string mark = hex_byte(static_cast<unsigned char>((offset >> 8) & 0xff)) +
                                     hex_byte(static_cast<unsigned char>(offset & 0xff));
            lines += mark;
            lines += "  ";
            const std::size_t end = std::min(offset + 16, n);
            for (std::size_t i = offset; i < end; ++i) {
                if (i > offset) {
                    lines.push_back(' ');
                }
                lines += hex_byte(value.blob[i]);
            }
        }
        if (value.blob.size() > kHexCap) {
            lines += "\n…（共 " + format_count(static_cast<std::int64_t>(value.blob.size())) +
                     " 字节，只显示前 " + format_count(static_cast<std::int64_t>(kHexCap)) + " 个）";
        } else {
            lines += "\n（" + format_count(static_cast<std::int64_t>(value.blob.size())) + " 字节）";
        }
        return lines;
    }
    if (value.text.size() <= kTextCap) {
        return value.text;
    }
    return value.text.substr(0, kTextCap) + "\n…（其余 " +
           format_count(static_cast<std::int64_t>(value.text.size() - kTextCap)) + " 个字符已省略）";
}

std::string utf8_path(const std::filesystem::path& path) {
    const auto bytes = path.u8string();
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

}  // namespace zdb
