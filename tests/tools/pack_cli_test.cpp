// Drives the built zui_create and zui-pack executables. It does not write a
// project itself and it does not reimplement the packer.

#include "pack_cli_paths.h"
#include "apple_bundle.h"
#include "archive.h"
#include "md5.h"
#include "package_text.h"
#include "pe.h"
#include "project.h"
#include "project_files.h"

#include <zui/host_types.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#endif

namespace {

namespace fs = std::filesystem;

template <typename... Ts>
std::vector<std::string> cmd(Ts&&... parts) {
    std::vector<std::string> out;
    out.reserve(sizeof...(parts));
    (out.emplace_back(std::forward<Ts>(parts)), ...);
    return out;
}

std::string quote_arg(const std::string& text) {
    std::string out = "\"";
    for (char c : text) {
        if (c == '"') {
            out += "\\\"";
        } else {
            out.push_back(c);
        }
    }
    out.push_back('"');
    return out;
}

struct Captured {
    int code = 1;
    std::string text;
};

Captured run_captured(const std::vector<std::string>& args, const fs::path& log) {
    std::string cmd;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i != 0) {
            cmd.push_back(' ');
        }
        cmd += quote_arg(args[i]);
    }
    // Extra quotes: cmd.exe /c strips one leading and one trailing quote.
    // NUL is not a terminal, so flag-driven runs take defaults instead of
    // starting the wizard on the test process's console.
#if defined(_WIN32)
    cmd = "\"" + cmd + " < NUL > " + quote_arg(log.string()) + " 2>&1\"";
#else
    cmd = "\"" + cmd + " < /dev/null > " + quote_arg(log.string()) + " 2>&1\"";
#endif
    Captured captured;
    captured.code = std::system(cmd.c_str());
    std::ifstream in(log, std::ios::binary);
    captured.text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return captured;
}

Captured run_with_input(const std::vector<std::string>& args, const fs::path& log, const fs::path& input) {
    std::string cmd;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i != 0) {
            cmd.push_back(' ');
        }
        cmd += quote_arg(args[i]);
    }
    cmd = "\"" + cmd + " < " + quote_arg(input.string()) + " > " + quote_arg(log.string()) + " 2>&1\"";
    Captured captured;
    captured.code = std::system(cmd.c_str());
    std::ifstream in(log, std::ios::binary);
    captured.text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return captured;
}

std::string read_text(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool exe_has_icon(const fs::path& exe) {
#if defined(_WIN32)
    const HMODULE module =
        LoadLibraryExW(exe.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (module == nullptr) {
        return false;
    }
    // RT_GROUP_ICON is 14. The A macro is char* when UNICODE is off.
    const HRSRC group = FindResourceW(module, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(14));
    FreeLibrary(module);
    return group != nullptr;
#else
    (void)exe;
    return false;
#endif
}

#if defined(_WIN32)

// One field of the executable's version resource, as Explorer would show it.
std::string version_field(const fs::path& exe, const std::wstring& name) {
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(exe.wstring().c_str(), &handle);
    if (size == 0) {
        return {};
    }
    std::vector<BYTE> data(size);
    if (GetFileVersionInfoW(exe.wstring().c_str(), 0, size, data.data()) == FALSE) {
        return {};
    }
    const std::wstring query = L"\\StringFileInfo\\040904B0\\" + name;
    LPWSTR value = nullptr;
    UINT length = 0;
    if (VerQueryValueW(data.data(), query.c_str(), reinterpret_cast<LPVOID*>(&value), &length) ==
            FALSE ||
        value == nullptr) {
        return {};
    }
    const std::wstring text(value, length > 0 ? length - 1 : 0);
    return std::string(text.begin(), text.end());
}

#endif

bool bytes_contain_launcher_icon(const std::string& bytes) {
    if (bytes.find("ic_launcher") != std::string::npos) {
        return true;
    }
    const char utf16[] = {'i', 0, 'c', 0, '_', 0, 'l', 0, 'a', 0, 'u', 0,
                          'n', 0, 'c', 0, 'h', 0, 'e', 0, 'r', 0};
    return bytes.find(std::string(utf16, sizeof(utf16))) != std::string::npos;
}

std::set<std::string> relative_files(const fs::path& root) {
    std::set<std::string> names;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        names.insert(fs::relative(entry.path(), root).generic_string());
    }
    return names;
}

fs::path fresh_dir(const char* name) {
    const fs::path dir = fs::temp_directory_path() / "zui-pack-cli" / name;
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

std::string lower_copy(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return text;
}

// The executable zui-pack builds for a project, whichever layout the
// generator uses.
fs::path built_exe(const fs::path& project, const std::string& name) {
    const fs::path build = project / ".zui" / "build";
    const char* configs[] = {"", "Debug", "Release", "RelWithDebInfo", "MinSizeRel"};
    for (const char* config : configs) {
        const fs::path candidate =
            config[0] == '\0' ? build / (name + ".exe") : build / config / (name + ".exe");
        std::error_code ec;
        if (fs::is_regular_file(candidate, ec)) {
            return candidate;
        }
    }
    return {};
}

std::vector<std::string> lower_names(const std::vector<std::filesystem::path>& paths) {
    std::vector<std::string> names;
    for (const fs::path& path : paths) {
        names.push_back(lower_copy(path.filename().string()));
    }
    return names;
}

bool contains(const std::vector<std::string>& names, const std::string& wanted) {
    const std::string lowered = lower_copy(wanted);
    return std::find(names.begin(), names.end(), lowered) != names.end();
}

void expect_app_surface(const fs::path& project) {
    const std::string entry = read_text(project / "src" / "main.cpp");
    EXPECT_NE(entry.find("#include <zui/app.h>"), std::string::npos) << entry;
    EXPECT_EQ(entry.find("#include <zui/document.h>"), std::string::npos);
    EXPECT_EQ(entry.find("#include <zui/host.h>"), std::string::npos);
    EXPECT_EQ(entry.find("#include <zui/window.h>"), std::string::npos);
    EXPECT_EQ(entry.find("zui_sign"), std::string::npos);
    EXPECT_EQ(entry.find("CPack"), std::string::npos);
    const std::string build = read_text(project / "CMakeLists.txt");
    EXPECT_EQ(build.find("CPack"), std::string::npos) << build;
    EXPECT_EQ(build.find("zui_sign"), std::string::npos);
    EXPECT_EQ(build.find("install("), std::string::npos);
    const std::string page = read_text(project / "ui" / "index.html");
    EXPECT_NE(page.find("Hello from zui"), std::string::npos);
    EXPECT_NE(page.find("id=\"greeting\""), std::string::npos);
    EXPECT_FALSE(page.empty());
}

int permission_count(const std::string& manifest) {
    int count = 0;
    for (std::size_t at = 0; (at = manifest.find("<uses-permission ", at)) != std::string::npos; ++at) {
        ++count;
    }
    return count;
}

// aapt2 writes a binary manifest. The permission name is not a plain string
// in the APK bytes, so the packed check dumps the XML tree.
fs::path find_aapt2() {
    std::vector<fs::path> sdks;
    if (const char* home = std::getenv("ANDROID_HOME")) {
        sdks.emplace_back(home);
    }
    if (const char* root = std::getenv("ANDROID_SDK_ROOT")) {
        sdks.emplace_back(root);
    }
    if (const char* local = std::getenv("LOCALAPPDATA")) {
        sdks.emplace_back(fs::path(local) / "Android" / "Sdk");
    }
    sdks.emplace_back("C:/Android/Sdk");
    fs::path best;
    std::string best_name;
    for (const fs::path& sdk : sdks) {
        const fs::path tools = sdk / "build-tools";
        std::error_code ec;
        if (!fs::is_directory(tools, ec)) {
            continue;
        }
        for (const auto& entry : fs::directory_iterator(tools, ec)) {
            if (ec || !entry.is_directory()) {
                continue;
            }
#if defined(_WIN32)
            const fs::path exe = entry.path() / "aapt2.exe";
#else
            const fs::path exe = entry.path() / "aapt2";
#endif
            if (!fs::is_regular_file(exe)) {
                continue;
            }
            const std::string name = entry.path().filename().string();
            if (best_name.empty() || name > best_name) {
                best_name = name;
                best = exe;
            }
        }
    }
    return best;
}

int xml_element_count(const std::string& dump, const std::string& element) {
    int count = 0;
    const std::string needle = "E: " + element;
    for (std::size_t at = 0; (at = dump.find(needle, at)) != std::string::npos;) {
        const std::size_t end = at + needle.size();
        const bool boundary = end == dump.size() || dump[end] == ' ' || dump[end] == '\n' ||
                              dump[end] == '\r' || dump[end] == '(';
        if (boundary) {
            ++count;
        }
        at = end;
    }
    return count;
}

// The import closure of zui.dll in this build: the core library and the plugin
// DLLs source/ext/builtin.cpp registers. A package of a plain app ships these.
const char* kRuntimeDlls[] = {
    "zui.dll",
    "zui_plugin_image.dll",
    "zui_plugin_clipboard.dll",
    "zui_plugin_pick.dll",
    "zui_plugin_shell.dll",
    "zui_plugin_gamepad.dll",
    "zui_plugin_tasks.dll",
};

// Starts the packed executable. A missing plugin DLL makes the loader exit
// immediately with STATUS_DLL_NOT_FOUND (0xC0000135). A successful start calls
// app.run() and keeps the process alive, so this waits only long enough to
// tell those apart, then terminates a process that is still running.
int start_status(const fs::path& exe, const fs::path& cwd = {}) {
#if !defined(_WIN32)
    (void)exe;
    (void)cwd;
    return 0;
#else
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring command = L"\"" + exe.wstring() + L"\"";
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');
    const std::wstring work = (cwd.empty() ? exe.parent_path() : cwd).wstring();
    if (!CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        work.c_str(), &si, &pi)) {
        return -1;
    }
    const DWORD waited = WaitForSingleObject(pi.hProcess, 1200);
    DWORD code = 0;
    if (waited == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 0);
        WaitForSingleObject(pi.hProcess, 2000);
        code = 259;
    } else {
        GetExitCodeProcess(pi.hProcess, &code);
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return static_cast<int>(code);
#endif
}

fs::path local_programs() {
#if !defined(_WIN32)
    return {};
#else
    wchar_t buf[MAX_PATH];
    if (SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, buf) != S_OK) {
        return {};
    }
    return fs::path(buf) / "Programs";
#endif
}

