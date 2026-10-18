#include "backends/platform.h"
#include "ext/slots.h"
#include "plugins/pick/command.h"
#include <memory>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commdlg.h>
#include <objbase.h>
#include <shellapi.h>
#include <shobjidl.h>
#endif

namespace zui {
namespace backends {
namespace {

namespace fs = std::filesystem;

std::string effective_app_id(std::string_view app_id) {
    return app_id.empty() ? std::string("app") : std::string(app_id);
}

std::string env_or_empty(const char* name) {
    const char* v = std::getenv(name);
    return v != nullptr ? std::string(v) : std::string();
}

std::string ensure_dir(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir.string();
}

bool ascii_ieq_prefix(std::string_view text, std::string_view prefix) {
    if (text.size() < prefix.size()) {
        return false;
    }
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        char a = text[i];
        const char b = prefix[i];
        if (a >= 'A' && a <= 'Z') {
            a = static_cast<char>(a - 'A' + 'a');
        }
        if (a != b) {
            return false;
        }
    }
    return true;
}

/// Whitelist only http(s) / mailto / tel / file; reject javascript: and friends.
bool uri_scheme_allowed(std::string_view uri) {
    return ascii_ieq_prefix(uri, "https:") || ascii_ieq_prefix(uri, "http:") ||
           ascii_ieq_prefix(uri, "mailto:") || ascii_ieq_prefix(uri, "tel:") ||
           ascii_ieq_prefix(uri, "file:");
}

#ifdef _WIN32

std::wstring utf8_to_wide(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                                         nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), size);
    return wide;
}

std::string wide_to_utf8(std::wstring_view wide) {
    if (wide.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string utf8(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), utf8.data(), size,
                        nullptr, nullptr);
    return utf8;
}

/// FileFilter::patterns are semicolon extensions without "*."; Win32 wants "*.md;*.txt".
std::wstring win32_patterns(std::string_view patterns) {
    return utf8_to_wide(pick_detail::DialogCommand::win32_patterns(patterns));
}

std::wstring build_filter_blob(const std::vector<FileFilter>& filters) {
    std::wstring blob;
    for (const FileFilter& filter : filters) {
        const std::wstring label = utf8_to_wide(filter.label.empty() ? filter.patterns : filter.label);
        const std::wstring pat = win32_patterns(filter.patterns.empty() ? "*" : filter.patterns);
        blob.append(label);
        blob.push_back(L'\0');
        blob.append(pat);
        blob.push_back(L'\0');
    }
    if (blob.empty()) {
        blob.append(L"All");
        blob.push_back(L'\0');
        blob.append(L"*.*");
        blob.push_back(L'\0');
    }
    blob.push_back(L'\0');
    return blob;
}

std::wstring first_def_ext(const std::vector<FileFilter>& filters) {
    if (filters.empty() || filters.front().patterns.empty()) {
        return {};
    }
    const std::string& patterns = filters.front().patterns;
    const std::size_t semi = patterns.find(';');
    const std::string_view first =
        patterns.substr(0, semi == std::string::npos ? std::string::npos : semi);
    if (first.empty() || first == "*") {
        return {};
    }
    return utf8_to_wide(first);
}

HWND owner_hwnd(NativeWindowId owner) {
    return static_cast<HWND>(owner.get());
}

std::vector<std::string> parse_open_buffer(const wchar_t* buffer) {
    std::vector<std::string> paths;
    if (buffer == nullptr || buffer[0] == L'\0') {
        return paths;
    }
    const wchar_t* cursor = buffer;
    const std::wstring first = cursor;
    cursor += first.size() + 1;
    if (*cursor == L'\0') {
        paths.push_back(wide_to_utf8(first));
        return paths;
    }
    const fs::path dir(first);
    while (*cursor != L'\0') {
        const std::wstring name = cursor;
        paths.push_back(wide_to_utf8((dir / name).wstring()));
        cursor += name.size() + 1;
    }
    return paths;
}

PickPaths win32_pick_open(NativeWindowId owner, const FileDialogOptions& opt) {
    std::vector<wchar_t> file(opt.multi ? 32768u : 4096u);
    if (!opt.default_name.empty()) {
        const std::wstring suggested = utf8_to_wide(opt.default_name);
        const std::size_t n = (std::min)(suggested.size(), file.size() - 1);
        wcsncpy_s(file.data(), file.size(), suggested.c_str(), n);
    } else {
        file[0] = L'\0';
    }

    const std::wstring filter = build_filter_blob(opt.filters);
    const std::wstring title = utf8_to_wide(opt.title);
    const std::wstring def_ext = first_def_ext(opt.filters);

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner_hwnd(owner);
    ofn.lpstrFile = file.data();
    ofn.nMaxFile = static_cast<DWORD>(file.size());
    ofn.lpstrFilter = filter.c_str();
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = title.empty() ? nullptr : title.c_str();
    ofn.lpstrDefExt = def_ext.empty() ? nullptr : def_ext.c_str();
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY |
                OFN_NOCHANGEDIR;
    if (opt.multi) {
        ofn.Flags |= OFN_ALLOWMULTISELECT;
    }

    if (!GetOpenFileNameW(&ofn)) {
        return {PickStatus::Cancelled, {}};
    }
    std::vector<std::string> paths = parse_open_buffer(file.data());
    if (paths.empty()) {
        return {PickStatus::Cancelled, {}};
    }
    return {PickStatus::Ok, std::move(paths)};
}

