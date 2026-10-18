#include "backends/posix_platform_util.h"
#include "ext/slots.h"
#include "plugins/apple_objc.h"
#include "plugins/clipboard/bmp.h"

#include <string>
#include <vector>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace zui {
namespace backends {
namespace {

#if defined(__APPLE__) && !(defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)

class MacClipboard final : public ClipboardService {
public:
    ClipboardText text() override {
        ClipboardText out;
        id board = apple_objc::msg<id>(apple_objc::cls("NSPasteboard"), apple_objc::sel("generalPasteboard"));
        const std::string kind = "public.utf8-plain-text";
        id value = apple_objc::msg<id>(board, apple_objc::sel("stringForType:"), apple_objc::ns_string_kept(kind));
        const char* utf8 = value != nullptr ? apple_objc::msg<const char*>(value, apple_objc::sel("UTF8String"))
                                            : nullptr;
        if (utf8 == nullptr) {
            std::string captured;
            int code = -1;
            if (posix_util::run_command_capture("pbpaste 2>/dev/null", captured, code) && code == 0) {
                out.status = ClipboardStatus::Ok;
                out.text = std::move(captured);
            }
            return out;
        }
        out.status = ClipboardStatus::Ok;
        out.text = utf8;
        return out;
    }

    bool set_text(std::string_view value) override {
        id board = apple_objc::msg<id>(apple_objc::cls("NSPasteboard"), apple_objc::sel("generalPasteboard"));
        if (board == nullptr) {
            const std::string cmd = "printf %s " + posix_util::shell_quote(value) + " | pbcopy";
            std::string out;
            int code = -1;
            return posix_util::run_command_capture(cmd, out, code);
        }
        apple_objc::msg<void>(board, apple_objc::sel("clearContents"));
        const std::string owned(value);
        const std::string kind = "public.utf8-plain-text";
        return apple_objc::msg<bool>(board, apple_objc::sel("setString:forType:"), apple_objc::ns_string_kept(owned),
                                     apple_objc::ns_string_kept(kind));
    }

    ClipboardImage image() override {
        id board = apple_objc::msg<id>(apple_objc::cls("NSPasteboard"), apple_objc::sel("generalPasteboard"));
        const std::string kind = "com.microsoft.bmp";
        id data = board != nullptr ? apple_objc::msg<id>(board, apple_objc::sel("dataForType:"),
                                                         apple_objc::ns_string_kept(kind))
                                   : nullptr;
        if (data == nullptr) {
            return {};
        }
        const auto* bytes = apple_objc::msg<const std::uint8_t*>(data, apple_objc::sel("bytes"));
        const unsigned long length = apple_objc::msg<unsigned long>(data, apple_objc::sel("length"));
        if (bytes == nullptr || length == 0) {
            return {};
        }
        return clipboard_bmp::decode(bytes, static_cast<std::size_t>(length));
    }

    bool set_image(const ClipboardImage& image) override {
        const std::vector<std::uint8_t> bmp =
            clipboard_bmp::encode(image.width, image.height, image.rgba.data(), image.rgba.size());
        if (bmp.empty()) {
            return false;
        }
        id data = apple_objc::msg<id>(apple_objc::cls("NSData"), apple_objc::sel("dataWithBytes:length:"), bmp.data(),
                                      static_cast<unsigned long>(bmp.size()));
        id board = apple_objc::msg<id>(apple_objc::cls("NSPasteboard"), apple_objc::sel("generalPasteboard"));
        if (data == nullptr || board == nullptr) {
            return false;
        }
        apple_objc::msg<void>(board, apple_objc::sel("clearContents"));
        const std::string kind = "com.microsoft.bmp";
        return apple_objc::msg<bool>(board, apple_objc::sel("setData:forType:"), data, apple_objc::ns_string_kept(kind));
    }
};

#endif

}  // namespace

void register_macos_clipboard(ExtensionRegistry& registry) {
#if defined(__APPLE__) && !(defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
    registry.add_clipboard(std::make_unique<MacClipboard>());
#else
    (void)registry;
#endif
}

}  // namespace backends
}  // namespace zui