fs::path start_menu_shortcut(const std::wstring& product) {
#if !defined(_WIN32)
    (void)product;
    return {};
#else
    wchar_t buf[MAX_PATH];
    if (SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, buf) != S_OK) {
        return {};
    }
    return fs::path(buf) / "Microsoft" / "Windows" / "Start Menu" / "Programs" / (product + L".lnk");
#endif
}

bool uninstall_location(const std::wstring& product, std::wstring& location) {
#if !defined(_WIN32)
    (void)product;
    (void)location;
    return false;
#else
    const std::wstring sub =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + product;
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, sub.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return false;
    }
    wchar_t buf[32768];
    DWORD size = sizeof(buf);
    DWORD type = 0;
    const LONG rc = RegQueryValueExW(key, L"InstallLocation", nullptr, &type,
                                     reinterpret_cast<BYTE*>(buf), &size);
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS || type != REG_SZ) {
        return false;
    }
    location.assign(buf);
    return true;
#endif
}

bool wait_until_gone(const fs::path& dir) {
    for (int i = 0; i < 40; ++i) {
        std::error_code ec;
        if (!fs::exists(dir, ec)) {
            return true;
        }
#if defined(_WIN32)
        Sleep(250);
#else
        break;
#endif
    }
    std::error_code ec;
    return !fs::exists(dir, ec);
}

#if defined(_WIN32)

BOOL CALLBACK first_window_of_process(HWND hwnd, LPARAM param) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    auto* search = reinterpret_cast<std::pair<DWORD, HWND>*>(param);
    if (pid == search->first) {
        search->second = hwnd;
        return FALSE;
    }
    return TRUE;
}

HWND find_window(DWORD pid) {
    std::pair<DWORD, HWND> search{pid, nullptr};
    for (int attempt = 0; attempt < 100 && search.second == nullptr; ++attempt) {
        EnumWindows(first_window_of_process, reinterpret_cast<LPARAM>(&search));
        if (search.second == nullptr) {
            Sleep(100);
        }
    }
    return search.second;
}

void send_key(HWND hwnd, WPARAM key) {
    PostMessageW(hwnd, WM_KEYDOWN, key, 0);
    PostMessageW(hwnd, WM_KEYUP, key, 0);
}

#endif

void preclean_install(const fs::path& dir, const fs::path& log) {
    if (!fs::is_regular_file(dir / "Uninstall.exe")) {
        return;
    }
    run_captured(cmd((dir / "Uninstall.exe").string(), "/S"), log);
    wait_until_gone(dir);
}

bool extract_zip_entry(const fs::path& zip, const std::string& entry, const fs::path& out,
                       std::string& error) {
    fs::create_directories(out.parent_path());
    const fs::path script = out.parent_path() / "extract-zip.ps1";
    {
        std::ofstream file(script, std::ios::binary);
        file << "param([string]$Zip, [string]$Entry, [string]$Out)\r\n"
                "Add-Type -AssemblyName System.IO.Compression.FileSystem\r\n"
                "$z = [System.IO.Compression.ZipFile]::OpenRead($Zip)\r\n"
                "try {\r\n"
                "  $e = $z.GetEntry($Entry)\r\n"
                "  if ($null -eq $e) { Write-Error \"missing $Entry\"; exit 2 }\r\n"
                "  $parent = Split-Path -Parent $Out\r\n"
                "  if ($parent -and -not (Test-Path $parent)) {\r\n"
                "    New-Item -ItemType Directory -Force -Path $parent | Out-Null\r\n"
                "  }\r\n"
                "  $stream = $e.Open()\r\n"
                "  $file = [System.IO.File]::Create($Out)\r\n"
                "  try { $stream.CopyTo($file) } finally { $file.Dispose(); $stream.Dispose() }\r\n"
                "} finally { $z.Dispose() }\r\n";
    }
    const fs::path log = out.parent_path() / "extract-zip.log";
    const Captured captured =
        run_captured(cmd("powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                         script.string(), zip.string(), entry, out.string()),
                     log);
    if (captured.code != 0 || !fs::is_regular_file(out)) {
        error = captured.text;
        return false;
    }
    return true;
}

}  // namespace

TEST(PackCli, CreateProduct) {
    const fs::path work = fresh_dir("product");
    const fs::path project = work / "app";
    const Captured created = run_captured(
        cmd(ZUI_CREATE_EXE, "ZuiProduct", project.string(), "--version", "1.2.3", "--identifier",
            "com.example.zuiproduct", "--page", "ui/index.html", "--entry", "src/main.cpp", "--icon",
            "icons/icon.svg", "--portable", "--android", "--permission", "network", "--reason",
            "Open network connections", "--file", "notes/extra.txt"),
        work / "create.log");
    ASSERT_EQ(created.code, 0) << created.text;
    const std::string desc = read_text(project / "zui.json");
    EXPECT_NE(desc.find("\"schema\": 2"), std::string::npos) << desc;
    EXPECT_NE(desc.find("\"name\": \"ZuiProduct\""), std::string::npos) << desc;
    EXPECT_NE(desc.find("\"version\": \"1.2.3\""), std::string::npos) << desc;
    EXPECT_NE(desc.find("\"identifier\": \"com.example.zuiproduct\""), std::string::npos) << desc;
    EXPECT_NE(desc.find("\"icon\": \"icons/icon.svg\""), std::string::npos) << desc;
    EXPECT_NE(desc.find("\"page\": \"ui/index.html\""), std::string::npos) << desc;
    EXPECT_NE(desc.find("\"entry\": \"src/main.cpp\""), std::string::npos) << desc;
    EXPECT_NE(desc.find("\"portable\""), std::string::npos) << desc;
    EXPECT_NE(desc.find("\"android\""), std::string::npos) << desc;
    EXPECT_NE(desc.find("\"name\": \"network\""), std::string::npos) << desc;
    EXPECT_NE(desc.find("\"android\": \"android.permission.INTERNET\""), std::string::npos) << desc;
    EXPECT_NE(desc.find("\"reason\": \"Open network connections\""), std::string::npos) << desc;
    EXPECT_NE(desc.find("notes/extra.txt"), std::string::npos) << desc;
    expect_app_surface(project);
    // A declared runtime file starts as a placeholder, never as text the author
    // did not write.
    EXPECT_EQ(read_text(project / "notes" / "extra.txt"), "");
    // The permission table is the declaration; the platform manifest is built
    // from it at package time, so no second file carries it.
    EXPECT_NE(desc.find("\"android\": \"android.permission.INTERNET\""), std::string::npos) << desc;
    EXPECT_EQ(desc.find("android.permission.CAMERA"), std::string::npos) << desc;
    EXPECT_NE(desc.find("\"android\": {"), std::string::npos) << desc;
    EXPECT_NE(desc.find("\"minSdk\": 24"), std::string::npos) << desc;
    EXPECT_FALSE(fs::exists(project / "android" / "AndroidManifest.xml")) << desc;
}

TEST(PackCli, PermissionCatalogCoversFilesAndSettings) {
    const zui_tool::CatalogPermission* storage = zui_tool::find_catalog_permission("storage");
    const zui_tool::CatalogPermission* write = zui_tool::find_catalog_permission("storage-write");
    const zui_tool::CatalogPermission* all = zui_tool::find_catalog_permission("manage-storage");
    const zui_tool::CatalogPermission* selected =
        zui_tool::find_catalog_permission("media-visual-selected");
    ASSERT_NE(storage, nullptr);
    ASSERT_NE(write, nullptr);
    ASSERT_NE(all, nullptr);
    ASSERT_NE(selected, nullptr);
    EXPECT_STREQ(storage->android, "android.permission.READ_EXTERNAL_STORAGE");
    EXPECT_EQ(storage->max_sdk, 32);
    EXPECT_STREQ(write->android, "android.permission.WRITE_EXTERNAL_STORAGE");
    EXPECT_EQ(write->max_sdk, 29);
    EXPECT_TRUE(write->runtime);
    EXPECT_STREQ(all->android, "android.permission.MANAGE_EXTERNAL_STORAGE");
    EXPECT_TRUE(all->settings);
    EXPECT_FALSE(all->runtime);
    EXPECT_STREQ(selected->android, "android.permission.READ_MEDIA_VISUAL_USER_SELECTED");
    EXPECT_STREQ(zui_tool::find_catalog_permission("camera")->ios_key, "NSCameraUsageDescription");
    EXPECT_EQ(sizeof(zui_tool::kPermissionCatalog) / sizeof(zui_tool::kPermissionCatalog[0]), 61u);
}


