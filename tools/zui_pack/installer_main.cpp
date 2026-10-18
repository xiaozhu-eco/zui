// Per-user desktop installer. zui-pack appends a store-method zip and a 16-byte
// trailer. The zip holds the app files, product.txt (the project name, then its
// version), and Uninstall.exe (this program without a payload). Double-click
// opens the install window: the app icon, the product and its version, the
// install directory with a Browse button, a progress bar while the files land,
// and a finish screen with a "run the app" switch. It writes a Start Menu
// shortcut and an uninstall entry. /S installs without a window. /D=<dir>
// chooses the directory, /uninstall removes the installation.
//
// setup_ui draws the window; zui_setup_preview renders the same screens to PNG.

#include "project.h"
#include "setup_ui.h"
#include "zip.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <dwmapi.h>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <windowsx.h>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "uuid.lib")
#endif

namespace {

constexpr char kMagic[8] = {'Z', 'U', 'I', 'I', 'N', 'S', 'T', '1'};

std::uint64_t read_le64(const std::uint8_t* p) {
    std::uint64_t value = 0;
    for (int i = 7; i >= 0; --i) {
        value = (value << 8) | p[i];
    }
    return value;
}

std::string trim(std::string text) {
    while (!text.empty() &&
           (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) {
        text.pop_back();
    }
    std::size_t start = 0;
    while (start < text.size() && (text[start] == '\n' || text[start] == ' ')) {
        ++start;
    }
    return text.substr(start);
}

#if defined(_WIN32)

std::wstring widen(const std::string& text) {
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

std::wstring exe_path() {
    wchar_t buf[32768];
    const DWORD n = GetModuleFileNameW(nullptr, buf, 32768);
    if (n == 0 || n >= 32768) {
        return {};
    }
    return std::wstring(buf, n);
}

bool load_self(std::vector<std::uint8_t>& bytes) {
    const std::wstring self = exe_path();
    if (self.empty()) {
        return false;
    }
    std::ifstream in(std::filesystem::path(self), std::ios::binary);
    if (!in) {
        return false;
    }
    in.seekg(0, std::ios::end);
    const auto len = in.tellg();
    if (len < 16) {
        return false;
    }
    in.seekg(0, std::ios::beg);
    bytes.resize(static_cast<std::size_t>(len));
    in.read(reinterpret_cast<char*>(bytes.data()), len);
    return static_cast<bool>(in);
}

std::string nth_line(const std::string& text, int index) {
    std::size_t start = 0;
    for (int i = 0; i < index; ++i) {
        const auto end = text.find('\n', start);
        if (end == std::string::npos) {
            return {};
        }
        start = end + 1;
    }
    const auto end = text.find('\n', start);
    std::string line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    return trim(line);
}

// Where the payload trailer sits, or 0 when this file carries no payload. The
// trailer is found by its magic rather than by the end of the file, because
// signing the installer appends a certificate after it.
std::size_t payload_trailer(const std::vector<std::uint8_t>& self) {
    if (self.size() < 16) {
        return 0;
    }
    for (std::size_t at = self.size() - 8; at >= 8; --at) {
        if (std::memcmp(self.data() + at, kMagic, 8) == 0) {
            // The magic also appears in this program's own code, so a candidate
            // counts only when a zip really starts where its size says.
            const std::size_t start = at - 8;
            const std::uint64_t size = read_le64(self.data() + start);
            if (size > 0 && size <= start) {
                const std::size_t zip = start - static_cast<std::size_t>(size);
                if (zip + 4 <= self.size() && self[zip] == 'P' && self[zip + 1] == 'K' &&
                    self[zip + 2] == 0x03 && self[zip + 3] == 0x04) {
                    return at;
                }
            }
        }
        if (at == 8) {
            break;
        }
    }
    return 0;
}

bool read_payload(std::vector<zui_tool::ZipEntry>& entries, std::string& product, std::string& version) {
    std::vector<std::uint8_t> self;
    if (!load_self(self) || self.size() < 16) {
        return false;
    }
    const std::size_t trailer = payload_trailer(self);
    if (trailer < 8) {
        return false;
    }
    const std::uint64_t zip_size = read_le64(self.data() + trailer - 8);
    if (zip_size == 0 || zip_size > trailer - 8) {
        std::cerr << "zui installer: payload size is wrong\n";
        return false;
    }
    const std::uint8_t* zip = self.data() + trailer - 8 - static_cast<std::size_t>(zip_size);
    std::string error;
    if (!zui_tool::read_zip(zip, static_cast<std::size_t>(zip_size), entries, error)) {
        std::cerr << "zui installer: " << error << "\n";
        return false;
    }
    for (const zui_tool::ZipEntry& entry : entries) {
        if (entry.name == "product.txt") {
            const std::string text(entry.bytes.begin(), entry.bytes.end());
            product = nth_line(text, 0);
            version = nth_line(text, 1);
        }
    }
    return true;
}

std::wstring folder_path(int csidl) {
    wchar_t buf[MAX_PATH];
    if (SHGetFolderPathW(nullptr, csidl, nullptr, SHGFP_TYPE_CURRENT, buf) != S_OK) {
        return {};
    }
    return buf;
}

std::wstring uninstall_key(const std::wstring& product) {
    return L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + product;
}

std::wstring shortcut_path(const std::wstring& product) {
    const std::wstring root = folder_path(CSIDL_APPDATA);
    if (root.empty()) {
        return {};
    }
    return root + L"\\Microsoft\\Windows\\Start Menu\\Programs\\" + product + L".lnk";
}

std::wstring default_dest(const std::wstring& product) {
    const std::wstring root = folder_path(CSIDL_LOCAL_APPDATA);
    if (root.empty()) {
        return {};
    }
    return root + L"\\Programs\\" + product;
}

bool write_shortcut(const std::filesystem::path& dest, const std::wstring& exe_name,
                    const std::wstring& product) {
    const std::wstring lnk = shortcut_path(product);
    if (lnk.empty()) {
        std::cerr << "zui installer: cannot locate the Start Menu\n";
        return false;
    }
    const std::filesystem::path link_file(lnk);
    std::error_code ec;
    std::filesystem::create_directories(link_file.parent_path(), ec);
    if (ec) {
        std::cerr << "zui installer: cannot create the Start Menu directory\n";
        return false;
    }
    IShellLinkW* shell = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                                  reinterpret_cast<void**>(&shell));
    if (FAILED(hr) || shell == nullptr) {
        std::cerr << "zui installer: cannot create a shortcut\n";
        return false;
    }
    const std::wstring target = (dest / exe_name).wstring();
    shell->SetPath(target.c_str());
    shell->SetWorkingDirectory(dest.wstring().c_str());
    shell->SetDescription(product.c_str());
    IPersistFile* file = nullptr;
    hr = shell->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file));
    if (SUCCEEDED(hr) && file != nullptr) {
        hr = file->Save(lnk.c_str(), TRUE);
        file->Release();
    }
    shell->Release();
    if (FAILED(hr)) {
        std::cerr << "zui installer: cannot write the shortcut\n";
        return false;
    }
    return true;
}

