#include "image/svg/parser/xml_scanner.h"

namespace zui {
namespace image {
namespace svg {
namespace {

std::string decode_entities(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '&') {
            out.push_back(text[i]);
            continue;
        }
        const std::size_t semi = text.find(';', i + 1);
        if (semi == std::string_view::npos) {
            out.push_back('&');
            continue;
        }
        const std::string_view ent = text.substr(i + 1, semi - i - 1);
        if (ent == "amp") {
            out.push_back('&');
        } else if (ent == "lt") {
            out.push_back('<');
        } else if (ent == "gt") {
            out.push_back('>');
        } else if (ent == "quot") {
            out.push_back('"');
        } else if (ent == "apos") {
            out.push_back('\'');
        } else {
            out.append(text.substr(i, semi - i + 1));
        }
        i = semi;
    }
    return out;
}

}  // namespace

XmlScanner::XmlScanner(std::string_view xml)
    : xml_(xml) {
    if (xml_.size() >= 3 && static_cast<unsigned char>(xml_[0]) == 0xEF) {
        xml_.remove_prefix(3);
        i_ = 0;
    }
}

void XmlScanner::skip_ws() {
    while (i_ < xml_.size()) {
        const char c = xml_[i_];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            ++i_;
            continue;
        }
        break;
    }
}

bool XmlScanner::skip_until(char end) {
    while (i_ < xml_.size() && xml_[i_] != end) {
        ++i_;
    }
    if (i_ >= xml_.size()) {
        return false;
    }
    ++i_;
    return true;
}

bool XmlScanner::skip_comment() {
    if (xml_.substr(i_, 4) != "<!--") {
        return false;
    }
    i_ += 4;
    const std::size_t end = xml_.find("-->", i_);
    if (end == std::string_view::npos) {
        error_ = "svg comment";
        return false;
    }
    i_ = end + 3;
    return true;
}

bool XmlScanner::read_name(std::string& out) {
    if (i_ >= xml_.size()) {
        return false;
    }
    const std::size_t begin = i_;
    const char c = xml_[i_];
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == ':')) {
        return false;
    }
    ++i_;
    while (i_ < xml_.size()) {
        const char n = xml_[i_];
        if ((n >= 'A' && n <= 'Z') || (n >= 'a' && n <= 'z') || (n >= '0' && n <= '9') || n == '_' ||
            n == ':' || n == '-' || n == '.') {
            ++i_;
            continue;
        }
        break;
    }
    out.assign(xml_.substr(begin, i_ - begin));
    const std::size_t colon = out.rfind(':');
    if (colon != std::string::npos) {
        out.erase(0, colon + 1);
    }
    return true;
}

bool XmlScanner::read_attr_value(std::string& out) {
    skip_ws();
    if (i_ >= xml_.size()) {
        return false;
    }
    const char quote = xml_[i_];
    if (quote != '"' && quote != '\'') {
        return false;
    }
    ++i_;
    const std::size_t begin = i_;
    while (i_ < xml_.size() && xml_[i_] != quote) {
        ++i_;
    }
    if (i_ >= xml_.size()) {
        return false;
    }
    out = decode_entities(xml_.substr(begin, i_ - begin));
    ++i_;
    return true;
}

bool XmlScanner::has(std::string_view key) const {
    for (const auto& attr : attrs_) {
        if (attr.first == key) {
            return true;
        }
    }
    return false;
}

std::string_view XmlScanner::attr(std::string_view key) const {
    for (const auto& item : attrs_) {
        if (item.first == key) {
            return item.second;
        }
    }
    return {};
}

XmlScanner::Kind XmlScanner::next() {
    attrs_.clear();
    element_.clear();
    while (true) {
        skip_ws();
        if (i_ >= xml_.size()) {
            return Kind::End;
        }
        if (xml_[i_] != '<') {
            if (!skip_until('<')) {
                return Kind::End;
            }
            continue;
        }
        if (xml_.substr(i_, 4) == "<!--") {
            if (!skip_comment()) {
                return Kind::Error;
            }
            continue;
        }
        if (xml_.substr(i_, 2) == "<?" || xml_.substr(i_, 2) == "<!") {
            if (!skip_until('>')) {
                error_ = "svg declaration";
                return Kind::Error;
            }
            continue;
        }
        ++i_;
        const bool closing = i_ < xml_.size() && xml_[i_] == '/';
        if (closing) {
            ++i_;
        }
        if (!read_name(element_)) {
            error_ = "svg tag name";
            return Kind::Error;
        }
        if (closing) {
            skip_ws();
            if (i_ >= xml_.size() || xml_[i_] != '>') {
                error_ = "svg close tag";
                return Kind::Error;
            }
            ++i_;
            return Kind::Close;
        }
        while (true) {
            skip_ws();
            if (i_ >= xml_.size()) {
                error_ = "svg tag";
                return Kind::Error;
            }
            if (xml_[i_] == '>') {
                ++i_;
                return Kind::Open;
            }
            if (xml_[i_] == '/' && i_ + 1 < xml_.size() && xml_[i_ + 1] == '>') {
                i_ += 2;
                return Kind::Empty;
            }
            std::string key;
            if (!read_name(key)) {
                error_ = "svg attribute";
                return Kind::Error;
            }
            skip_ws();
            if (i_ >= xml_.size() || xml_[i_] != '=') {
                error_ = "svg attribute";
                return Kind::Error;
            }
            ++i_;
            std::string value;
            if (!read_attr_value(value)) {
                error_ = "svg attribute value";
                return Kind::Error;
            }
            attrs_.emplace_back(std::move(key), std::move(value));
        }
    }
}

}  // namespace svg
}  // namespace image
}  // namespace zui