TEST(PackCli, MulticastCatalogEntryExists) {
    const zui_tool::CatalogPermission* entry =
        zui_tool::find_catalog_permission("multicast");
    ASSERT_NE(entry, nullptr);
    EXPECT_STREQ(entry->android, "android.permission.CHANGE_WIFI_MULTICAST_STATE");
    EXPECT_FALSE(entry->runtime);
    EXPECT_FALSE(entry->settings);
}

TEST(PackCli, PermissionCatalogMatchesATierEnum) {
    constexpr std::size_t catalog =
        sizeof(zui_tool::kPermissionCatalog) / sizeof(zui_tool::kPermissionCatalog[0]);
    EXPECT_EQ(catalog, static_cast<std::size_t>(zui::Permission::FullScreenIntent) + 1u);
    for (const zui_tool::CatalogPermission& entry : zui_tool::kPermissionCatalog) {
        zui::Permission parsed{};
        ASSERT_TRUE(zui::permission_parse(entry.name, parsed)) << entry.name;
        EXPECT_STREQ(zui::permission_name(parsed), entry.name);
    }
    for (int i = 0; i <= static_cast<int>(zui::Permission::FullScreenIntent); ++i) {
        const auto value = static_cast<zui::Permission>(i);
        const char* name = zui::permission_name(value);
        ASSERT_NE(zui_tool::find_catalog_permission(name), nullptr) << name;
    }
}

TEST(PackCli, PermissionDeclaration) {
    zui_tool::Project decl;
    decl.name = "ZuiPerm";
    decl.version = "1.2.3";
    decl.identifier = "com.example.zuiperm";
    decl.page = "ui/index.html";
    const char* names[] = {"camera", "storage", "storage-write", "biometric", "bluetooth-scan",
                           "location-background", "manage-storage"};
    const char* reasons[] = {"Scan a code with the camera",
                             "Read files the app saved",
                             "Save a photo the user took",
                             "Unlock with face or fingerprint",
                             "Find a nearby sensor",
                             "Keep a location while the screen is off",
                             "Read every file on the device"};
    for (std::size_t i = 0; i < 7; ++i) {
        const zui_tool::CatalogPermission* known = zui_tool::find_catalog_permission(names[i]);
        ASSERT_NE(known, nullptr) << names[i];
        zui_tool::Permission item;
        item.name = names[i];
        item.android = known->android;
        item.reason = reasons[i];
        decl.permissions.push_back(std::move(item));
    }
    int code = 0;
    std::string error;
    ASSERT_TRUE(zui_tool::android_version_code(decl.version, code, error)) << error;
    const std::string manifest = zui_tool::android_manifest(decl, code, false);
    EXPECT_NE(manifest.find("android.permission.CAMERA"), std::string::npos) << manifest;
    EXPECT_NE(manifest.find("android.permission.READ_EXTERNAL_STORAGE\" android:maxSdkVersion=\"32\""),
              std::string::npos)
        << manifest;
    EXPECT_NE(manifest.find("android.permission.WRITE_EXTERNAL_STORAGE\" android:maxSdkVersion=\"29\""),
              std::string::npos)
        << manifest;
    EXPECT_NE(manifest.find("android.permission.USE_BIOMETRIC"), std::string::npos) << manifest;
    EXPECT_NE(manifest.find("android.permission.USE_FINGERPRINT\" android:maxSdkVersion=\"28\""),
              std::string::npos)
        << manifest;
    EXPECT_NE(manifest.find("android.permission.BLUETOOTH_SCAN"), std::string::npos) << manifest;
    EXPECT_NE(manifest.find("android.permission.BLUETOOTH_ADMIN\" android:maxSdkVersion=\"30\""),
              std::string::npos)
        << manifest;
    EXPECT_NE(manifest.find("android.permission.MANAGE_EXTERNAL_STORAGE"), std::string::npos) << manifest;
    EXPECT_NE(manifest.find("android.hardware.camera\" android:required=\"false\""), std::string::npos)
        << manifest;
    EXPECT_EQ(manifest.find("RECORD_AUDIO"), std::string::npos) << manifest;

    const auto usages = zui_tool::apple_usage_entries(decl);
    auto reason_for = [&usages](const char* key) -> std::string {
        for (const auto& entry : usages) {
            if (entry.first == key) {
                return entry.second;
            }
        }
        return {};
    };
    EXPECT_EQ(reason_for("NSCameraUsageDescription"), reasons[0]);
    EXPECT_EQ(reason_for("NSPhotoLibraryAddUsageDescription"), reasons[2]);
    EXPECT_EQ(reason_for("NSFaceIDUsageDescription"), reasons[3]);
    EXPECT_EQ(reason_for("NSBluetoothAlwaysUsageDescription"), reasons[4]);
    EXPECT_EQ(reason_for("NSBluetoothPeripheralUsageDescription"), reasons[4]);
    EXPECT_EQ(reason_for("NSLocationAlwaysAndWhenInUseUsageDescription"), reasons[5]);
    EXPECT_EQ(reason_for("NSLocationWhenInUseUsageDescription"), reasons[5]);
    EXPECT_TRUE(reason_for("NSMicrophoneUsageDescription").empty());

    const fs::path work = fresh_dir("permission-decl");
    zui_tool::AppleBundleInfo info;
    info.name = decl.name;
    info.identifier = decl.identifier;
    info.version = decl.version;
    info.executable = decl.name;
    info.ios = true;
    zui_tool::AppleBundle bundle(work / "Payload", info);
    bundle.add_executable({0xcf, 0xfa, 0xed, 0xfe, 1});
    for (const auto& usage : usages) {
        bundle.set_extra(usage.first, usage.second);
    }
    bundle.set_extra("ZuiDeclaredPermissions", zui_tool::apple_declared_permissions(decl));
    ASSERT_TRUE(bundle.write(error)) << error;
    const std::string plist = bundle.plist();
    EXPECT_NE(plist.find("<key>NSCameraUsageDescription</key>\n\t<string>" + std::string(reasons[0])),
              std::string::npos)
        << plist;
    EXPECT_NE(plist.find(reasons[5]), std::string::npos) << plist;

    zui_tool::Project network_only;
    network_only.name = "ZuiNet";
    network_only.version = "1.0.0";
    network_only.identifier = "com.example.zuinet";
    const zui_tool::CatalogPermission* network = zui_tool::find_catalog_permission("network");
    ASSERT_NE(network, nullptr);
    zui_tool::Permission only;
    only.name = "network";
    only.android = network->android;
    only.reason = "Open network connections";
    network_only.permissions.push_back(only);
    int network_code = 0;
    ASSERT_TRUE(zui_tool::android_version_code(network_only.version, network_code, error)) << error;
    const std::string network_manifest = zui_tool::android_manifest(network_only, network_code, false);
    EXPECT_EQ(network_manifest.find("android.permission.CAMERA"), std::string::npos) << network_manifest;
    EXPECT_NE(network_manifest.find("android.permission.INTERNET"), std::string::npos) << network_manifest;
    const auto network_usage = zui_tool::apple_usage_entries(network_only);
    for (const auto& entry : network_usage) {
        EXPECT_NE(entry.first, "NSCameraUsageDescription") << entry.first;
    }
    EXPECT_NE(manifest, network_manifest);
}

TEST(PackCli, UnknownPermission) {
    const fs::path work = fresh_dir("unknown-permission");
    const fs::path project = work / "app";
    const Captured created = run_captured(
        cmd(ZUI_CREATE_EXE, "ZuiNope", project.string(), "--permission", "nope"), work / "create.log");
    EXPECT_NE(created.code, 0) << created.text;
    EXPECT_NE(created.text.find("unknown permission nope"), std::string::npos) << created.text;
    EXPECT_FALSE(fs::exists(project));
}

TEST(PackCli, MissingPage) {
    const fs::path work = fresh_dir("missing-page");
    const fs::path project = work / "app";
    const Captured created =
        run_captured(cmd(ZUI_CREATE_EXE, "ZuiMissing", project.string(), "--portable"), work / "create.log");
    ASSERT_EQ(created.code, 0) << created.text;
    const fs::path zip = project / "dist" / "ZuiMissing-1.0.0-portable.zip";
    fs::create_directories(zip.parent_path());
    {
        std::ofstream planted(zip, std::ios::binary);
        planted << "stale";
    }
    ASSERT_TRUE(fs::is_regular_file(zip));
    fs::remove(project / "ui" / "index.html");
    const Captured packed = run_captured(
        cmd(ZUI_PACK_EXE, project.string(), "--root", ZUI_ROOT_DIR, "--build", ZUI_BUILD_DIR),
        work / "pack.log");
    EXPECT_NE(packed.code, 0) << packed.text;
    EXPECT_NE(packed.text.find("missing page ui/index.html"), std::string::npos) << packed.text;
    EXPECT_FALSE(fs::exists(zip)) << packed.text;
}