bool write_uninstall_key(const std::filesystem::path& dest, const std::wstring& product,
                         const std::wstring& version) {
    HKEY key = nullptr;
    const std::wstring name = uninstall_key(product);
    if (RegCreateKeyExW(HKEY_CURRENT_USER, name.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr,
                        &key, nullptr) != ERROR_SUCCESS) {
        std::cerr << "zui installer: cannot register uninstall\n";
        return false;
    }
    const std::wstring display = product;
    const std::wstring location = dest.wstring();
    const std::wstring publisher = L"zui";
    const std::wstring command = L"\"" + (dest / L"Uninstall.exe").wstring() + L"\"";
    const DWORD one = 1;
    auto set_sz = [&](const wchar_t* value_name, const std::wstring& value) {
        RegSetValueExW(key, value_name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                       static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    };
    set_sz(L"DisplayName", display);
    set_sz(L"InstallLocation", location);
    set_sz(L"DisplayVersion", version);
    set_sz(L"Publisher", publisher);
    set_sz(L"UninstallString", command);
    RegSetValueExW(key, L"NoModify", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&one),
                   sizeof(one));
    RegSetValueExW(key, L"NoRepair", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&one),
                   sizeof(one));
    RegCloseKey(key);
    return true;
}

void remove_registration(const std::wstring& product) {
    const std::wstring lnk = shortcut_path(product);
    if (!lnk.empty()) {
        DeleteFileW(lnk.c_str());
    }
    RegDeleteKeyW(HKEY_CURRENT_USER, uninstall_key(product).c_str());
}

