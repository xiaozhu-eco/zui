#include "ext/slots.h"

#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace zui {
namespace backends {

namespace {

#if defined(_WIN32)

std::wstring utf8_to_wide(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                      nullptr, 0);
    if (n <= 0) {
        return {};
    }
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n);
    return out;
}

std::string wide_to_utf8(const wchar_t* text, int chars) {
    if (text == nullptr || chars <= 0) {
        return {};
    }
    const int n = WideCharToMultiByte(CP_UTF8, 0, text, chars, nullptr, 0, nullptr, nullptr);
    if (n <= 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, chars, out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring read_unicode() {
    HANDLE data = GetClipboardData(CF_UNICODETEXT);
    if (data == nullptr) {
        return {};
    }
    const wchar_t* text = static_cast<const wchar_t*>(GlobalLock(data));
    if (text == nullptr) {
        return {};
    }
    std::wstring out(text);
    GlobalUnlock(data);
    return out;
}

bool open_clipboard() {
    // Clipboard history and other apps hold the lock for a few milliseconds.
    for (int attempt = 0; attempt < 30; ++attempt) {
        if (OpenClipboard(nullptr)) {
            return true;
        }
        Sleep(10);
    }
    return false;
}

std::vector<std::uint8_t> copy_dib() {
    std::vector<std::uint8_t> out;
    HANDLE data = GetClipboardData(CF_DIB);
    if (data == nullptr) {
        return out;
    }
    const SIZE_T bytes = GlobalSize(data);
    const void* src = GlobalLock(data);
    if (src == nullptr || bytes == 0) {
        if (src != nullptr) {
            GlobalUnlock(data);
        }
        return out;
    }
    out.resize(static_cast<std::size_t>(bytes));
    std::memcpy(out.data(), src, static_cast<std::size_t>(bytes));
    GlobalUnlock(data);
    return out;
}

bool write_dib_raw(const std::vector<std::uint8_t>& bytes) {
    if (bytes.empty()) {
        return true;
    }
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
    if (mem == nullptr) {
        return false;
    }
    void* locked = GlobalLock(mem);
    if (locked == nullptr) {
        GlobalFree(mem);
        return false;
    }
    std::memcpy(locked, bytes.data(), bytes.size());
    GlobalUnlock(mem);
    if (SetClipboardData(CF_DIB, mem) == nullptr) {
        GlobalFree(mem);
        return false;
    }
    return true;
}

bool write_unicode(const std::wstring& text) {
    const std::size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (mem == nullptr) {
        return false;
    }
    void* locked = GlobalLock(mem);
    if (locked == nullptr) {
        GlobalFree(mem);
        return false;
    }
    std::memcpy(locked, text.c_str(), bytes);
    GlobalUnlock(mem);
    if (SetClipboardData(CF_UNICODETEXT, mem) == nullptr) {
        GlobalFree(mem);
        return false;
    }
    return true;
}

#endif

}  // namespace

ClipboardImage clipboard_image_win32() {
    ClipboardImage out;
#if defined(_WIN32)
    if (!open_clipboard()) {
        return out;
    }
    HANDLE data = GetClipboardData(CF_DIB);
    if (data == nullptr) {
        CloseClipboard();
        out.status = ClipboardStatus::Ok;
        return out;
    }
    const auto* header = static_cast<const BITMAPINFOHEADER*>(GlobalLock(data));
    if (header == nullptr || header->biBitCount != 32) {
        if (header != nullptr) {
            GlobalUnlock(data);
        }
        CloseClipboard();
        return out;
    }
    const int width = header->biWidth;
    int height = header->biHeight;
    const bool bottom_up = height > 0;
    if (height < 0) {
        height = -height;
    }
    if (width <= 0 || height <= 0) {
        GlobalUnlock(data);
        CloseClipboard();
        return out;
    }
    const auto* pixels = reinterpret_cast<const std::uint8_t*>(header) + header->biSize;
    out.rgba.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4, 0);
    for (int y = 0; y < height; ++y) {
        const int src_y = bottom_up ? (height - 1 - y) : y;
        const std::uint8_t* src =
            pixels + static_cast<std::size_t>(src_y) * static_cast<std::size_t>(width) * 4;
        std::uint8_t* dst =
            out.rgba.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4;
        for (int x = 0; x < width; ++x) {
            dst[x * 4 + 0] = src[x * 4 + 2];
            dst[x * 4 + 1] = src[x * 4 + 1];
            dst[x * 4 + 2] = src[x * 4 + 0];
            dst[x * 4 + 3] = src[x * 4 + 3];
        }
    }
    out.width = width;
    out.height = height;
    out.status = ClipboardStatus::Ok;
    GlobalUnlock(data);
    CloseClipboard();
#else
    out.status = ClipboardStatus::Unsupported;
#endif
    return out;
}

