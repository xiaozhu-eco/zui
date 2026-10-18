#include "plugins/association/ios_store.h"

#include "plugins/apple_objc.h"
#include "plugins/association/text.h"

#include <string>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
#include <dlfcn.h>
#endif

namespace zui {
namespace association_ios {
namespace {

std::string bare_extension(std::string_view extension) {
    if (!extension.empty() && extension.front() == '.') {
        extension.remove_prefix(1);
    }
    return std::string(extension);
}

std::string uti_for_extension(std::string_view extension) {
    const std::string ext = bare_extension(extension);
    if (ext == "png") {
        return "public.png";
    }
    if (ext == "jpg" || ext == "jpeg") {
        return "public.jpeg";
    }
    if (ext == "gif") {
        return "com.compuserve.gif";
    }
    if (ext == "pdf") {
        return "com.adobe.pdf";
    }
    if (ext == "html" || ext == "htm") {
        return "public.html";
    }
    if (ext == "json") {
        return "public.json";
    }
    if (ext == "txt" || ext == "md" || ext == "text") {
        return "public.plain-text";
    }
    return "public.data";
}

#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE

id array_with(const std::string& text) {
    id array = apple_objc::msg<id>(apple_objc::cls("NSMutableArray"), apple_objc::sel("array"));
    apple_objc::msg<void>(array, apple_objc::sel("addObject:"), apple_objc::ns_string_kept(text));
    return array;
}

void put_entry(id dict, const std::string& key, id value) {
    apple_objc::msg<void>(dict, apple_objc::sel("setObject:forKey:"), value, apple_objc::ns_string_kept(key));
}

#endif

}  // namespace

plugin::OpenerReport IosAssociationStore::write(const association_detail::Record& record, plugin::OpenerReport report) {
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
    report.ran = true;
    const std::string plist =
        association_detail::AssociationText::ios_plist(record.extension, record.mime, record.scheme);
    report.desktop_entry = plist;
    const bool need_docs = !record.extension.empty();
    const bool need_scheme = !record.scheme.empty();
    bool docs_ok = !need_docs;
    bool scheme_ok = !need_scheme;
    id dict = apple_objc::msg<id>(apple_objc::cls("NSMutableDictionary"), apple_objc::sel("dictionary"));
    if (need_docs && dict != nullptr) {
        const std::string bare = bare_extension(record.extension);
        const std::string uti = uti_for_extension(record.extension);
        const std::string key_docs = "CFBundleDocumentTypes";
        const std::string key_ext = "CFBundleTypeExtensions";
        const std::string key_name = "CFBundleTypeName";
        const std::string key_types = "LSItemContentTypes";
        id doc = apple_objc::msg<id>(apple_objc::cls("NSMutableDictionary"), apple_objc::sel("dictionary"));
        put_entry(doc, key_ext, array_with(bare));
        put_entry(doc, key_name, apple_objc::ns_string_kept(bare));
        put_entry(doc, key_types, array_with(uti));
        id docs = apple_objc::msg<id>(apple_objc::cls("NSMutableArray"), apple_objc::sel("array"));
        apple_objc::msg<void>(docs, apple_objc::sel("addObject:"), doc);
        put_entry(dict, key_docs, docs);
        docs_ok = doc != nullptr && docs != nullptr;
    }
    if (need_scheme && dict != nullptr) {
        const std::string scheme = record.scheme;
        const std::string prog = record.prog_id;
        const std::string key_urls = "CFBundleURLTypes";
        const std::string key_schemes = "CFBundleURLSchemes";
        const std::string key_url_name = "CFBundleURLName";
        id url = apple_objc::msg<id>(apple_objc::cls("NSMutableDictionary"), apple_objc::sel("dictionary"));
        put_entry(url, key_schemes, array_with(scheme));
        put_entry(url, key_url_name, apple_objc::ns_string_kept(prog));
        id urls = apple_objc::msg<id>(apple_objc::cls("NSMutableArray"), apple_objc::sel("array"));
        apple_objc::msg<void>(urls, apple_objc::sel("addObject:"), url);
        put_entry(dict, key_urls, urls);
        using SetHandler = int (*)(void*, void*);
        auto set_handler = reinterpret_cast<SetHandler>(dlsym(RTLD_DEFAULT, "LSSetDefaultHandlerForURLScheme"));
        if (set_handler == nullptr || url == nullptr) {
            scheme_ok = false;
        } else {
            const int rc = set_handler(apple_objc::ns_string_kept(scheme), apple_objc::ns_string_kept(prog));
            scheme_ok = rc == 0;
            if (rc != 0) {
                report.os_error = static_cast<unsigned long>(rc);
            }
        }
    }
    if (need_docs) {
        id workspace = apple_objc::msg<id>(apple_objc::cls("LSApplicationWorkspace"), apple_objc::sel("defaultWorkspace"));
        const bool responds = workspace != nullptr &&
                              apple_objc::msg<signed char>(workspace, apple_objc::sel("respondsToSelector:"),
                                                           apple_objc::sel("registerApplicationDictionary:")) != 0;
        if (!docs_ok || !responds) {
            docs_ok = false;
        } else {
            apple_objc::msg<void>(workspace, apple_objc::sel("registerApplicationDictionary:"), dict);
            docs_ok = true;
        }
    }
    report.ok = (need_docs || need_scheme) && docs_ok && scheme_ok;
#else
    (void)record;
#endif
    return report;
}

bool IosAssociationStore::remove(const association_detail::Record& record) {
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
    id workspace = apple_objc::msg<id>(apple_objc::cls("LSApplicationWorkspace"), apple_objc::sel("defaultWorkspace"));
    if (workspace == nullptr) {
        return false;
    }
    const std::string prog = record.prog_id;
    const bool responds = apple_objc::msg<signed char>(workspace, apple_objc::sel("respondsToSelector:"),
                                                       apple_objc::sel("unregisterApplication:")) != 0;
    if (!responds || prog.empty()) {
        return false;
    }
    apple_objc::msg<void>(workspace, apple_objc::sel("unregisterApplication:"), apple_objc::ns_string_kept(prog));
    return true;
#else
    (void)record;
    return false;
#endif
}

}  // namespace association_ios
}  // namespace zui