std::string app_exe_name(const std::vector<zui_tool::ZipEntry>& entries) {
    std::string found;
    for (const zui_tool::ZipEntry& entry : entries) {
        if (entry.name.find('/') != std::string::npos) {
            continue;
        }
        if (entry.name == "Uninstall.exe" || entry.name == "product.txt") {
            continue;
        }
        if (entry.name.size() > 4 && entry.name.compare(entry.name.size() - 4, 4, ".exe") == 0) {
            found = entry.name;
        }
    }
    return found;
}

// One file of the payload. The window reports progress per entry, so the unit
// of work is one entry, not the whole payload.
bool extract_entry(const std::filesystem::path& dest, const zui_tool::ZipEntry& entry,
                   std::string& error) {
    const std::filesystem::path relative(entry.name, std::filesystem::path::generic_format);
    const std::filesystem::path output = dest / relative;
    std::error_code ec;
    std::filesystem::create_directories(output.parent_path(), ec);
    if (ec) {
        error = "cannot create " + output.parent_path().string();
        return false;
    }
    std::ofstream out(output, std::ios::binary);
    if (!out) {
        error = "cannot write " + entry.name;
        return false;
    }
    if (!entry.bytes.empty()) {
        out.write(reinterpret_cast<const char*>(entry.bytes.data()),
                  static_cast<std::streamsize>(entry.bytes.size()));
    }
    if (!out) {
        error = "cannot write " + entry.name;
        return false;
    }
    return true;
}

// Steps the installer reports: every payload file, then the Start Menu
// shortcut, then the uninstall entry.
int install_step_count(const std::vector<zui_tool::ZipEntry>& entries) {
    return static_cast<int>(entries.size()) + 2;
}

bool run_install_step(int index, const std::vector<zui_tool::ZipEntry>& entries,
                      const std::filesystem::path& dest, const std::wstring& exe_name,
                      const std::wstring& product, const std::wstring& version, std::string& error) {
    const int files = static_cast<int>(entries.size());
    if (index < files) {
        if (index == 0) {
            std::error_code ec;
            std::filesystem::create_directories(dest, ec);
            if (ec) {
                error = "cannot create " + dest.string();
                return false;
            }
        }
        return extract_entry(dest, entries[static_cast<std::size_t>(index)], error);
    }
    if (index == files) {
        if (!write_shortcut(dest, exe_name, product)) {
            error = "cannot write the Start Menu shortcut";
            return false;
        }
        return true;
    }
    if (!write_uninstall_key(dest, product, version)) {
        error = "cannot register the uninstall entry";
        return false;
    }
    return true;
}

struct SetupArgs {
    bool silent = false;
    bool uninstall = false;
    std::wstring dest;
};

SetupArgs parse_args() {
    SetupArgs args;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) {
        return args;
    }
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"/S" || arg == L"/s" || arg == L"--silent") {
            args.silent = true;
        } else if (arg == L"/uninstall" || arg == L"--uninstall") {
            args.uninstall = true;
        } else if (arg.rfind(L"/D=", 0) == 0) {
            args.dest = arg.substr(3);
        } else if ((arg == L"/D" || arg == L"-D") && i + 1 < argc) {
            args.dest = argv[++i];
        }
    }
    LocalFree(argv);
    return args;
}

// The window closes the process while the rmdir below is still waiting, so the
// delay has to outlast the window.
void schedule_remove_dir(const std::filesystem::path& dest) {
    std::wstring cmd = L"cmd.exe /c ping 127.0.0.1 -n 4 >nul & rmdir /s /q \"" + dest.wstring() +
                       L"\"";
    std::vector<wchar_t> mutable_cmd(cmd.begin(), cmd.end());
    mutable_cmd.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, mutable_cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                       nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

// Every file below the install directory except the running program, which the
// scheduled rmdir removes once this process is gone.
void remove_installed_files(const std::filesystem::path& dest,
                            const std::filesystem::path& running) {
    std::error_code ec;
    std::vector<std::filesystem::path> files;
    if (std::filesystem::is_directory(dest, ec)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dest, ec)) {
            if (!ec && entry.is_regular_file()) {
                files.push_back(entry.path());
            }
        }
    }
    for (const std::filesystem::path& file : files) {
        if (file.lexically_normal() == running.lexically_normal()) {
            continue;
        }
        std::filesystem::remove(file, ec);
    }
}

