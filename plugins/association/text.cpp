#include "plugins/association/text.h"

namespace zui {
namespace association_detail {
namespace {

std::string xml_escape(std::string_view text) {
    std::string out;
    for (char c : text) {
        switch (c) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case '"':
                out += "&quot;";
                break;
            default:
                out.push_back(c);
                break;
        }
    }
    return out;
}

std::string bare(std::string_view extension) {
    if (!extension.empty() && extension.front() == '.') {
        extension.remove_prefix(1);
    }
    return std::string(extension);
}

}  // namespace

std::string AssociationText::ios_plist(std::string_view extension, std::string_view mime, std::string_view scheme) {
    const std::string ext = xml_escape(bare(extension));
    const std::string type = xml_escape(mime);
    const std::string url = xml_escape(scheme);
    std::string out =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<plist version=\"1.0\"><dict>\n"
        "<key>CFBundleDocumentTypes</key><array><dict>\n"
        "<key>CFBundleTypeExtensions</key><array><string>";
    out += ext;
    out += "</string></array>\n<key>CFBundleTypeName</key><string>";
    out += ext;
    out += "</string>\n<key>LSItemContentTypes</key><array><string>";
    out += type.empty() ? ext : type;
    out += "</string></array>\n</dict></array>\n"
           "<key>CFBundleURLTypes</key><array><dict>\n"
           "<key>CFBundleURLSchemes</key><array><string>";
    out += url;
    out += "</string></array>\n<key>CFBundleURLName</key><string>";
    out += url;
    out += "</string>\n</dict></array>\n</dict></plist>\n";
    return out;
}

std::string AssociationText::android_manifest(std::string_view extension, std::string_view mime,
                                              std::string_view scheme) {
    const std::string ext = xml_escape(bare(extension));
    const std::string type = xml_escape(mime);
    const std::string url = xml_escape(scheme);
    std::string out =
        "<intent-filter>\n"
        "<action android:name=\"android.intent.action.VIEW\"/>\n"
        "<category android:name=\"android.intent.category.DEFAULT\"/>\n"
        "<category android:name=\"android.intent.category.BROWSABLE\"/>\n"
        "<data android:scheme=\"";
    out += url;
    out += "\"/>\n";
    if (!type.empty()) {
        out += "<data android:mimeType=\"";
        out += type;
        out += "\"/>\n";
    }
    if (!ext.empty()) {
        out += "<data android:pathPattern=\".*\\\\.";
        out += ext;
        out += "\"/>\n";
    }
    out += "</intent-filter>\n";
    return out;
}

}  // namespace association_detail
}  // namespace zui