TEST(PackCli, DesktopPackage) {
    const fs::path zui_lib = fs::path(ZUI_BUILD_DIR) / "zui.lib";
    ASSERT_TRUE(fs::is_regular_file(zui_lib)) << zui_lib;
    const fs::path work = fresh_dir("desktop");
    const fs::path project = work / "app";
    const fs::path installed = local_programs() / "ZuiPackSample";
    preclean_install(installed, work / "preclean.log");
    const Captured created = run_captured(
        cmd(ZUI_CREATE_EXE, "ZuiPackSample", project.string(), "--version", "1.4.0", "--portable",
            "--installer"),
        work / "create.log");
    ASSERT_EQ(created.code, 0) << created.text;
    const Captured packed = run_captured(
        cmd(ZUI_PACK_EXE, project.string(), "--root", ZUI_ROOT_DIR, "--build", ZUI_BUILD_DIR),
        work / "pack.log");
    ASSERT_EQ(packed.code, 0) << packed.text;
    // The template copies the page next to the executable, so the project also
    // runs from its own build directory, before it is packed.
    const fs::path built = built_exe(project, "ZuiPackSample");
    ASSERT_FALSE(built.empty()) << packed.text;
    EXPECT_TRUE(fs::is_regular_file(built.parent_path() / "ui" / "index.html")) << packed.text;
    const fs::path zip = project / "dist" / "ZuiPackSample-1.4.0-portable.zip";
    const fs::path setup = project / "dist" / "ZuiPackSample-1.4.0-setup.exe";
    ASSERT_TRUE(fs::is_regular_file(zip)) << packed.text;
    ASSERT_TRUE(fs::is_regular_file(setup)) << packed.text;
    const fs::path extracted = work / "unzipped";
    fs::create_directories(extracted);
    std::string error;
    ASSERT_TRUE(extract_zip_entry(zip, "ZuiPackSample.exe", extracted / "ZuiPackSample.exe", error)) << error;
    // The package keeps the page at the path zui.json names, and the app opens
    // it relative to its working directory. Unpacking the archive the way a
    // user does is the only layout that has to work.
    ASSERT_TRUE(extract_zip_entry(zip, "ui/index.html", extracted / "ui" / "index.html", error)) << error;
    EXPECT_NE(read_text(extracted / "ui" / "index.html").find("Hello from zui"), std::string::npos);
    for (const char* dll : kRuntimeDlls) {
        ASSERT_TRUE(extract_zip_entry(zip, dll, extracted / dll, error)) << dll << " " << error;
        EXPECT_GT(fs::file_size(extracted / dll), 0u) << dll;
    }
    EXPECT_TRUE(exe_has_icon(extracted / "ZuiPackSample.exe"));
    // Explorer reads this resource; a package without it looks unfinished.
    EXPECT_EQ(version_field(extracted / "ZuiPackSample.exe", L"ProductName"),
              "ZuiPackSample");
    EXPECT_EQ(version_field(extracted / "ZuiPackSample.exe", L"FileVersion"), "1.4.0");
    EXPECT_EQ(version_field(extracted / "ZuiPackSample.exe", L"CompanyName"), "");
    const int status = start_status(extracted / "ZuiPackSample.exe");
    EXPECT_NE(status, static_cast<int>(0xC0000135)) << status;
    EXPECT_NE(status, -1) << status;
    // 259 is STILL_ACTIVE: the app loaded the page and stayed in its loop. An
    // immediate exit would mean the packaged app cannot find its own page.
    EXPECT_EQ(status, 259) << status;
    // The archive deflates its entries. A Debug build of the runtime is a few
    // times the archive, so storing it raw would show up here.
    std::uintmax_t unpacked = 0;
    for (const auto& entry : fs::recursive_directory_iterator(extracted)) {
        if (entry.is_regular_file()) {
            unpacked += entry.file_size();
        }
    }
    EXPECT_GT(unpacked, fs::file_size(zip) * 2) << unpacked << " vs " << fs::file_size(zip);

    const Captured installed_run =
        run_captured(cmd(setup.string(), "/S"), work / "install.log");
    ASSERT_EQ(installed_run.code, 0) << installed_run.text;
    EXPECT_TRUE(fs::is_regular_file(installed / "ZuiPackSample.exe")) << installed_run.text;
    const std::string product = read_text(installed / "product.txt");
    EXPECT_NE(product.find("ZuiPackSample\n"), std::string::npos) << product;
    EXPECT_NE(product.find("1.4.0"), std::string::npos) << product;
    const Captured removed =
        run_captured(cmd((installed / "Uninstall.exe").string(), "/S"), work / "uninstall.log");
    ASSERT_EQ(removed.code, 0) << removed.text;
    EXPECT_TRUE(wait_until_gone(installed)) << removed.text;
}

TEST(PackCli, AndroidPermission) {
    const fs::path work = fresh_dir("android-permission");
    const fs::path project = work / "app";
    const Captured created = run_captured(
        cmd(ZUI_CREATE_EXE, "ZuiMobile", project.string(), "--android", "--permission", "network",
            "--reason", "Open network connections", "--permission", "camera", "--permission",
            "storage"),
        work / "create.log");
    ASSERT_EQ(created.code, 0) << created.text;
    // The declaration is zui.json; the platform manifest is the packer's.
    const std::string desc = read_text(project / "zui.json");
    EXPECT_NE(desc.find("android.permission.INTERNET"), std::string::npos) << desc;
    EXPECT_FALSE(fs::exists(project / "android" / "AndroidManifest.xml")) << desc;
    const fs::path apk = project / "dist" / "ZuiMobile-1.0.0.apk";
    const Captured packed = run_captured(
        cmd(ZUI_PACK_EXE, "android", project.string(), "--root", ZUI_ROOT_DIR, "--build",
            ZUI_BUILD_DIR),
        work / "android.log");
    if (packed.code != 0) {
        const bool named = packed.text.find("missing Android SDK") != std::string::npos ||
                           packed.text.find("missing JDK") != std::string::npos ||
                           packed.text.find("missing Android NDK") != std::string::npos ||
                           packed.text.find("missing the android build of zui") != std::string::npos;
        EXPECT_TRUE(named) << packed.text;
        EXPECT_FALSE(fs::exists(apk)) << packed.text;
        return;
    }
    ASSERT_TRUE(fs::is_regular_file(apk)) << packed.text;
    const fs::path aapt2 = find_aapt2();
    ASSERT_TRUE(fs::is_regular_file(aapt2)) << aapt2;
    // The manifest names the SDL activity, the SDK window, and the permission.
    const Captured dumped = run_captured(
        cmd(aapt2.string(), "dump", "xmltree", apk.string(), "--file", "AndroidManifest.xml"),
        work / "manifest-dump.log");
    ASSERT_EQ(dumped.code, 0) << dumped.text;
    EXPECT_NE(dumped.text.find("android.permission.INTERNET"), std::string::npos) << dumped.text;
    // A runtime permission keeps its hardware feature, declared as not required.
    EXPECT_NE(dumped.text.find("android.permission.CAMERA"), std::string::npos) << dumped.text;
    EXPECT_NE(dumped.text.find("android.hardware.camera"), std::string::npos) << dumped.text;
    // A permission that a newer API level replaced carries its ceiling.
    EXPECT_NE(dumped.text.find("android.permission.READ_EXTERNAL_STORAGE"), std::string::npos)
        << dumped.text;
    EXPECT_NE(dumped.text.find("maxSdkVersion"), std::string::npos) << dumped.text;
    EXPECT_EQ(xml_element_count(dumped.text, "uses-permission"), 3) << dumped.text;
    EXPECT_NE(dumped.text.find("com.zui.runtime.ZuiActivity"), std::string::npos) << dumped.text;
    const Captured badging =
        run_captured(cmd(aapt2.string(), "dump", "badging", apk.string()), work / "badging.log");
    ASSERT_EQ(badging.code, 0) << badging.text;
    EXPECT_NE(badging.text.find("versionCode='1000000'"), std::string::npos) << badging.text;
    EXPECT_NE(badging.text.find("versionName='1.0.0'"), std::string::npos) << badging.text;
    EXPECT_NE(badging.text.find("minSdkVersion:'24'"), std::string::npos) << badging.text;
    EXPECT_NE(badging.text.find("targetSdkVersion:'36'"), std::string::npos) << badging.text;
    // The APK runs the engine: the app library, SDL's Java classes, the payload.
    const std::string bytes = read_text(apk);
    EXPECT_NE(bytes.find("lib/arm64-v8a/libzui.so"), std::string::npos);
    EXPECT_NE(bytes.find("lib/arm64-v8a/libmain.so"), std::string::npos);
    EXPECT_NE(bytes.find("lib/arm64-v8a/libzui_bridge.so"), std::string::npos);
    EXPECT_NE(bytes.find("org/libsdl/app/SDLActivity"), std::string::npos);
    EXPECT_NE(bytes.find("com/zui/runtime/ZuiPayload"), std::string::npos);
    EXPECT_NE(bytes.find("assets/payload/ui/index.html"), std::string::npos);
    // A package nobody can install is not a package.
    const fs::path signer = fs::path(ZUI_BUILD_DIR).parent_path() / ".." / ".." /
                            "Android" / "Sdk" / "build-tools";
    fs::path apksigner;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(signer, ec)) {
        if (ec || !entry.is_directory()) {
            continue;
        }
        const fs::path jar = entry.path() / "lib" / "apksigner.jar";
        if (fs::is_regular_file(jar)) {
            apksigner = jar;
        }
    }
    if (!apksigner.empty()) {
        const fs::path java = fs::path("D:/Program Files (x86)/AndroidStudio/jbr/bin/java.exe");
        if (fs::is_regular_file(java)) {
            const Captured verify =
                run_captured(cmd(java.string(), "-jar", apksigner.string(), "verify", "--verbose",
                                 apk.string()),
                             work / "verify.log");
            EXPECT_EQ(verify.code, 0) << verify.text;
            EXPECT_NE(verify.text.find("Verified using v2 scheme"), std::string::npos)
                << verify.text;
        }
    }
}