float dpi_scale() {
    HDC dc = GetDC(nullptr);
    const int dpi = dc != nullptr ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc != nullptr) {
        ReleaseDC(nullptr, dc);
    }
    return std::max(1.0f, static_cast<float>(dpi) / 96.0f);
}

// "n / m" in the language of the window.
std::wstring step_text(zui_tool::setup_ui::Lang lang, int step, int total) {
    wchar_t buffer[64];
    if (lang == zui_tool::setup_ui::Lang::Zh) {
        swprintf(buffer, 64, L"文件 %d / %d", step, total);
    } else {
        swprintf(buffer, 64, L"File %d / %d", step, total);
    }
    return buffer;
}

std::wstring fill_product(const wchar_t* pattern, const std::wstring& product) {
    std::wstring text(pattern != nullptr ? pattern : L"");
    const std::size_t at = text.find(L"%s");
    if (at == std::wstring::npos) {
        return text;
    }
    return text.substr(0, at) + product + text.substr(at + 2);
}

int item_at(const zui_tool::setup_ui::Model& model, float scale, POINT point) {
    for (zui_tool::setup_ui::Item item : zui_tool::setup_ui::screen_items(model.screen)) {
        const Gdiplus::RectF rect = zui_tool::setup_ui::item_rect(item, scale);
        const float x = static_cast<float>(point.x);
        const float y = static_cast<float>(point.y);
        if (x >= rect.X && x < rect.X + rect.Width && y >= rect.Y && y < rect.Y + rect.Height) {
            return static_cast<int>(item);
        }
    }
    return -1;
}

// The keyboard stops the field and the buttons take, in reading order.
std::vector<zui_tool::setup_ui::Item> focus_order(zui_tool::setup_ui::Screen screen) {
    std::vector<zui_tool::setup_ui::Item> order;
    for (zui_tool::setup_ui::Item item : zui_tool::setup_ui::screen_items(screen)) {
        if (item != zui_tool::setup_ui::Item::Path) {
            order.push_back(item);
        }
    }
    return order;
}

// The install window. One window covers every screen: choosing the directory,
// watching the files land, running the app, or failing with a reason.
class SetupWindow {
public:
    SetupWindow(HINSTANCE instance, std::vector<zui_tool::ZipEntry> entries,
                const std::string& product_utf8, const std::string& version_utf8, bool uninstall,
                const SetupArgs& args)
        : instance_(instance), entries_(std::move(entries)), product_utf8_(product_utf8),
          product_(widen(product_utf8)), version_(widen(version_utf8)), uninstall_(uninstall),
          args_(args) {
        model_.lang = zui_tool::setup_ui::system_lang();
        model_.product = product_;
        model_.version = model_.lang == zui_tool::setup_ui::Lang::Zh
                             ? L"版本 " + version_
                             : L"Version " + version_;
        exe_name_ = widen(app_exe_name(entries_));
    }

    ~SetupWindow() {
        if (font_ != nullptr) {
            DeleteObject(font_);
        }
        if (white_ != nullptr) {
            DeleteObject(white_);
        }
    }

