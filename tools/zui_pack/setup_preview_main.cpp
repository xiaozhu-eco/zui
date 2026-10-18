// Renders the installer screens to PNG files. The live installer paints with
// the same setup_ui code, so these images are the window's client area without
// the title bar. A design aid: it is not installed and no package uses it.
//
//   zui_setup_preview <out-dir> [--icon icon.png]

#include "setup_ui.h"

#include <windows.h>
#include <gdiplus.h>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#pragma comment(lib, "gdiplus.lib")

namespace {

using namespace Gdiplus;

int png_encoder_clsid(CLSID* clsid) {
    UINT count = 0;
    UINT bytes = 0;
    if (GetImageEncodersSize(&count, &bytes) != Ok || bytes == 0) {
        return -1;
    }
    std::vector<BYTE> buffer(bytes);
    ImageCodecInfo* codecs = reinterpret_cast<ImageCodecInfo*>(buffer.data());
    if (GetImageEncoders(count, bytes, codecs) != Ok) {
        return -1;
    }
    for (UINT i = 0; i < count; ++i) {
        if (wcscmp(codecs[i].MimeType, L"image/png") == 0) {
            *clsid = codecs[i].Clsid;
            return static_cast<int>(i);
        }
    }
    return -1;
}

struct Shot {
    const char* name;
    zui_tool::setup_ui::Model model;
};

std::vector<Shot> shots() {
    using zui_tool::setup_ui::Lang;
    using zui_tool::setup_ui::Screen;
    const std::wstring destination = L"C:\\Users\\geocat\\AppData\\Local\\Programs\\ReadFlow";
    std::vector<Shot> all;
    zui_tool::setup_ui::Model install;
    install.screen = Screen::Install;
    install.lang = Lang::Zh;
    install.product = L"ReadFlow";
    install.version = L"\u7248\u672c 1.4.0";
    install.destination = destination;
    all.push_back({"install-zh", install});
    install.lang = Lang::En;
    install.version = L"Version 1.4.0";
    all.push_back({"install-en", install});

    zui_tool::setup_ui::Model progress = install;
    progress.lang = Lang::Zh;
    progress.version = L"\u7248\u672c 1.4.0";
    progress.screen = Screen::Progress;
    progress.step = 5;
    progress.total = 9;
    progress.status = L"\u6587\u4ef6 5 / 9";
    all.push_back({"progress-zh", progress});

    zui_tool::setup_ui::Model done = progress;
    done.screen = Screen::Done;
    done.run_after = true;
    all.push_back({"done-zh", done});

    zui_tool::setup_ui::Model failed = progress;
    failed.screen = Screen::Error;
    failed.message =
        L"\u65e0\u6cd5\u5199\u5165 C:\\Program Files\\ReadFlow\u3002"
        L"\u8bf7\u5173\u95ed\u6b63\u5728\u8fd0\u884c\u7684 ReadFlow\uff0c"
        L"\u6216\u9009\u62e9\u53e6\u4e00\u4e2a\u76ee\u5f55\u3002";
    all.push_back({"error-zh", failed});

    zui_tool::setup_ui::Model uninstall = progress;
    uninstall.screen = Screen::Uninstall;
    uninstall.status =
        L"\u8981\u4ece\u8fd9\u53f0\u7535\u8111\u5378\u8f7d ReadFlow \u5417\uff1f";
    all.push_back({"uninstall-zh", uninstall});
    return all;
}

bool render(const Shot& shot, float scale, const std::filesystem::path& path, Image* icon) {
    const SizeF size = zui_tool::setup_ui::client_size(scale);
    Bitmap bitmap(static_cast<INT>(size.Width), static_cast<INT>(size.Height),
                  PixelFormat32bppARGB);
    Graphics graphics(&bitmap);
    zui_tool::setup_ui::paint(graphics, shot.model, scale, icon);
    graphics.Flush();
    CLSID clsid{};
    if (png_encoder_clsid(&clsid) < 0) {
        return false;
    }
    return bitmap.Save(path.wstring().c_str(), &clsid, nullptr) == Ok;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: zui_setup_preview <out-dir> [--icon icon.png]\n";
        return 1;
    }
    const std::filesystem::path out = argv[1];
    std::filesystem::path icon_path;
    for (int i = 2; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--icon") {
            icon_path = argv[++i];
        }
    }
    GdiplusStartupInput input;
    ULONG_PTR token = 0;
    if (GdiplusStartup(&token, &input, nullptr) != Ok) {
        std::cerr << "zui_setup_preview: cannot start GDI+\n";
        return 1;
    }
    Bitmap* icon = nullptr;
    if (!icon_path.empty() && std::filesystem::is_regular_file(icon_path)) {
        icon = Bitmap::FromFile(icon_path.wstring().c_str());
        if (icon != nullptr && icon->GetLastStatus() != Ok) {
            delete icon;
            icon = nullptr;
        }
    }
    std::error_code ec;
    std::filesystem::create_directories(out, ec);
    int code = 0;
    for (const Shot& shot : shots()) {
        for (const float scale : {1.0f, 1.75f}) {
            const std::string name = std::string(shot.name) + "-" +
                                     std::to_string(static_cast<int>(scale * 100)) + ".png";
            const std::filesystem::path path = out / name;
            if (!render(shot, scale, path, icon)) {
                std::cerr << "zui_setup_preview: cannot write " << path.string() << "\n";
                code = 1;
            } else {
                std::cout << path.string() << std::endl;
            }
        }
    }
    delete icon;
    GdiplusShutdown(token);
    return code;
}