// Windows metadata: the version resource and the signing report. A package
// with a certificate signs; without one it says so instead of pretending.
TEST(PackCli, WindowsMetadata) {
    const fs::path work = fresh_dir("windows-metadata");
    const fs::path project = work / "app";
    const Captured created = run_captured(
        cmd(ZUI_CREATE_EXE, "ZuiMeta", project.string(), "--version", "2.3.4", "--portable"),
        work / "create.log");
    ASSERT_EQ(created.code, 0) << created.text;
    std::string desc = read_text(project / "zui.json");
    const std::string anchor = "\"targets\": [\"portable\"]";
    const std::size_t at = desc.find(anchor);
    ASSERT_NE(at, std::string::npos) << desc;
    desc.insert(at + anchor.size(),
                ",\n  \"windows\": {\n    \"description\": \"A packaged sample\",\n"
                "    \"company\": \"zui\",\n    \"copyright\": \"Copyright (c) zui\"\n  }");
    {
        std::ofstream out(project / "zui.json", std::ios::binary);
        out << desc;
    }
    const Captured packed = run_captured(
        cmd(ZUI_PACK_EXE, project.string(), "--root", ZUI_ROOT_DIR, "--build", ZUI_BUILD_DIR),
        work / "pack.log");
    ASSERT_EQ(packed.code, 0) << packed.text;
    EXPECT_NE(packed.text.find("did not sign"), std::string::npos) << packed.text;
    const fs::path extracted = work / "unzipped";
    std::string error;
    ASSERT_TRUE(extract_zip_entry(project / "dist" / "ZuiMeta-2.3.4-portable.zip", "ZuiMeta.exe",
                                  extracted / "ZuiMeta.exe", error))
        << error;
    EXPECT_EQ(version_field(extracted / "ZuiMeta.exe", L"ProductName"), "ZuiMeta");
    EXPECT_EQ(version_field(extracted / "ZuiMeta.exe", L"FileVersion"), "2.3.4");
    EXPECT_EQ(version_field(extracted / "ZuiMeta.exe", L"FileDescription"), "A packaged sample");
    EXPECT_EQ(version_field(extracted / "ZuiMeta.exe", L"CompanyName"), "zui");
    EXPECT_EQ(version_field(extracted / "ZuiMeta.exe", L"LegalCopyright"), "Copyright (c) zui");
    EXPECT_NE(version_field(extracted / "ZuiMeta.exe", L"OriginalFilename"), "");
    // A certificate that is not in the store has to fail, not silently pass.
    const std::string thumbprint =
        "\"certificate\": \"0000000000000000000000000000000000000000\"";
    const std::size_t windows_at = desc.find("\"windows\"");
    ASSERT_NE(windows_at, std::string::npos) << desc;
    desc.insert(desc.find('{', windows_at) + 1, "\n    " + thumbprint + ",");
    {
        std::ofstream out(project / "zui.json", std::ios::binary);
        out << desc;
    }
    const fs::path signtool = fs::path("C:/Program Files (x86)/Windows Kits/10/bin");
    if (fs::is_directory(signtool)) {
        const Captured signed_pack = run_captured(
            cmd(ZUI_PACK_EXE, project.string(), "--root", ZUI_ROOT_DIR, "--build", ZUI_BUILD_DIR),
            work / "sign.log");
        EXPECT_NE(signed_pack.code, 0) << signed_pack.text;
        EXPECT_NE(signed_pack.text.find("signtool"), std::string::npos) << signed_pack.text;
    }
}

TEST(PackCli, InteractiveCreate) {
    const fs::path work = fresh_dir("wizard");
    const fs::path project = work / "app";
    const fs::path answers = work / "answers.txt";
    {
        std::ofstream file(answers);
        file << "ZuiWizard\n"
             << project.string() << "\n"
             << "1.2.3\n"
             << "com.example.wizard\n"
             << "ui/home.html\n"
             << "src/main.cpp\n"
             << "icons/icon.svg\n"
             << "y\n"
             << "n\n"
             << "y\n"
             << "network\n"
             << "Open a page from the network\n"
             << "\n"
             << "notes/extra.txt\n"
             << "y\n";
    }
    const Captured created =
        run_with_input(cmd(ZUI_CREATE_EXE, "--interactive"), work / "create.log", answers);
    ASSERT_EQ(created.code, 0) << created.text;
    EXPECT_NE(created.text.find("Project name:"), std::string::npos) << created.text;
    EXPECT_NE(created.text.find("Mobile permissions"), std::string::npos) << created.text;
    EXPECT_NE(created.text.find("android.permission.CAMERA"), std::string::npos) << created.text;
    EXPECT_NE(created.text.find("Create this project?"), std::string::npos) << created.text;
    const std::string desc = read_text(project / "zui.json");
    EXPECT_NE(desc.find("\"name\": \"ZuiWizard\""), std::string::npos) << desc;
    EXPECT_NE(desc.find("\"version\": \"1.2.3\""), std::string::npos) << desc;
    EXPECT_NE(desc.find("\"identifier\": \"com.example.wizard\""), std::string::npos) << desc;
    EXPECT_NE(desc.find("\"page\": \"ui/home.html\""), std::string::npos) << desc;
    EXPECT_NE(desc.find("\"portable\""), std::string::npos) << desc;
    EXPECT_NE(desc.find("\"android\""), std::string::npos) << desc;
    EXPECT_EQ(desc.find("installer"), std::string::npos) << desc;
    EXPECT_NE(desc.find("\"reason\": \"Open a page from the network\""), std::string::npos) << desc;
    EXPECT_EQ(desc.find("android.permission.CAMERA"), std::string::npos) << desc;
    EXPECT_NE(read_text(project / "src" / "main.cpp").find("#include <zui/app.h>"), std::string::npos);
    EXPECT_TRUE(fs::is_regular_file(project / "ui" / "home.html"));
    EXPECT_EQ(read_text(project / "notes" / "extra.txt"), "");
    EXPECT_NE(desc.find("android.permission.INTERNET"), std::string::npos) << desc;
    EXPECT_EQ(desc.find("android.permission.CAMERA"), std::string::npos) << desc;
}

TEST(PackCli, InteractiveCancel) {
    const fs::path work = fresh_dir("wizard-cancel");
    const fs::path project = work / "app";
    const fs::path answers = work / "answers.txt";
    {
        std::ofstream file(answers);
        file << "ZuiCancel\n"
             << project.string() << "\n"
             << "\n"
             << "\n"
             << "\n"
             << "\n"
             << "\n"
             << "y\n"
             << "n\n"
             << "n\n"
             << "\n"
             << "n\n";
    }
    const Captured created =
        run_with_input(cmd(ZUI_CREATE_EXE, "--interactive"), work / "create.log", answers);
    EXPECT_NE(created.code, 0) << created.text;
    EXPECT_NE(created.text.find("cancelled"), std::string::npos) << created.text;
    EXPECT_FALSE(fs::exists(project));
}

// A declared runtime file starts empty or as the smallest document of its
// kind, and a path with no extension is a directory. The build then carries
// the page, the icon, and those files next to the executable.
// The scaffold opens its page from beside the executable, so the program runs
// from its own build directory and from anywhere else.
TEST(PackCli, ScaffoldRunsFromAnyDirectory) {
    const fs::path work = fresh_dir("scaffold-run");
    const fs::path project = work / "app";
    const Captured created = run_captured(
        cmd(ZUI_CREATE_EXE, "ZuiRuns", project.string(), "--portable"), work / "create.log");
    ASSERT_EQ(created.code, 0) << created.text;
    const Captured packed = run_captured(
        cmd(ZUI_PACK_EXE, project.string(), "--root", ZUI_ROOT_DIR, "--build", ZUI_BUILD_DIR),
        work / "pack.log");
    ASSERT_EQ(packed.code, 0) << packed.text;
    const fs::path exe = built_exe(project, "ZuiRuns");
    ASSERT_FALSE(exe.empty()) << packed.text;
    ASSERT_TRUE(fs::is_regular_file(exe.parent_path() / "ui" / "index.html")) << packed.text;
    // Started from a directory that holds no page at all.
    const int status = start_status(exe, work);
    EXPECT_EQ(status, 259) << "the app did not open its page from beside the executable";
}

// A page reference the declaration does not carry is reported: the packer ships
// what zui.json names and does not read markup.
TEST(PackCli, UnshippedReferenceIsReported) {
    const fs::path work = fresh_dir("unshipped");
    const fs::path project = work / "app";
    const Captured created = run_captured(
        cmd(ZUI_CREATE_EXE, "ZuiRefs", project.string(), "--portable"), work / "create.log");
    ASSERT_EQ(created.code, 0) << created.text;
    const Captured clean = run_captured(
        cmd(ZUI_PACK_EXE, project.string(), "--root", ZUI_ROOT_DIR, "--build", ZUI_BUILD_DIR),
        work / "clean.log");
    ASSERT_EQ(clean.code, 0) << clean.text;
    EXPECT_EQ(clean.text.find("refers to"), std::string::npos) << clean.text;
    // The page gains a stylesheet nobody declared.
    std::string page = read_text(project / "ui" / "index.html");
    page.insert(page.find("<title>"), "  <link rel=\"stylesheet\" href=\"theme.css\">\n");
    {
        std::ofstream out(project / "ui" / "index.html", std::ios::binary);
        out << page;
    }
    {
        std::ofstream out(project / "ui" / "theme.css", std::ios::binary);
        out << "body { background: #fff; }\n";
    }
    const Captured warned = run_captured(
        cmd(ZUI_PACK_EXE, project.string(), "--root", ZUI_ROOT_DIR, "--build", ZUI_BUILD_DIR),
        work / "warn.log");
    EXPECT_NE(warned.text.find("refers to ui/theme.css"), std::string::npos) << warned.text;
}