    int run();

private:
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT dispatch(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    void create_children(HWND hwnd);
    void load_icon();
    void paint(HDC dc);
    void refresh() {
        if (hwnd_ != nullptr) {
            InvalidateRect(hwnd_, nullptr, FALSE);
            UpdateWindow(hwnd_);
        }
    }
    void set_focus(int item);
    bool on_key(WPARAM key);
    void activate(int item);
    void browse();
    void install();
    void uninstall();
    void finish();

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND edit_ = nullptr;
    std::vector<zui_tool::ZipEntry> entries_;
    std::string product_utf8_;
    std::wstring product_;
    std::wstring version_;
    std::wstring exe_name_;
    std::wstring destination_;
    bool uninstall_ = false;
    SetupArgs args_;
    zui_tool::setup_ui::Model model_;
    float scale_ = 1.0f;
    HFONT font_ = nullptr;
    HBRUSH white_ = nullptr;
    HCURSOR hand_ = nullptr;
    std::unique_ptr<Gdiplus::Bitmap> icon_;
    int code_ = 0;
    int pressed_ = -1;
    bool running_ = false;
};

int SetupWindow::run() {
    Gdiplus::GdiplusStartupInput input;
    ULONG_PTR token = 0;
    if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) {
        std::cerr << "zui installer: cannot start GDI+\n";
        return 1;
    }
    const zui_tool::setup_ui::Labels& text = zui_tool::setup_ui::labels(model_.lang);
    scale_ = dpi_scale();
    white_ = CreateSolidBrush(RGB(255, 255, 255));
    hand_ = LoadCursorW(nullptr, IDC_HAND);
    font_ = CreateFontW(-static_cast<int>(14.0f * scale_), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, text.font_family);
    load_icon();
    model_.screen =
        uninstall_ ? zui_tool::setup_ui::Screen::Uninstall : zui_tool::setup_ui::Screen::Install;
    if (uninstall_) {
        // The uninstaller sits in the directory it removes.
        destination_ = std::filesystem::path(exe_path()).parent_path().wstring();
    } else {
        destination_ = args_.dest.empty() ? default_dest(product_) : args_.dest;
    }
    model_.destination = destination_;
    if (uninstall_) {
        model_.status = fill_product(text.uninstall_question, product_);
    } else if (destination_.empty()) {
        // %LOCALAPPDATA% and no /D=<dir>: there is nowhere to install, so the
        // window opens on the reason instead of an empty field.
        model_.screen = zui_tool::setup_ui::Screen::Error;
        model_.message = model_.lang == zui_tool::setup_ui::Lang::Zh
                             ? L"无法确定安装目录。请用 /D=<目录> 指定一个。"
                             : L"There is no install directory. Pass /D=<dir>.";
    }

    WNDCLASSW window_class{};
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance_;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.lpszClassName = L"ZuiInstallWindow";
    window_class.hbrBackground = white_;
    RegisterClassW(&window_class);

