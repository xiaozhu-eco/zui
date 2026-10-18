#include "backends/posix_platform_util.h"
#include "ext/slots.h"
#include "plugins/clipboard/bmp.h"

#include <string>
#include <vector>

namespace zui {
namespace backends {
namespace {

using posix_util::run_command_capture;
using posix_util::shell_quote;

#if defined(__linux__) && !defined(__ANDROID__)

bool command_ok(const std::string& cmd) {
    std::string out;
    int code = -1;
    return run_command_capture(cmd, out, code);
}

std::string capture(const std::string& cmd) {
    std::string out;
    int code = -1;
    if (!run_command_capture(cmd, out, code)) {
        return {};
    }
    return out;
}

bool write_bytes(const char* cmd, const std::uint8_t* data, std::size_t bytes) {
    FILE* pipe = ZUI_POPEN(cmd, "w");
    if (pipe == nullptr) {
        return false;
    }
    const std::size_t wrote = std::fwrite(data, 1, bytes, pipe);
    const int code = ZUI_PCLOSE(pipe);
    return wrote == bytes && code == 0;
}

class LinuxClipboard final : public ClipboardService {
public:
    ClipboardText text() override {
        ClipboardText out;
        std::string value = capture("wl-paste --no-newline 2>/dev/null");
        if (value.empty()) {
            value = capture("xclip -selection clipboard -o 2>/dev/null");
        }
        if (!value.empty()) {
            out.status = ClipboardStatus::Ok;
            out.text = std::move(value);
        }
        return out;
    }

    bool set_text(std::string_view value) override {
        const std::string owned(value);
        const std::string quoted = shell_quote(owned);
        if (command_ok("printf %s " + quoted + " | wl-copy 2>/dev/null")) {
            return true;
        }
        return command_ok("printf %s " + quoted + " | xclip -selection clipboard 2>/dev/null");
    }

    ClipboardImage image() override {
        FILE* pipe = ZUI_POPEN("xclip -selection clipboard -t image/bmp -o 2>/dev/null", "r");
        if (pipe == nullptr) {
            return {};
        }
        std::vector<std::uint8_t> bytes;
        unsigned char buffer[4096];
        std::size_t count = 0;
        while ((count = std::fread(buffer, 1, sizeof(buffer), pipe)) > 0) {
            bytes.insert(bytes.end(), buffer, buffer + count);
        }
        const int code = ZUI_PCLOSE(pipe);
        if (code != 0 || bytes.empty()) {
            return {};
        }
        return clipboard_bmp::decode(bytes.data(), bytes.size());
    }

    bool set_image(const ClipboardImage& image) override {
        const std::vector<std::uint8_t> bmp =
            clipboard_bmp::encode(image.width, image.height, image.rgba.data(), image.rgba.size());
        if (bmp.empty()) {
            return false;
        }
        return write_bytes("xclip -selection clipboard -t image/bmp -i 2>/dev/null", bmp.data(), bmp.size());
    }
};

#endif

}  // namespace

void register_linux_clipboard(ExtensionRegistry& registry) {
#if defined(__linux__) && !defined(__ANDROID__)
    registry.add_clipboard(std::make_unique<LinuxClipboard>());
#else
    (void)registry;
#endif
}

}  // namespace backends
}  // namespace zui