// An icon the packer cannot draw is an error, not a silently flat rectangle.
TEST(PackCli, UndrawableIconFails) {
    const fs::path work = fresh_dir("undrawable-icon");
    const fs::path project = work / "app";
    const Captured created = run_captured(
        cmd(ZUI_CREATE_EXE, "ZuiIcon", project.string(), "--portable"), work / "create.log");
    ASSERT_EQ(created.code, 0) << created.text;
    {
        std::ofstream out(project / "icons" / "icon.svg", std::ios::binary);
        out << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 64 64\">\n"
            << "  <path d=\"M8 56 L32 8 L56 56 Z\" fill=\"#2c6b4e\"/>\n"
            << "</svg>\n";
    }
    const Captured packed = run_captured(
        cmd(ZUI_PACK_EXE, project.string(), "--root", ZUI_ROOT_DIR, "--build", ZUI_BUILD_DIR),
        work / "pack.log");
    EXPECT_NE(packed.code, 0) << packed.text;
    EXPECT_NE(packed.text.find("cannot draw icons/icon.svg"), std::string::npos) << packed.text;
}

TEST(PackCli, CreateDeclaresRuntimeFiles) {
    const fs::path work = fresh_dir("create-files");
    const fs::path project = work / "app";
    const Captured created = run_captured(
        cmd(ZUI_CREATE_EXE, "ZuiFiles", project.string(), "--portable", "--file", "ui/app.css",
            "--file", "notes/extra.txt", "--file", "assets", "--file", "data/settings.json"),
        work / "create.log");
    ASSERT_EQ(created.code, 0) << created.text;
    // The stylesheet is part of the scaffold; declaring it keeps the template.
    EXPECT_NE(read_text(project / "ui" / "app.css").find("button.primary"), std::string::npos);
    EXPECT_EQ(read_text(project / "notes" / "extra.txt"), "");
    EXPECT_EQ(read_text(project / "data" / "settings.json"), "{}\n");
    EXPECT_TRUE(fs::is_directory(project / "assets")) << created.text;
    EXPECT_TRUE(fs::is_regular_file(project / "README.md"));
    EXPECT_TRUE(fs::is_regular_file(project / ".gitignore"));
    const std::string build = read_text(project / "CMakeLists.txt");
    EXPECT_NE(build.find("$<TARGET_FILE_DIR:ZuiFiles>/ui/index.html"), std::string::npos) << build;
    EXPECT_NE(build.find("$<TARGET_FILE_DIR:ZuiFiles>/assets"), std::string::npos) << build;
    EXPECT_NE(build.find("copy_directory"), std::string::npos) << build;
    // The stylesheet is scaffold, not a placeholder: it is not reported as one.
    EXPECT_EQ(created.text.find("placeholder ui/app.css"), std::string::npos) << created.text;
}

// The project file carries its schema. A project from a newer tool fails with
// a message that names the schema, and leaves no package behind.
TEST(PackCli, RejectsNewerSchema) {
    const fs::path work = fresh_dir("schema");
    const fs::path project = work / "app";
    const Captured created =
        run_captured(cmd(ZUI_CREATE_EXE, "ZuiSchema", project.string(), "--portable"),
                     work / "create.log");
    ASSERT_EQ(created.code, 0) << created.text;
    std::string desc = read_text(project / "zui.json");
    const std::string from = "\"schema\": 2";
    const std::size_t at = desc.find(from);
    ASSERT_NE(at, std::string::npos) << desc;
    desc.replace(at, from.size(), "\"schema\": 99");
    {
        std::ofstream out(project / "zui.json", std::ios::binary);
        out << desc;
    }
    const Captured packed = run_captured(
        cmd(ZUI_PACK_EXE, project.string(), "--root", ZUI_ROOT_DIR, "--build", ZUI_BUILD_DIR),
        work / "pack.log");
    EXPECT_NE(packed.code, 0) << packed.text;
    EXPECT_NE(packed.text.find("schema 99"), std::string::npos) << packed.text;
    EXPECT_FALSE(fs::exists(project / "dist" / "ZuiSchema-1.0.0-portable.zip")) << packed.text;
}

// The install window is the interactive path. Drive it the way a user does:
// the path field holds the focus, Enter installs, Escape closes the finished
// window, and the same files and uninstall entry land as with /S.
TEST(PackCli, InteractiveInstall) {
    const fs::path work = fresh_dir("interactive-install");
    const fs::path project = work / "app";
    const fs::path installed = local_programs() / "ZuiInteractiveSample";
    preclean_install(installed, work / "preclean.log");
    const Captured created = run_captured(
        cmd(ZUI_CREATE_EXE, "ZuiInteractiveSample", project.string(), "--version", "2.0.1",
            "--installer"),
        work / "create.log");
    ASSERT_EQ(created.code, 0) << created.text;
    const Captured packed = run_captured(
        cmd(ZUI_PACK_EXE, project.string(), "--root", ZUI_ROOT_DIR, "--build", ZUI_BUILD_DIR),
        work / "pack.log");
    ASSERT_EQ(packed.code, 0) << packed.text;
    const fs::path setup = project / "dist" / "ZuiInteractiveSample-2.0.1-setup.exe";
    ASSERT_TRUE(fs::is_regular_file(setup)) << packed.text;
#if defined(_WIN32)
    std::wstring command = L"\"" + setup.wstring() + L"\"";
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    ASSERT_TRUE(CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                               work.wstring().c_str(), &si, &pi))
        << setup;
    const HWND window = find_window(pi.dwProcessId);
    ASSERT_NE(window, nullptr) << "the install window did not open";
    send_key(window, VK_RETURN);
    bool wrote_files = false;
    for (int attempt = 0; attempt < 150 && !wrote_files; ++attempt) {
        std::error_code ec;
        wrote_files = fs::is_regular_file(installed / "ZuiInteractiveSample.exe", ec) &&
                      fs::is_regular_file(installed / "product.txt", ec);
        if (!wrote_files) {
            Sleep(200);
        }
    }
    EXPECT_TRUE(wrote_files) << "Enter did not install";
    Sleep(1000);  // the last step (the uninstall entry) and then the finish screen
    send_key(window, VK_ESCAPE);
    const DWORD waited = WaitForSingleObject(pi.hProcess, 15000);
    DWORD code = 1;
    if (waited == WAIT_OBJECT_0) {
        GetExitCodeProcess(pi.hProcess, &code);
    }
    EXPECT_EQ(waited, WAIT_OBJECT_0) << "the install window did not close";
    EXPECT_EQ(code, 0u) << "the window reported a failed install";
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    const std::string product = read_text(installed / "product.txt");
    EXPECT_NE(product.find("ZuiInteractiveSample\n"), std::string::npos) << product;
    EXPECT_NE(product.find("2.0.1"), std::string::npos) << product;
    // Leave the machine as it was.
    const Captured removed =
        run_captured(cmd((installed / "Uninstall.exe").string(), "/S"), work / "uninstall.log");
    ASSERT_EQ(removed.code, 0) << removed.text;
    EXPECT_TRUE(wait_until_gone(installed)) << removed.text;
#endif
}

// The installer window is drawn by setup_ui, which the preview also renders
// offscreen. The screens are reviewable as images, and a broken paint shows up
// as a missing or tiny file instead of as a window that fails in front of a
// user.
TEST(PackCli, SetupPreviewRenders) {
    const fs::path work = fresh_dir("setup-preview");
    const Captured rendered =
        run_captured(cmd(ZUI_SETUP_PREVIEW_EXE, work.string()), work / "preview.log");
    ASSERT_EQ(rendered.code, 0) << rendered.text;
    const char* names[] = {
        "install-zh-100.png", "install-zh-175.png", "install-en-100.png", "progress-zh-100.png",
        "done-zh-100.png",    "error-zh-100.png",    "uninstall-zh-100.png",
    };
    for (const char* name : names) {
        const fs::path path = work / name;
        ASSERT_TRUE(fs::is_regular_file(path)) << name << " " << rendered.text;
        EXPECT_GT(fs::file_size(path), 2000u) << name;
    }
    // The client area is fixed, so the image size pins the layout.
    const std::string bytes = read_text(work / "install-zh-100.png");
    ASSERT_GE(bytes.size(), 24u);
    const auto be32 = [&bytes](std::size_t at) {
        const auto byte = [&bytes](std::size_t index) {
            return static_cast<unsigned>(static_cast<unsigned char>(bytes[index]));
        };
        return (byte(at) << 24) | (byte(at + 1) << 16) | (byte(at + 2) << 8) | byte(at + 3);
    };
    EXPECT_EQ(be32(16), 520u) << "client width";
    EXPECT_EQ(be32(20), 320u) << "client height";
}

