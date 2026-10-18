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

#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE

class IosClipboard final : public ClipboardService {
public:
    ClipboardText text() override {
        ClipboardText out;
        id board = apple_objc::msg<id>(apple_objc::cls("UIPasteboard"), apple_objc::sel("generalPasteboard"));
        id value = apple_objc::msg<id>(board, apple_objc::sel("string"));
        const char* utf8 = value != nullptr ? apple_objc::msg<const char*>(value, apple_objc::sel("UTF8String"))
                                            : nullptr;
        if (utf8 == nullptr) {
            return out;
        }
        out.status = ClipboardStatus::Ok;
        out.text = utf8;
        return out;
    }

    bool set_text(std::string_view value) override {
        id board = apple_objc::msg<id>(apple_objc::cls("UIPasteboard"), apple_objc::sel("generalPasteboard"));
        if (board == nullptr) {
            return false;
        }
        const std::string owned(value);
        apple_objc::msg<void>(board, apple_objc::sel("setString:"), apple_objc::ns_string_kept(owned));
        return true;
    }

    ClipboardImage image() override {
        id board = apple_objc::msg<id>(apple_objc::cls("UIPasteboard"), apple_objc::sel("generalPasteboard"));
        const std::string kind = "com.microsoft.bmp";
        id data = board != nullptr ? apple_objc::msg<id>(board, apple_objc::sel("dataForPasteboardType:"),
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
        id board = apple_objc::msg<id>(apple_objc::cls("UIPasteboard"), apple_objc::sel("generalPasteboard"));
        if (data == nullptr || board == nullptr) {
            return false;
        }
        apple_objc::msg<void>(board, apple_objc::sel("setData:forPasteboardType:"), data,
                              apple_objc::ns_string_kept(std::string("com.microsoft.bmp")));
        return true;
    }
};

#endif

}  // namespace

void register_ios_clipboard(ExtensionRegistry& registry) {
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
    registry.add_clipboard(std::make_unique<IosClipboard>());
#else
    (void)registry;
#endif
}

}  // namespace backends
}  // namespace zui