    const Gdiplus::SizeF size = zui_tool::setup_ui::client_size(scale_);
    RECT rect{0, 0, static_cast<LONG>(size.Width), static_cast<LONG>(size.Height)};
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    AdjustWindowRectEx(&rect, style, FALSE, 0);
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    const std::wstring title =
        std::wstring(uninstall_ ? text.uninstall_title : text.install) + L" " + product_;
    hwnd_ = CreateWindowExW(0, L"ZuiInstallWindow", title.c_str(), style | WS_CLIPCHILDREN,
                            (GetSystemMetrics(SM_CXSCREEN) - width) / 2,
                            (GetSystemMetrics(SM_CYSCREEN) - height) / 2, width, height, nullptr,
                            nullptr, instance_, this);
    if (hwnd_ == nullptr) {
        std::cerr << "zui installer: cannot open the setup window\n";
        Gdiplus::GdiplusShutdown(token);
        return 1;
    }
    // The client area is light, so the title bar is light too, and Windows 11
    // rounds the corners. Both calls are best effort: older builds reject them.
    const BOOL light_title = FALSE;
    if (FAILED(DwmSetWindowAttribute(hwnd_, 20, &light_title, sizeof(light_title)))) {
        DwmSetWindowAttribute(hwnd_, 19, &light_title, sizeof(light_title));
    }
    const DWORD rounded = 2;
    DwmSetWindowAttribute(hwnd_, 33, &rounded, sizeof(rounded));
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    UpdateWindow(hwnd_);
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        // The path field would keep Tab, Enter, and Escape for itself; the
        // window procedure handles them for every other target.
        if (message.message == WM_KEYDOWN && message.hwnd == edit_ && on_key(message.wParam)) {
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    icon_.reset();  // GDI+ objects cannot outlive GdiplusShutdown
    Gdiplus::GdiplusShutdown(token);
    return code_;
}

void SetupWindow::load_icon() {
    const HICON handle = static_cast<HICON>(
        LoadImageW(instance_, MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
    if (handle == nullptr) {
        return;
    }
    Gdiplus::Bitmap* bitmap = Gdiplus::Bitmap::FromHICON(handle);
    DestroyIcon(handle);
    if (bitmap != nullptr && bitmap->GetLastStatus() == Gdiplus::Ok) {
        icon_.reset(bitmap);
    } else {
        delete bitmap;
    }
}

void SetupWindow::create_children(HWND hwnd) {
    if (uninstall_ || model_.screen == zui_tool::setup_ui::Screen::Error) {
        set_focus(static_cast<int>(zui_tool::setup_ui::Item::Primary));
        return;
    }
    const Gdiplus::RectF rect = zui_tool::setup_ui::item_rect(zui_tool::setup_ui::Item::Edit, scale_);
    edit_ = CreateWindowExW(0, L"EDIT", destination_.c_str(),
                            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, static_cast<int>(rect.X),
                            static_cast<int>(rect.Y), static_cast<int>(rect.Width),
                            static_cast<int>(rect.Height), hwnd,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(1)), instance_, nullptr);
    if (edit_ != nullptr) {
        SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        SendMessageW(edit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(0, 0));
    }
    set_focus(static_cast<int>(zui_tool::setup_ui::Item::Edit));
}

void SetupWindow::set_focus(int item) {
    model_.focus = item;
    if (item == static_cast<int>(zui_tool::setup_ui::Item::Edit) && edit_ != nullptr) {
        SetFocus(edit_);
        SendMessageW(edit_, EM_SETSEL, 0, -1);
    } else if (hwnd_ != nullptr) {
        SetFocus(hwnd_);
    }
    refresh();
}

LRESULT CALLBACK SetupWindow::window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    SetupWindow* window =
        reinterpret_cast<SetupWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* created = reinterpret_cast<CREATESTRUCTW*>(lparam);
        window = static_cast<SetupWindow*>(created->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
        window->hwnd_ = hwnd;
    }
    if (window == nullptr) {
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
    return window->dispatch(hwnd, message, wparam, lparam);
}

LRESULT SetupWindow::dispatch(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CREATE:
        create_children(hwnd);
        return 0;
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wparam);
        SetTextColor(dc, RGB(31, 42, 36));
        SetBkColor(dc, RGB(255, 255, 255));
        return reinterpret_cast<LRESULT>(white_);
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_KEYDOWN:
        if (on_key(wparam)) {
            return 0;
        }
        break;
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(hwnd, &paint);
        SetupWindow::paint(dc);
        EndPaint(hwnd, &paint);
        return 0;
    }
    case WM_MOUSEMOVE: {
        const int item = item_at(model_, scale_, {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
        if (item != model_.hot) {
            model_.hot = item;
            refresh();
        }
        return 0;
    }
    case WM_SETCURSOR: {
        if (reinterpret_cast<HWND>(wparam) == hwnd_ && model_.hot >= 0 &&
            model_.hot != static_cast<int>(zui_tool::setup_ui::Item::Path) && hand_ != nullptr) {
            SetCursor(hand_);
            return TRUE;
        }
        break;
    }
    case WM_LBUTTONDOWN: {
        const int item = item_at(model_, scale_, {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
        if (item >= 0) {
            pressed_ = item;
            model_.pressed = true;
            SetCapture(hwnd);
            if (item == static_cast<int>(zui_tool::setup_ui::Item::Path) ||
                item == static_cast<int>(zui_tool::setup_ui::Item::Edit)) {
                set_focus(static_cast<int>(zui_tool::setup_ui::Item::Edit));
            }
            refresh();
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        if (pressed_ >= 0) {
            ReleaseCapture();
            const int item = item_at(model_, scale_, {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
            const int pressed = pressed_;
            pressed_ = -1;
            model_.pressed = false;
            if (item == pressed) {
                activate(item);
            }
            refresh();
        }
        return 0;
    }
    case WM_CLOSE:
        if (!running_) {
            // Closing the finished window keeps the result of the install.
            code_ = model_.screen == zui_tool::setup_ui::Screen::Done ? 0 : 1;
            DestroyWindow(hwnd);
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

void SetupWindow::paint(HDC dc) {
    Gdiplus::Graphics graphics(dc);
    zui_tool::setup_ui::paint(graphics, model_, scale_, icon_.get());
}

bool SetupWindow::on_key(WPARAM key) {
    using zui_tool::setup_ui::Item;
    using zui_tool::setup_ui::Screen;
    if (key == VK_ESCAPE) {
        if (!running_) {
            // An install that already finished keeps its result.
            code_ = model_.screen == Screen::Done ? 0 : 1;
            DestroyWindow(hwnd_);
        }
        return true;
    }
    const std::vector<zui_tool::setup_ui::Item> order = focus_order(model_.screen);
    if (key == VK_TAB) {
        if (order.empty()) {
            return true;
        }
        const int count = static_cast<int>(order.size());
        int current = -1;
        for (int i = 0; i < count; ++i) {
            if (static_cast<int>(order[static_cast<std::size_t>(i)]) == model_.focus) {
                current = i;
                break;
            }
        }
        const bool back = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        const int next = back ? (current <= 0 ? count - 1 : current - 1)
                              : (current + 1 >= count ? 0 : current + 1);
        set_focus(static_cast<int>(order[static_cast<std::size_t>(next)]));
        return true;
    }
    if (key == VK_SPACE) {
        // The path field takes a space as text.
        if (model_.focus >= 0 && model_.focus != static_cast<int>(Item::Edit) &&
            model_.focus != static_cast<int>(Item::Path)) {
            activate(model_.focus);
        }
        return true;
    }
    if (key == VK_RETURN) {
        // Enter is the default button, whatever holds the focus: from the path
        // field it starts the install.
        activate(static_cast<int>(Item::Primary));
        return true;
    }
    return false;
}

void SetupWindow::activate(int item) {
    using zui_tool::setup_ui::Item;
    using zui_tool::setup_ui::Screen;
    switch (static_cast<Item>(item)) {
    case Item::Path:
    case Item::Edit:
        set_focus(static_cast<int>(Item::Edit));
        break;
    case Item::Browse:
        browse();
        break;
    case Item::RunAfter:
        model_.run_after = !model_.run_after;
        refresh();
        break;
    case Item::Primary:
        if (model_.screen == Screen::Install) {
            install();
        } else if (model_.screen == Screen::Done) {
            finish();
        } else if (model_.screen == Screen::Uninstall) {
            uninstall();
        } else if (model_.screen == Screen::Error) {
            code_ = 1;
            DestroyWindow(hwnd_);
        }
        break;
    case Item::Secondary:
        code_ = 1;
        DestroyWindow(hwnd_);
        break;
    }
}

void SetupWindow::browse() {
    IFileDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog)))) {
        return;
    }
    DWORD options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options))) {
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    }
    dialog->SetTitle(zui_tool::setup_ui::labels(model_.lang).install_to);
    if (dialog->Show(hwnd_) == S_OK) {
        IShellItem* result = nullptr;
        if (SUCCEEDED(dialog->GetResult(&result)) && result != nullptr) {
            PWSTR path = nullptr;
            if (SUCCEEDED(result->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path != nullptr) {
                destination_ = path;
                model_.destination = destination_;
                if (edit_ != nullptr) {
                    SetWindowTextW(edit_, destination_.c_str());
                }
                CoTaskMemFree(path);
            }
            result->Release();
        }
    }
    dialog->Release();
    refresh();
}

void SetupWindow::install() {
    if (edit_ != nullptr) {
        wchar_t buffer[32768];
        GetWindowTextW(edit_, buffer, 32768);
        destination_ = buffer;
        model_.destination = destination_;
    }
    if (destination_.empty()) {
        model_.screen = zui_tool::setup_ui::Screen::Error;
        model_.message = model_.lang == zui_tool::setup_ui::Lang::Zh
                             ? L"安装目录是空的。填一个目录，或按「浏览…」选一个。"
                             : L"The install directory is empty. Type one, or press Browse.";
        set_focus(static_cast<int>(zui_tool::setup_ui::Item::Primary));
        return;
    }
    const std::filesystem::path dest(destination_);
    std::string error;
    running_ = true;
    model_.screen = zui_tool::setup_ui::Screen::Progress;
    model_.total = install_step_count(entries_);
    model_.step = 0;
    model_.status = step_text(model_.lang, 0, model_.total);
    model_.hot = -1;
    model_.focus = -1;
    refresh();
    for (int index = 0; index < model_.total; ++index) {
        if (!run_install_step(index, entries_, dest, exe_name_, product_, version_, error)) {
            break;
        }
        model_.step = index + 1;
        model_.status = step_text(model_.lang, model_.step, model_.total);
        refresh();
    }
    running_ = false;
    if (!error.empty()) {
        std::cerr << "zui installer: " << error << "\n";
        model_.screen = zui_tool::setup_ui::Screen::Error;
        model_.message = widen(error);
        set_focus(static_cast<int>(zui_tool::setup_ui::Item::Primary));
        return;
    }
    std::cout << "zui installer: " << product_utf8_ << "\n";
    std::cout << "installed: " << dest.string() << "\n";
    model_.screen = zui_tool::setup_ui::Screen::Done;
    set_focus(static_cast<int>(zui_tool::setup_ui::Item::Primary));
}

void SetupWindow::uninstall() {
    running_ = true;
    const std::wstring self = exe_path();
    const std::filesystem::path dest = std::filesystem::path(self).parent_path();
    remove_registration(product_);
    remove_installed_files(dest, std::filesystem::path(self));
    schedule_remove_dir(dest);
    std::cout << "zui installer: " << product_utf8_ << "\n";
    std::cout << "uninstalled: " << dest.string() << "\n";
    running_ = false;
    code_ = 0;
    DestroyWindow(hwnd_);
}

void SetupWindow::finish() {
    if (model_.run_after) {
        const std::filesystem::path dest(destination_);
        const std::wstring target = (dest / exe_name_).wstring();
        const HINSTANCE result =
            ShellExecuteW(nullptr, L"open", target.c_str(), nullptr, dest.wstring().c_str(),
                          SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(result) <= 32) {
            std::cerr << "zui installer: cannot start the application\n";
        }
    }
    code_ = 0;
    DestroyWindow(hwnd_);
}
std::string read_product_file(const std::filesystem::path& dest) {
    std::ifstream in(dest / "product.txt", std::ios::binary);
    if (!in) {
        return {};
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return nth_line(text, 0);
}

int uninstall_product(bool silent) {
    const std::wstring self = exe_path();
    if (self.empty()) {
        std::cerr << "zui installer: cannot locate the setup program\n";
        return 1;
    }
    const std::filesystem::path dest = std::filesystem::path(self).parent_path();
    const std::string product_utf8 = read_product_file(dest);
    if (!zui_tool::valid_name(product_utf8)) {
        std::cerr << "zui installer: product name is missing\n";
        return 1;
    }
    const std::wstring product = widen(product_utf8);
    if (!silent) {
        // The window asks, removes, and closes; its return code is the result.
        SetupWindow window(GetModuleHandleW(nullptr), {}, product_utf8, std::string(), true, {});
        return window.run();
    }
    remove_registration(product);
    remove_installed_files(dest, std::filesystem::path(self));
    schedule_remove_dir(dest);
    std::cout << "zui installer: " << product_utf8 << "\n";
    std::cout << "uninstalled: " << dest.string() << "\n";
    return 0;
}

int install_product(const SetupArgs& args) {
    std::vector<zui_tool::ZipEntry> entries;
    std::string product_utf8;
    std::string version_utf8;
    if (!read_payload(entries, product_utf8, version_utf8) || !zui_tool::valid_name(product_utf8)) {
        std::cerr << "zui installer: product name is missing\n";
        return 1;
    }
    const std::string exe_utf8 = app_exe_name(entries);
    if (exe_utf8.empty()) {
        std::cerr << "zui installer: application executable is missing\n";
        return 1;
    }
    const std::wstring product = widen(product_utf8);
    const std::wstring version = version_utf8.empty() ? L"1.0" : widen(version_utf8);
    if (!args.silent) {
        SetupWindow window(GetModuleHandleW(nullptr), entries, product_utf8, version_utf8, false,
                           args);
        return window.run();
    }
    std::wstring dest_text = args.dest.empty() ? default_dest(product) : args.dest;
    if (dest_text.empty()) {
        std::cerr << "zui installer: cannot locate the install directory\n";
        return 1;
    }
    const std::filesystem::path dest(dest_text);
    std::string error;
    const int total = install_step_count(entries);
    for (int index = 0; index < total; ++index) {
        if (!run_install_step(index, entries, dest, widen(exe_utf8), product, version, error)) {
            std::cerr << "zui installer: " << error << "\n";
            return 1;
        }
    }
    std::cout << "zui installer: " << product_utf8 << "\n";
    std::cout << "installed: " << dest.string() << "\n";
    return 0;
}

int run_installer() {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const SetupArgs args = parse_args();
    std::vector<std::uint8_t> self;
    const bool payload = load_self(self) && payload_trailer(self) >= 8;
    int code = 1;
    if (!payload || args.uninstall) {
        code = uninstall_product(args.silent);
    } else {
        code = install_product(args);
    }
    if (SUCCEEDED(com)) {
        CoUninitialize();
    }
    return code;
}

}  // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    SetProcessDPIAware();
    return run_installer();
}

#else

}  // namespace

int main() {
    std::cerr << "zui installer: the desktop installer runs on Windows\n";
    return 1;
}

#endif