// The archive primitives a .deb and a .tar.gz are made of: a round trip through
// each writer and its reader.
TEST(PackCli, ArchivePrimitives) {
    // md5sums is how dpkg --verify checks a package, so the digest is checked
    // against the values everyone knows.
    EXPECT_EQ(zui_tool::md5_hex(nullptr, 0), "d41d8cd98f00b204e9800998ecf8427e");
    const std::string abc = "abc";
    EXPECT_EQ(zui_tool::md5_hex(reinterpret_cast<const std::uint8_t*>(abc.data()), abc.size()),
              "900150983cd24fb0d6963f7d28e17f72");

    const std::vector<std::uint8_t> text = {'h', 'e', 'l', 'l', 'o', '\n'};
    std::vector<std::uint8_t> gz;
    std::string error;
    ASSERT_TRUE(zui_tool::gzip_bytes(text, gz, error)) << error;
    ASSERT_GT(gz.size(), text.size()) << "gzip keeps a header and a trailer";
    EXPECT_EQ(gz[0], 0x1f);
    EXPECT_EQ(gz[1], 0x8b);
    std::vector<std::uint8_t> back;
    ASSERT_TRUE(zui_tool::gunzip_bytes(gz.data(), gz.size(), back, error)) << error;
    EXPECT_EQ(back, text);

    zui_tool::TarWriter tar;
    tar.add_directory("./usr/bin/");
    tar.add_text("./usr/bin/app", "#!/bin/sh\nexit 0\n", 0755);
    tar.add("./usr/lib/app/libzui.so", std::vector<std::uint8_t>(4096, 7), 0755);
    const std::vector<std::uint8_t> tar_bytes = tar.finish();
    std::vector<std::pair<std::string, std::size_t>> members;
    ASSERT_TRUE(zui_tool::tar_list(tar_bytes.data(), tar_bytes.size(), members, error)) << error;
    ASSERT_EQ(members.size(), 3u);
    EXPECT_EQ(members[1].first, "./usr/bin/app");
    EXPECT_EQ(members[1].second, std::string("#!/bin/sh\nexit 0\n").size());
    EXPECT_EQ(members[2].second, 4096u);

    zui_tool::ArWriter ar;
    ar.add("debian-binary", std::vector<std::uint8_t>{'2', '.', '0', '\n'});
    ar.add("control.tar.gz", gz);
    ar.add("data.tar.gz", tar_bytes);
    const std::vector<std::uint8_t> deb = ar.finish();
    std::vector<std::pair<std::string, std::size_t>> ar_members;
    ASSERT_TRUE(zui_tool::ar_list(deb.data(), deb.size(), ar_members, error)) << error;
    ASSERT_EQ(ar_members.size(), 3u);
    EXPECT_EQ(ar_members[0].first, "debian-binary");
    EXPECT_EQ(ar_members[1].first, "control.tar.gz");
    EXPECT_EQ(ar_members[2].first, "data.tar.gz");
    EXPECT_EQ(ar_members[2].second, tar_bytes.size());
}

// The Apple bundle: the layout and the Info.plist, which are ours because no
// Apple tool runs on this host.
TEST(PackCli, AppleBundleLayout) {
    const fs::path work = fresh_dir("apple-bundle");
    zui_tool::AppleBundleInfo info;
    info.name = "ZuiApple";
    info.identifier = "com.example.zuiapple";
    info.version = "1.2.3";
    info.executable = "ZuiApple";
    info.minimum_system = "10.15";
    info.category = "public.app-category.utilities";
    zui_tool::AppleBundle macos(work / "macos", info);
    macos.add_executable({0xcf, 0xfa, 0xed, 0xfe, 1, 2, 3, 4});
    macos.add_resource("ui/index.html", {'<', 'h', 't', 'm', 'l', '>'});
    macos.add_resource("icon.icns", {1, 2, 3, 4});
    macos.set_extra("CFBundleIconFile", "icon.icns");
    std::string error;
    ASSERT_TRUE(macos.write(error)) << error;
    EXPECT_TRUE(fs::is_regular_file(macos.app_root() / "Contents" / "MacOS" / "ZuiApple"));
    EXPECT_TRUE(fs::is_regular_file(macos.app_root() / "Contents" / "Resources" / "ui" /
                                   "index.html"));
    const std::string plist = read_text(macos.app_root() / "Contents" / "Info.plist");
    EXPECT_NE(plist.find("<key>CFBundleName</key>\n\t<string>ZuiApple</string>"), std::string::npos)
        << plist;
    EXPECT_NE(plist.find("<string>com.example.zuiapple</string>"), std::string::npos) << plist;
    EXPECT_NE(plist.find("<string>1.2.3</string>"), std::string::npos) << plist;
    EXPECT_EQ(read_text(macos.app_root() / "Contents" / "Resources" / "ui" / "index.html"),
              "<html>");
    EXPECT_NE(plist.find("<key>CFBundleExecutable</key>"), std::string::npos) << plist;
    EXPECT_NE(plist.find("<string>icon.icns</string>"), std::string::npos) << plist;
    EXPECT_NE(plist.find("<key>LSMinimumSystemVersion</key>"), std::string::npos) << plist;
    EXPECT_EQ(plist.find("LSRequiresIPhoneOS"), std::string::npos) << "a macOS bundle is not iOS";

    zui_tool::AppleBundleInfo phone = info;
    phone.name = "ZuiPhone";
    phone.executable = "ZuiPhone";
    phone.ios = true;
    phone.orientations = {"UIInterfaceOrientationPortrait"};
    zui_tool::AppleBundle ios(work / "ios" / "Payload", phone);
    ios.add_executable({0xcf, 0xfa, 0xed, 0xfe, 9});
    ASSERT_TRUE(ios.write(error)) << error;
    EXPECT_TRUE(fs::is_regular_file(ios.app_root() / "ZuiPhone")) << "a flat iOS layout";
    EXPECT_TRUE(fs::is_regular_file(ios.app_root() / "Info.plist"));
    const std::string phone_plist = read_text(ios.app_root() / "Info.plist");
    EXPECT_NE(phone_plist.find("LSRequiresIPhoneOS"), std::string::npos) << phone_plist;
    EXPECT_NE(phone_plist.find("UIInterfaceOrientationPortrait"), std::string::npos) << phone_plist;
    EXPECT_EQ(phone_plist.find("LSMinimumSystemVersion"), std::string::npos) << phone_plist;
    EXPECT_TRUE(zui_tool::is_macho(ios.app_root() / "ZuiPhone"));
}

// --platform: the names it accepts, the ones it refuses, and the platforms this
// host cannot build (they say so, and write nothing).
TEST(PackCli, PlatformSelection) {
    const fs::path work = fresh_dir("platform");
    const fs::path project = work / "app";
    const Captured created = run_captured(
        cmd(ZUI_CREATE_EXE, "ZuiPlatform", project.string(), "--portable"), work / "create.log");
    ASSERT_EQ(created.code, 0) << created.text;
    const Captured unknown = run_captured(
        cmd(ZUI_PACK_EXE, "--platform", "netbsd", project.string()), work / "unknown.log");
    EXPECT_NE(unknown.code, 0) << unknown.text;
    EXPECT_NE(unknown.text.find("is not one of windows, linux, macos, android, ios"),
              std::string::npos)
        << unknown.text;
    for (const char* platform : {"linux", "macos", "ios"}) {
        const Captured gated = run_captured(
            cmd(ZUI_PACK_EXE, "--platform", platform, project.string(), "--root", ZUI_ROOT_DIR,
                "--build", ZUI_BUILD_DIR),
            work / (std::string(platform) + ".log"));
        EXPECT_NE(gated.code, 0) << platform << ": " << gated.text;
        EXPECT_NE(gated.text.find("needs a"), std::string::npos) << platform << ": " << gated.text;
        EXPECT_EQ(gated.text.find("warning: the build is Debug"), std::string::npos)
            << platform << " should stop before it builds anything: " << gated.text;
    }
}