PickPath win32_pick_save(NativeWindowId owner, const FileDialogOptions& opt) {
    std::vector<wchar_t> file(4096);
    if (!opt.default_name.empty()) {
        const std::wstring suggested = utf8_to_wide(opt.default_name);
        const std::size_t n = (std::min)(suggested.size(), file.size() - 1);
        wcsncpy_s(file.data(), file.size(), suggested.c_str(), n);
    } else {
        file[0] = L'\0';
    }

    const std::wstring filter = build_filter_blob(opt.filters);
    const std::wstring title = utf8_to_wide(opt.title);
    const std::wstring def_ext = first_def_ext(opt.filters);

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner_hwnd(owner);
    ofn.lpstrFile = file.data();
    ofn.nMaxFile = static_cast<DWORD>(file.size());
    ofn.lpstrFilter = filter.c_str();
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = title.empty() ? nullptr : title.c_str();
    ofn.lpstrDefExt = def_ext.empty() ? nullptr : def_ext.c_str();
    ofn.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_NOCHANGEDIR |
                OFN_PATHMUSTEXIST;

    if (!GetSaveFileNameW(&ofn)) {
        return {PickStatus::Cancelled, {}};
    }
    const std::string path = wide_to_utf8(file.data());
    if (path.empty()) {
        return {PickStatus::Cancelled, {}};
    }
    return {PickStatus::Ok, path};
}

PickPath win32_pick_folder(NativeWindowId owner, const FileDialogOptions& opt) {
    HRESULT hr_init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool com_owned = SUCCEEDED(hr_init);
    if (hr_init == RPC_E_CHANGED_MODE) {
        // Already initialized on another model; continue.
    } else if (FAILED(hr_init) && hr_init != S_FALSE) {
        return {PickStatus::Unsupported, {}};
    }

    PickPath result{PickStatus::Cancelled, {}};
    IFileOpenDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&dialog));
    if (SUCCEEDED(hr) && dialog != nullptr) {
        DWORD options = 0;
        if (SUCCEEDED(dialog->GetOptions(&options))) {
            dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        }
        if (!opt.title.empty()) {
            const std::wstring title = utf8_to_wide(opt.title);
            dialog->SetTitle(title.c_str());
        }
        hr = dialog->Show(owner_hwnd(owner));
        if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
            result = {PickStatus::Cancelled, {}};
        } else if (SUCCEEDED(hr)) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item)) && item != nullptr) {
                PWSTR path_w = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path_w)) &&
                    path_w != nullptr) {
                    result = {PickStatus::Ok, wide_to_utf8(path_w)};
                    CoTaskMemFree(path_w);
                }
                item->Release();
            }
        } else {
            result = {PickStatus::Unsupported, {}};
        }
        dialog->Release();
    } else {
        result = {PickStatus::Unsupported, {}};
    }

    if (com_owned) {
        CoUninitialize();
    }
    return result;
}

#endif  // _WIN32


}  // namespace

class Win32Dialogs final : public FileDialogService {
public:
    PickPaths open(NativeWindowId owner, const FileDialogOptions& opt) override {
#ifdef _WIN32
        return win32_pick_open(owner, opt);
#else
        (void)owner;
        (void)opt;
        return {PickStatus::Unsupported, {}};
#endif
    }
    PickPath save(NativeWindowId owner, const FileDialogOptions& opt) override {
#ifdef _WIN32
        return win32_pick_save(owner, opt);
#else
        (void)owner;
        (void)opt;
        return {PickStatus::Unsupported, {}};
#endif
    }
    PickPath folder(NativeWindowId owner, const FileDialogOptions& opt) override {
#ifdef _WIN32
        return win32_pick_folder(owner, opt);
#else
        (void)owner;
        (void)opt;
        return {PickStatus::Unsupported, {}};
#endif
    }
};

void register_win32_dialogs(ExtensionRegistry& registry) {
#if defined(_WIN32)
    registry.add_dialogs(PlatformKind::Win32, std::make_unique<Win32Dialogs>());
#else
    (void)registry;
#endif
}

}  // namespace backends
}  // namespace zui