bool set_clipboard_image_win32(int width, int height, const std::uint8_t* rgba, std::size_t bytes) {
#if defined(_WIN32)
    if (width <= 0 || height <= 0 || rgba == nullptr) {
        return false;
    }
    const std::size_t need = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    if (bytes != need) {
        return false;
    }
    if (!open_clipboard()) {
        return false;
    }
    const std::wstring previous = read_unicode();
    if (!EmptyClipboard()) {
        CloseClipboard();
        return false;
    }
    if (!write_unicode(previous)) {
        CloseClipboard();
        return false;
    }
    const std::size_t dib_bytes = sizeof(BITMAPINFOHEADER) + need;
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, dib_bytes);
    if (mem == nullptr) {
        CloseClipboard();
        return false;
    }
    auto* header = static_cast<BITMAPINFOHEADER*>(GlobalLock(mem));
    if (header == nullptr) {
        GlobalFree(mem);
        CloseClipboard();
        return false;
    }
    std::memset(header, 0, sizeof(*header));
    header->biSize = sizeof(BITMAPINFOHEADER);
    header->biWidth = width;
    header->biHeight = -height;
    header->biPlanes = 1;
    header->biBitCount = 32;
    header->biCompression = BI_RGB;
    auto* dst = reinterpret_cast<std::uint8_t*>(header + 1);
    for (int i = 0; i < width * height; ++i) {
        dst[i * 4 + 0] = rgba[i * 4 + 2];
        dst[i * 4 + 1] = rgba[i * 4 + 1];
        dst[i * 4 + 2] = rgba[i * 4 + 0];
        dst[i * 4 + 3] = rgba[i * 4 + 3];
    }
    GlobalUnlock(mem);
    if (SetClipboardData(CF_DIB, mem) == nullptr) {
        GlobalFree(mem);
        CloseClipboard();
        return false;
    }
    CloseClipboard();
    return true;
#else
    (void)width;
    (void)height;
    (void)rgba;
    (void)bytes;
    return false;
#endif
}

ClipboardText clipboard_text_win32() {
    ClipboardText out;
#if defined(_WIN32)
    if (!open_clipboard()) {
        return out;
    }
    const std::wstring text = read_unicode();
    CloseClipboard();
    out.status = ClipboardStatus::Ok;
    out.text = wide_to_utf8(text.c_str(), static_cast<int>(text.size()));
#else
    out.status = ClipboardStatus::Unsupported;
#endif
    return out;
}

bool set_clipboard_text_win32(std::string_view text) {
#if defined(_WIN32)
    if (!open_clipboard()) {
        return false;
    }
    const std::vector<std::uint8_t> previous_image = copy_dib();
    if (!EmptyClipboard()) {
        CloseClipboard();
        return false;
    }
    if (!write_unicode(utf8_to_wide(text))) {
        CloseClipboard();
        return false;
    }
    if (!write_dib_raw(previous_image)) {
        CloseClipboard();
        return false;
    }
    CloseClipboard();
    return true;
#else
    (void)text;
    return false;
#endif
}

namespace {

#if defined(_WIN32)

class Win32Clipboard final : public ClipboardService {
public:
    ClipboardText text() override { return clipboard_text_win32(); }

    bool set_text(std::string_view value) override { return set_clipboard_text_win32(value); }

    ClipboardImage image() override { return clipboard_image_win32(); }

    bool set_image(const ClipboardImage& image) override {
        return set_clipboard_image_win32(image.width, image.height, image.rgba.data(), image.rgba.size());
    }
};

#endif

}  // namespace

void register_win32_clipboard(ExtensionRegistry& registry) {
#if defined(_WIN32)
    registry.add_clipboard(std::make_unique<Win32Clipboard>());
#else
    (void)registry;
#endif
}

}  // namespace backends
}  // namespace zui