// Linux desktop text, the .deb control file, and the Android manifest are the
// strings the packagers write. This host has no foreign SDK, so the test reads
// a project zui_create just wrote and asks those same functions for the text.
TEST(PackCli, ForeignLayoutsUseTheProject) {
    const fs::path work = fresh_dir("foreign-layout");
    const fs::path project = work / "app";
    const Captured created = run_captured(
        cmd(ZUI_CREATE_EXE, "ZuiForeign", project.string(), "--version", "1.2.3", "--identifier",
            "com.example.zuiforeign", "--page", "ui/index.html", "--portable", "--android"),
        work / "create.log");
    ASSERT_EQ(created.code, 0) << created.text;

    zui_tool::Project decl;
    std::string error;
    ASSERT_TRUE(zui_tool::ProjectWriter::read(project / "zui.json", decl, error)) << error;
    EXPECT_EQ(decl.name, "ZuiForeign");
    EXPECT_EQ(decl.version, "1.2.3");
    EXPECT_EQ(decl.identifier, "com.example.zuiforeign");
    EXPECT_EQ(decl.page, "ui/index.html");

    const zui_tool::LinuxDesktop portable = zui_tool::linux_portable_desktop(decl, true);
    const std::string desktop = zui_tool::linux_desktop_entry(portable);
    EXPECT_EQ(portable.name, decl.name);
    EXPECT_EQ(portable.exec, "run.sh");
    EXPECT_EQ(portable.icon, "icon.png");
    EXPECT_NE(portable.exec, decl.name);
    EXPECT_NE(desktop.find("Name=" + decl.name + "\n"), std::string::npos) << desktop;
    EXPECT_NE(desktop.find("Exec=" + portable.exec + "\n"), std::string::npos) << desktop;
    EXPECT_NE(desktop.find("Icon=" + portable.icon + "\n"), std::string::npos) << desktop;
    EXPECT_EQ(desktop.find("Exec=" + decl.name + "\n"), std::string::npos) << desktop;
    EXPECT_EQ(desktop.find("Icon=" + decl.name + "\n"), std::string::npos) << desktop;
    EXPECT_NE(desktop.find("X-Zui-Identifier=" + decl.identifier), std::string::npos) << desktop;
    EXPECT_EQ(desktop.find("Name=OtherApp"), std::string::npos) << desktop;
    const zui_tool::LinuxDesktop bare = zui_tool::linux_portable_desktop(decl, false);
    const std::string bare_entry = zui_tool::linux_desktop_entry(bare);
    EXPECT_EQ(bare.exec, portable.exec);
    EXPECT_TRUE(bare.icon.empty());
    EXPECT_EQ(bare_entry.find("\nIcon="), std::string::npos) << bare_entry;

    const std::string package = zui_tool::linux_package_name(decl.name);
    EXPECT_EQ(package, "zuiforeign");
    EXPECT_NE(package, zui_tool::linux_package_name("OtherApp"));
    const zui_tool::LinuxDesktop deb = zui_tool::linux_deb_desktop(decl, true);
    const std::string deb_entry = zui_tool::linux_desktop_entry(deb);
    EXPECT_EQ(deb.name, decl.name);
    EXPECT_EQ(deb.exec, package);
    EXPECT_EQ(deb.icon, package);
    EXPECT_NE(deb_entry.find("Name=" + decl.name + "\n"), std::string::npos) << deb_entry;
    EXPECT_EQ(deb_entry.find("Name=" + package + "\n"), std::string::npos) << deb_entry;
    EXPECT_NE(deb_entry.find("Exec=" + package + "\n"), std::string::npos) << deb_entry;
    EXPECT_NE(deb_entry.find("Icon=" + package + "\n"), std::string::npos) << deb_entry;
    zui_tool::Project other = decl;
    other.name = "OtherApp";
    other.identifier = "com.example.other";
    const zui_tool::LinuxDesktop other_deb = zui_tool::linux_deb_desktop(other, true);
    EXPECT_EQ(other_deb.name, other.name);
    EXPECT_NE(other_deb.exec, deb.exec);
    EXPECT_NE(zui_tool::linux_desktop_entry(other_deb).find("X-Zui-Identifier=" + other.identifier),
              std::string::npos);
    const std::string control = zui_tool::linux_deb_control(
        package, decl.version, "amd64", decl.name + " <unknown@localhost>", decl.name,
        decl.identifier, 100);
    EXPECT_NE(control.find("Package: " + package), std::string::npos) << control;
    EXPECT_NE(control.find("Version: " + decl.version), std::string::npos) << control;
    EXPECT_NE(control.find("X-Zui-Identifier: " + decl.identifier), std::string::npos) << control;
    EXPECT_EQ(zui_tool::linux_portable_page(decl.name, decl.page), decl.name + "/" + decl.page);
    EXPECT_EQ(zui_tool::linux_deb_page(package, decl.page),
              "./usr/lib/" + package + "/" + decl.page);

    const fs::path page_path = project / fs::path(decl.page, fs::path::generic_format);
    const std::string page_text = read_text(page_path);
    ASSERT_FALSE(page_text.empty()) << page_path;
    const std::string page_member = zui_tool::linux_portable_page(decl.name, decl.page);
    zui_tool::TarWriter tar;
    tar.add(page_member, std::vector<std::uint8_t>(page_text.begin(), page_text.end()));
    tar.add_text(decl.name + ".desktop", desktop, 0644);
    const std::vector<std::uint8_t> tar_bytes = tar.finish();
    std::vector<std::pair<std::string, std::size_t>> members;
    ASSERT_TRUE(zui_tool::tar_list(tar_bytes.data(), tar_bytes.size(), members, error)) << error;
    bool saw_page = false;
    bool saw_desktop = false;
    for (const auto& member : members) {
        if (member.first == page_member) {
            saw_page = true;
            EXPECT_EQ(member.second, page_text.size()) << page_member;
        }
        if (member.first == decl.name + ".desktop") {
            saw_desktop = true;
            EXPECT_EQ(member.second, desktop.size());
        }
    }
    EXPECT_TRUE(saw_page) << page_member;
    EXPECT_TRUE(saw_desktop);

    int version_code = 0;
    ASSERT_TRUE(zui_tool::android_version_code(decl.version, version_code, error)) << error;
    const std::string manifest = zui_tool::android_manifest(decl, version_code, false);
    EXPECT_NE(manifest.find("package=\"" + decl.identifier + "\""), std::string::npos) << manifest;
    EXPECT_NE(manifest.find("android:versionName=\"" + decl.version + "\""), std::string::npos)
        << manifest;
    EXPECT_NE(manifest.find("android:versionCode=\"" + std::to_string(version_code) + "\""),
              std::string::npos)
        << manifest;
    EXPECT_NE(manifest.find("android:label=\"" + decl.name + "\""), std::string::npos) << manifest;
    EXPECT_EQ(manifest.find("package=\"com.example.other\""), std::string::npos) << manifest;
    EXPECT_EQ(zui_tool::android_asset_entry(decl.page), "assets/payload/" + decl.page);

    const Captured linux_pack = run_captured(
        cmd(ZUI_PACK_EXE, "--platform", "linux", project.string(), "--root", ZUI_ROOT_DIR,
            "--build", ZUI_BUILD_DIR),
        work / "linux.log");
    EXPECT_NE(linux_pack.code, 0) << linux_pack.text;
    EXPECT_NE(linux_pack.text.find("needs a"), std::string::npos) << linux_pack.text;
    const Captured android_pack = run_captured(
        cmd(ZUI_PACK_EXE, "--platform", "android", project.string(), "--root", ZUI_ROOT_DIR,
            "--build", ZUI_BUILD_DIR),
        work / "android.log");
    EXPECT_NE(android_pack.code, 0) << android_pack.text;
    const bool named = android_pack.text.find("missing Android SDK") != std::string::npos ||
                       android_pack.text.find("missing JDK") != std::string::npos ||
                       android_pack.text.find("missing Android NDK") != std::string::npos ||
                       android_pack.text.find("missing the android build of zui") !=
                           std::string::npos;
    EXPECT_TRUE(named) << android_pack.text;
    EXPECT_FALSE(fs::exists(project / "dist" / (decl.name + "-" + decl.version + ".apk")))
        << android_pack.text;
    std::error_code ec;
    if (fs::is_directory(project / "dist", ec)) {
        for (const auto& entry : fs::directory_iterator(project / "dist", ec)) {
            if (ec || !entry.is_regular_file()) {
                continue;
            }
            const std::string ext = entry.path().extension().string();
            EXPECT_NE(ext, ".deb") << entry.path();
            EXPECT_NE(ext, ".gz") << entry.path();
            EXPECT_NE(ext, ".apk") << entry.path();
        }
    }
}

// A bundle that names an icon it does not carry is a broken app, and the error
// belongs here rather than on a device.
TEST(PackCli, AppleBundleRejectsMissingIcon) {
    const fs::path work = fresh_dir("apple-icon");
    zui_tool::AppleBundleInfo info;
    info.name = "ZuiNoIcon";
    info.identifier = "com.example.zuinoicon";
    info.version = "0.1.0";
    info.executable = "ZuiNoIcon";
    info.minimum_system = "10.15";
    zui_tool::AppleBundle bundle(work, info);
    bundle.add_executable({0xcf, 0xfa, 0xed, 0xfe, 1});
    bundle.set_extra("CFBundleIconFile", "icon.icns");
    std::string error;
    EXPECT_FALSE(bundle.write(error));
    EXPECT_NE(error.find("icon.icns"), std::string::npos) << error;
    // With the icon in the bundle the same bundle writes.
    bundle.add_resource("icon.icns", {1, 2, 3, 4});
    EXPECT_TRUE(bundle.write(error)) << error;
}

TEST(PackCli, PeImportsRejectNonImage) {
    const fs::path work = fresh_dir("pe-imports-bad");
    const fs::path page = work / "index.html";
    {
        std::ofstream file(page);
        file << "<!DOCTYPE html>\n";
    }
    std::vector<std::string> names;
    std::string error;
    EXPECT_FALSE(zui_tool::read_pe_imports(page, names, error));
    EXPECT_FALSE(error.empty());
}

// The packages ship what the executable imports, not a list the tool keeps by
// hand. zui_hello links zui only, and zui.dll imports the six plugin DLLs
// source/ext/builtin.cpp registers, so that is the closure.
TEST(PackCli, PeImportsResolveClosure) {
    const fs::path exe = fs::path(ZUI_BUILD_DIR) / "zui_hello.exe";
    ASSERT_TRUE(fs::is_regular_file(exe)) << exe;
    std::vector<std::string> imports;
    std::string error;
    ASSERT_TRUE(zui_tool::read_pe_imports(exe, imports, error)) << error;
    std::vector<std::string> lowered;
    for (const std::string& name : imports) {
        lowered.push_back(lower_copy(name));
    }
    EXPECT_TRUE(contains(lowered, "zui.dll")) << "zui_hello.exe imports no zui.dll";
    EXPECT_TRUE(contains(lowered, "kernel32.dll")) << "zui_hello.exe imports no kernel32.dll";

    std::vector<fs::path> dlls;
    std::vector<std::string> external;
    ASSERT_TRUE(zui_tool::collect_local_dlls(
        exe, {exe.parent_path(), fs::path(ZUI_BUILD_DIR)}, dlls, external, error))
        << error;
    const std::vector<std::string> shipped = lower_names(dlls);
    for (const char* dll : kRuntimeDlls) {
        EXPECT_TRUE(contains(shipped, dll)) << dll << " is not in the closure";
    }
    EXPECT_EQ(shipped.size(), std::size(kRuntimeDlls)) << "the closure holds too many libraries";
    for (const std::string& name : external) {
        EXPECT_FALSE(zui_tool::dll_name_is_zui(name)) << name;
        std::error_code ec;
        EXPECT_FALSE(fs::is_regular_file(fs::path(ZUI_BUILD_DIR) / name, ec))
            << name << " is in the build and was reported as external";
    }
}
