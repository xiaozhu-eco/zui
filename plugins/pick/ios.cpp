#include "ext/slots.h"
#include "plugins/apple_objc.h"
#include "plugins/pick/command.h"

#include <string>
#include <vector>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
#include <dlfcn.h>
#endif

namespace zui {
namespace backends {
namespace {

#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE

bool g_ready = false;
bool g_cancelled = false;
std::vector<std::string> g_paths;
id g_delegate = nullptr;

id string_array(const std::vector<std::string>& types) {
    id array = apple_objc::msg<id>(apple_objc::cls("NSMutableArray"), apple_objc::sel("array"));
    for (const std::string& type : types) {
        apple_objc::msg<void>(array, apple_objc::sel("addObject:"), apple_objc::ns_string_kept(type));
    }
    return array;
}

void note_urls(id urls) {
    g_paths.clear();
    const auto count = urls != nullptr ? apple_objc::msg<unsigned long>(urls, apple_objc::sel("count")) : 0;
    for (unsigned long i = 0; i < count; ++i) {
        id url = apple_objc::msg<id>(urls, apple_objc::sel("objectAtIndex:"), i);
        id path = apple_objc::msg<id>(url, apple_objc::sel("path"));
        const char* utf8 = path != nullptr ? apple_objc::msg<const char*>(path, apple_objc::sel("UTF8String")) : nullptr;
        if (utf8 != nullptr && utf8[0] != '\0') {
            g_paths.emplace_back(utf8);
        }
    }
    g_cancelled = g_paths.empty();
    g_ready = true;
}

void did_pick(id, SEL, id, id urls) { note_urls(urls); }

void did_pick_one(id, SEL, id, id url) {
    id urls = apple_objc::msg<id>(apple_objc::cls("NSMutableArray"), apple_objc::sel("array"));
    if (url != nullptr) {
        apple_objc::msg<void>(urls, apple_objc::sel("addObject:"), url);
    }
    note_urls(urls);
}

void was_cancelled(id, SEL, id) {
    g_paths.clear();
    g_cancelled = true;
    g_ready = true;
}

id picker_delegate() {
    if (g_delegate != nullptr) {
        return g_delegate;
    }
    Class pair = reinterpret_cast<Class>(apple_objc::cls("ZuiDocumentPickerDelegate"));
    if (pair == nullptr) {
        pair = objc_allocateClassPair(reinterpret_cast<Class>(apple_objc::cls("NSObject")), "ZuiDocumentPickerDelegate", 0);
        if (pair == nullptr) {
            return nullptr;
        }
        class_addMethod(pair, apple_objc::sel("documentPicker:didPickDocumentsAtURLs:"), reinterpret_cast<IMP>(did_pick),
                        "v@:@@");
        class_addMethod(pair, apple_objc::sel("documentPicker:didPickDocumentAtURL:"), reinterpret_cast<IMP>(did_pick_one),
                        "v@:@@");
        class_addMethod(pair, apple_objc::sel("documentPickerWasCancelled:"), reinterpret_cast<IMP>(was_cancelled), "v@:@");
        objc_registerClassPair(pair);
    }
    g_delegate = apple_objc::msg<id>(apple_objc::msg<id>(reinterpret_cast<id>(pair), apple_objc::sel("alloc")),
                                    apple_objc::sel("init"));
    if (g_delegate != nullptr) {
        g_delegate = apple_objc::msg<id>(g_delegate, apple_objc::sel("retain"));
    }
    return g_delegate;
}

bool responds(id object, const char* name) {
    return object != nullptr && apple_objc::msg<signed char>(object, apple_objc::sel("respondsToSelector:"),
                                                             apple_objc::sel(name)) != 0;
}

id alloc_picker() {
    return apple_objc::msg<id>(apple_objc::cls("UIDocumentPickerViewController"), apple_objc::sel("alloc"));
}

id uttype_array(const std::vector<std::string>& types) {
    id array = apple_objc::msg<id>(apple_objc::cls("NSMutableArray"), apple_objc::sel("array"));
    for (const std::string& type : types) {
        id uttype = apple_objc::msg<id>(apple_objc::cls("UTType"), apple_objc::sel("typeWithIdentifier:"),
                                       apple_objc::ns_string_kept(type));
        if (uttype != nullptr) {
            apple_objc::msg<void>(array, apple_objc::sel("addObject:"), uttype);
        }
    }
    return array;
}

id open_picker(const std::vector<std::string>& types) {
    id picker = alloc_picker();
    if (responds(picker, "initWithDocumentTypes:inMode:")) {
        return apple_objc::msg<id>(picker, apple_objc::sel("initWithDocumentTypes:inMode:"), string_array(types), 1ul);
    }
    if (picker != nullptr) {
        apple_objc::msg<void>(picker, apple_objc::sel("release"));
    }
    picker = alloc_picker();
    if (!responds(picker, "initForOpeningContentTypes:asCopy:")) {
        if (picker != nullptr) {
            apple_objc::msg<void>(picker, apple_objc::sel("release"));
        }
        return nullptr;
    }
    return apple_objc::msg<id>(picker, apple_objc::sel("initForOpeningContentTypes:asCopy:"), uttype_array(types),
                              static_cast<signed char>(1));
}

id export_urls(const FileDialogOptions& opt) {
    using TempDir = id (*)();
    auto temp_dir = reinterpret_cast<TempDir>(dlsym(RTLD_DEFAULT, "NSTemporaryDirectory"));
    id root = temp_dir != nullptr ? temp_dir() : nullptr;
    if (root == nullptr) {
        return nullptr;
    }
    const std::string name = opt.default_name.empty() ? std::string("untitled") : opt.default_name;
    id path = apple_objc::msg<id>(root, apple_objc::sel("stringByAppendingPathComponent:"), apple_objc::ns_string_kept(name));
    id url = apple_objc::msg<id>(apple_objc::cls("NSURL"), apple_objc::sel("fileURLWithPath:"), path);
    if (url == nullptr) {
        return nullptr;
    }
    id urls = apple_objc::msg<id>(apple_objc::cls("NSMutableArray"), apple_objc::sel("array"));
    apple_objc::msg<void>(urls, apple_objc::sel("addObject:"), url);
    return urls;
}

id export_picker(const FileDialogOptions& opt, const std::vector<std::string>& types) {
    id urls = export_urls(opt);
    id picker = alloc_picker();
    if (urls != nullptr && responds(picker, "initForExportingURLs:asCopy:")) {
        return apple_objc::msg<id>(picker, apple_objc::sel("initForExportingURLs:asCopy:"), urls,
                                  static_cast<signed char>(1));
    }
    if (picker != nullptr) {
        apple_objc::msg<void>(picker, apple_objc::sel("release"));
    }
    picker = alloc_picker();
    if (!responds(picker, "initWithDocumentTypes:inMode:")) {
        if (picker != nullptr) {
            apple_objc::msg<void>(picker, apple_objc::sel("release"));
        }
        return nullptr;
    }
    return apple_objc::msg<id>(picker, apple_objc::sel("initWithDocumentTypes:inMode:"), string_array(types), 2ul);
}

bool wait_for_delegate() {
    if (g_ready) {
        return true;
    }
    id run_loop = apple_objc::msg<id>(apple_objc::cls("NSRunLoop"), apple_objc::sel("currentRunLoop"));
    if (run_loop == nullptr) {
        return false;
    }
    const std::string mode_name = "NSDefaultRunLoopMode";
    id mode = apple_objc::ns_string_kept(mode_name);
    id until = apple_objc::msg<id>(apple_objc::cls("NSDate"), apple_objc::sel("distantFuture"));
    id pool = apple_objc::msg<id>(apple_objc::msg<id>(apple_objc::cls("NSAutoreleasePool"), apple_objc::sel("alloc")),
                                 apple_objc::sel("init"));
    while (!g_ready) {
        const signed char ran =
            apple_objc::msg<signed char>(run_loop, apple_objc::sel("runMode:beforeDate:"), mode, until);
        if (ran == 0) {
            break;
        }
    }
    if (pool != nullptr) {
        apple_objc::msg<void>(pool, apple_objc::sel("drain"));
    }
    return g_ready;
}

bool present_picker(const char* mode, const FileDialogOptions& opt) {
    g_ready = false;
    g_cancelled = false;
    g_paths.clear();
    if (apple_objc::cls("UIDocumentPickerViewController") == nullptr) {
        return false;
    }
    id picker = nullptr;
    const bool folder = std::string(mode) == "folder";
    const bool save = std::string(mode) == "export";
    if (folder) {
        picker = open_picker({"public.folder"});
    } else if (save) {
        picker = export_picker(opt, pick_detail::DialogCommand::ios_types(opt.filters));
    } else {
        picker = open_picker(pick_detail::DialogCommand::ios_types(opt.filters));
    }
    id delegate = picker_delegate();
    if (picker == nullptr || delegate == nullptr || !responds(picker, "setDelegate:")) {
        if (picker != nullptr) {
            apple_objc::msg<void>(picker, apple_objc::sel("release"));
        }
        return false;
    }
    apple_objc::msg<void>(picker, apple_objc::sel("setDelegate:"), delegate);
    if (!opt.title.empty() && responds(picker, "setTitle:")) {
        const std::string title = opt.title;
        apple_objc::msg<void>(picker, apple_objc::sel("setTitle:"), apple_objc::ns_string_kept(title));
    }
    if (!save && !folder && responds(picker, "setAllowsMultipleSelection:")) {
        apple_objc::msg<void>(picker, apple_objc::sel("setAllowsMultipleSelection:"),
                              static_cast<signed char>(opt.multi ? 1 : 0));
    }
    id app = apple_objc::msg<id>(apple_objc::cls("UIApplication"), apple_objc::sel("sharedApplication"));
    id window = apple_objc::msg<id>(app, apple_objc::sel("keyWindow"));
    id root = apple_objc::msg<id>(window, apple_objc::sel("rootViewController"));
    if (root == nullptr || !responds(root, "presentViewController:animated:completion:")) {
        apple_objc::msg<void>(picker, apple_objc::sel("release"));
        return false;
    }
    apple_objc::msg<void>(root, apple_objc::sel("presentViewController:animated:completion:"), picker,
                          static_cast<signed char>(1), nullptr);
    const bool delivered = wait_for_delegate();
    apple_objc::msg<void>(picker, apple_objc::sel("release"));
    return delivered;
}

class IosDialogs final : public FileDialogService {
public:
    PickPaths open(NativeWindowId, const FileDialogOptions& opt) override {
        if (!present_picker("open", opt)) {
            return {PickStatus::Unsupported, {}};
        }
        if (g_cancelled || g_paths.empty()) {
            return {PickStatus::Cancelled, {}};
        }
        return {PickStatus::Ok, g_paths};
    }
    PickPath save(NativeWindowId, const FileDialogOptions& opt) override {
        if (!present_picker("export", opt)) {
            return {PickStatus::Unsupported, {}};
        }
        if (g_cancelled || g_paths.empty()) {
            return {PickStatus::Cancelled, {}};
        }
        return {PickStatus::Ok, g_paths.front()};
    }
    PickPath folder(NativeWindowId, const FileDialogOptions& opt) override {
        if (!present_picker("folder", opt)) {
            return {PickStatus::Unsupported, {}};
        }
        if (g_cancelled || g_paths.empty()) {
            return {PickStatus::Cancelled, {}};
        }
        return {PickStatus::Ok, g_paths.front()};
    }
};

#endif

}  // namespace

void register_ios_dialogs(ExtensionRegistry& registry) {
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
    registry.add_dialogs(PlatformKind::Ios, std::make_unique<IosDialogs>());
#else
    (void)registry;
#endif
}

}  // namespace backends
}  // namespace zui
