#include "plugins/association/macos_store.h"

#include "plugins/apple_objc.h"

#include <string>
#include <unordered_map>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if defined(__APPLE__) && !(defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
#include <dlfcn.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace zui {
namespace association_macos {
namespace {

bool ends_with_app(std::string_view path) {
    return path.size() >= 4 && (path.compare(path.size() - 4, 4, ".app") == 0 ||
                                path.compare(path.size() - 4, 4, ".APP") == 0);
}

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

#if defined(__APPLE__) && !(defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)

std::unordered_map<std::string, std::string>& remembered_bundles() {
    static std::unordered_map<std::string, std::string> bundles;
    return bundles;
}

const char* lsregister_tool() {
    return "/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister";
}

bool run_lsregister(const char* flag, const std::string& path, unsigned long* error) {
    const char* tool = lsregister_tool();
    if (path.empty() || access(tool, X_OK) != 0) {
        return false;
    }
    std::string tool_path = tool;
    std::string flag_text = flag;
    std::string exe = path;
    char* argv[] = {tool_path.data(), flag_text.data(), exe.data(), nullptr};
    pid_t pid = 0;
    const int rc = posix_spawn(&pid, tool, nullptr, nullptr, argv, environ);
    if (rc != 0) {
        if (error != nullptr) {
            *error = static_cast<unsigned long>(rc);
        }
        return false;
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0 || status != 0) {
        if (error != nullptr) {
            *error = static_cast<unsigned long>(status);
        }
        return false;
    }
    return true;
}

std::string bundle_identifier(const association_detail::Record& record) {
    if (ends_with_app(record.executable)) {
        const std::string exe = record.executable;
        id bundle = apple_objc::msg<id>(apple_objc::cls("NSBundle"), apple_objc::sel("bundleWithPath:"),
                                        apple_objc::ns_string_kept(exe));
        id ident = apple_objc::msg<id>(bundle, apple_objc::sel("bundleIdentifier"));
        const char* utf8 = ident != nullptr ? apple_objc::msg<const char*>(ident, apple_objc::sel("UTF8String")) : nullptr;
        if (utf8 != nullptr && utf8[0] != '\0') {
            return std::string(utf8);
        }
    }
    return record.prog_id;
}

#endif

}  // namespace

plugin::OpenerReport MacAssociationStore::write(const association_detail::Record& record, plugin::OpenerReport report) {
#if defined(__APPLE__) && !(defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    report.ran = true;
    const bool bundle = ends_with_app(record.executable);
    if (bundle) {
        remembered_bundles()[record.prog_id] = record.executable;
        unsigned long error = 0;
        if (!run_lsregister("-f", record.executable, &error) && error != 0) {
            report.os_error = error;
        }
    }
    const std::string handler = bundle_identifier(record);
    bool ok = true;
    bool requested = false;
    if (!record.extension.empty()) {
        requested = true;
        using SetType = int (*)(id, unsigned int, id);
        auto set_type = reinterpret_cast<SetType>(dlsym(RTLD_DEFAULT, "LSSetDefaultRoleHandlerForContentType"));
        const std::string uti = uti_for_extension(record.extension);
        const int rc = set_type != nullptr ? set_type(apple_objc::ns_string_kept(uti), 0xFFFFFFFFu,
                                                      apple_objc::ns_string_kept(handler))
                                           : -1;
        ok = rc == 0;
        if (rc != 0) {
            report.os_error = static_cast<unsigned long>(rc);
        }
    }
    if (!record.scheme.empty()) {
        requested = true;
        using SetScheme = int (*)(id, id);
        auto set_scheme = reinterpret_cast<SetScheme>(dlsym(RTLD_DEFAULT, "LSSetDefaultHandlerForURLScheme"));
        const std::string scheme = record.scheme;
        const int rc = set_scheme != nullptr ? set_scheme(apple_objc::ns_string_kept(scheme),
                                                          apple_objc::ns_string_kept(handler))
                                             : -1;
        ok = ok && rc == 0;
        if (rc != 0) {
            report.os_error = static_cast<unsigned long>(rc);
        }
    }
    report.ok = requested && ok;
#else
    (void)record;
#endif
    return report;
}

bool MacAssociationStore::remove(const association_detail::Record& record) {
#if defined(__APPLE__) && !(defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    std::string path;
    const auto found = remembered_bundles().find(record.prog_id);
    if (found != remembered_bundles().end()) {
        path = found->second;
    } else if (ends_with_app(record.executable)) {
        path = record.executable;
    }
    if (!ends_with_app(path)) {
        return false;
    }
    unsigned long error = 0;
    const bool removed = run_lsregister("-u", path, &error);
    if (removed) {
        remembered_bundles().erase(record.prog_id);
    }
    return removed;
#else
    (void)record;
    return false;
#endif
}

}  // namespace association_macos
}  // namespace zui
