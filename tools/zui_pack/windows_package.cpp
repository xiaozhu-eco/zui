// zui-pack: build a zui_create project and emit the packages named in zui.json.
//   zui-pack <project-dir> [--root DIR] [--build DIR]
//   zui-pack android <project-dir>
// Desktop packaging writes dist/<name>-portable.zip and dist/<name>-setup.exe.
// The Android invocation is package_android.

#include "android.h"
#include "packer.h"
#include "project_files.h"
#include "style.h"
#include "icon.h"
#include "project_files.h"
#include "pe.h"
#include "subprocess.h"
#include "sign.h"
#include "style.h"
#include "zip.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

std::string env_or_empty(const char* key) {
    const char* value = std::getenv(key);
    return value == nullptr ? std::string() : std::string(value);
}

std::filesystem::path this_executable() {
#if defined(_WIN32)
    wchar_t buf[32768];
    const DWORD n = GetModuleFileNameW(nullptr, buf, 32768);
    if (n == 0 || n >= 32768) {
        return {};
    }
    return std::filesystem::path(buf);
#else
    return {};
#endif
}

std::string cache_value(const std::filesystem::path& cache, const std::string& key) {
    std::ifstream in(cache);
    if (!in) {
        return {};
    }
    const std::string prefix = key + ":";
    std::string found;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.compare(0, prefix.size(), prefix) != 0) {
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string value = line.substr(eq + 1);
        if (!value.empty()) {
            found = value;
        }
    }
    return found;
}

void report_missing_from(const char* source, const std::vector<std::string>& names) {
    if (names.empty()) {
        return;
    }
    std::cout << "zui-pack: not shipped, expected from " << source << ":";
    for (const std::string& name : names) {
        std::cout << " " << name;
    }
    std::cout << std::endl;
}

// Libraries the package does not ship: they come from Windows or from a
// redistributable installed on the machine that runs the package.
void report_external_dlls(const std::vector<std::string>& external) {
    std::vector<std::string> system;
    std::vector<std::string> redistributable;
    for (const std::string& name : external) {
        if (zui_tool::dll_name_is_redistributable(name)) {
            redistributable.push_back(name);
        } else {
            system.push_back(name);
        }
    }
    report_missing_from("Windows", system);
    report_missing_from("the Visual C++ redistributable", redistributable);
}

// Package file name stem: product and version, so a new build does not
// overwrite the packages of the last one.
std::string artifact_stem(const zui_tool::Project& decl) {
    return decl.name + "-" + decl.version;
}

// What the executable's version resource says, and how the package is signed.
zui_tool::ExeMetadata exe_metadata(const zui_tool::Project& decl, const std::string& file_name) {
    zui_tool::ExeMetadata metadata;
    metadata.product = decl.name;
    metadata.description = decl.windows.description;
    metadata.company = decl.windows.company;
    metadata.copyright = decl.windows.copyright;
    metadata.version = decl.version;
    metadata.original_filename = file_name;
    return metadata;
}

zui_tool::SigningOptions signing_options(const std::filesystem::path& project,
                                        const zui_tool::Project& decl) {
    zui_tool::SigningOptions options;
    options.project = project;
    options.certificate = decl.windows.certificate;
    options.timestamp_url = decl.windows.timestamp_url;
    return options;
}

bool add_cache_arg(std::vector<std::string>& args, const std::filesystem::path& cache,
                   const char* cmake_key, const char* cache_key) {
    const std::string value = cache_value(cache, cache_key);
    if (value.empty()) {
        return false;
    }
    args.push_back(std::string("-D") + cmake_key + "=" + value);
    return true;
}

std::filesystem::path find_built_exe(const std::filesystem::path& build, const std::string& name) {
#if defined(_WIN32)
    const std::string file = name + ".exe";
#else
    const std::string file = name;
#endif
    const std::filesystem::path candidates[] = {
        build / file,
        build / "Debug" / file,
        build / "Release" / file,
        build / "RelWithDebInfo" / file,
        build / "MinSizeRel" / file,
    };
    for (const std::filesystem::path& candidate : candidates) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(candidate, ec)) {
            return candidate;
        }
    }
    // A zui build keeps each example in its own directory: build/reader/…. One
    // level down is where a target named by zui.json lands.
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(build, ec)) {
        if (ec || !entry.is_directory()) {
            continue;
        }
        const std::filesystem::path candidate = entry.path() / file;
        std::error_code file_ec;
        if (std::filesystem::is_regular_file(candidate, file_ec)) {
            return candidate;
        }
    }
    return {};
}

bool load_entry(std::vector<zui_tool::ZipEntry>& entries, const std::filesystem::path& path,
                const std::string& name, std::string& error) {
    zui_tool::ZipEntry entry;
    entry.name = name;
    if (!zui_tool::read_file_bytes(path, entry.bytes, error)) {
        return false;
    }
    entries.push_back(std::move(entry));
    return true;
}

void put_le64(std::uint8_t* dest, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        dest[i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xff);
    }
}

std::uint16_t read_le16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

std::uint32_t read_le32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

void put_le32_at(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint32_t value) {
    bytes[at] = static_cast<std::uint8_t>(value & 0xff);
    bytes[at + 1] = static_cast<std::uint8_t>((value >> 8) & 0xff);
    bytes[at + 2] = static_cast<std::uint8_t>((value >> 16) & 0xff);
    bytes[at + 3] = static_cast<std::uint8_t>((value >> 24) & 0xff);
}

// Puts the payload inside the executable's last section. A signing tool refuses
// a Windows image with bytes after the image that its headers do not describe,
// so the payload has to look like part of the image instead of trailing data.
bool embed_payload(std::vector<std::uint8_t>& image, const std::vector<std::uint8_t>& payload,
                   std::string& error) {
    if (image.size() < 0x40 || image[0] != 'M' || image[1] != 'Z') {
        error = "the installer stub is not a PE image";
        return false;
    }
    const std::size_t pe = read_le32(&image[0x3c]);
    if (pe + 24 > image.size()) {
        error = "the installer stub has a broken PE header";
        return false;
    }
    const std::uint16_t section_count = read_le16(&image[pe + 6]);
    const std::uint16_t optional_size = read_le16(&image[pe + 20]);
    const std::size_t optional = pe + 24;
    if (optional_size < 96 || optional + optional_size > image.size()) {
        error = "the installer stub has a broken optional header";
        return false;
    }
    const std::uint16_t magic = read_le16(&image[optional]);
    if (magic != 0x10b && magic != 0x20b) {
        error = "the installer stub is not a PE32 or PE32+ image";
        return false;
    }
    const std::size_t directories = optional + (magic == 0x20b ? 112 : 96);
    if (directories + 8 * 5 <= optional + optional_size &&
        read_le32(&image[directories + 4 * 8 + 4]) != 0) {
        // A certificate table already sits after the image. Growing the last
        // section over it would corrupt both the signature and the payload.
        error = "the installer stub is already signed; the payload has to go in first";
        return false;
    }
    const std::uint32_t section_alignment = read_le32(&image[optional + 32]);
    const std::uint32_t file_alignment = read_le32(&image[optional + 36]);
    const std::uint64_t virtual_align = section_alignment == 0 ? 0x1000 : section_alignment;
    const std::uint64_t file_align = file_alignment == 0 ? 0x200 : file_alignment;
    std::size_t last = 0;
    std::uint64_t image_end = 0;
    for (std::uint16_t i = 0; i < section_count; ++i) {
        const std::size_t header = optional + optional_size + static_cast<std::size_t>(i) * 40;
        if (header + 40 > image.size()) {
            error = "the installer stub has a broken section table";
            return false;
        }
        const std::uint64_t end = static_cast<std::uint64_t>(read_le32(&image[header + 20])) +
                                  read_le32(&image[header + 16]);
        if (last == 0 || end >= image_end) {
            image_end = end;
            last = header;
        }
    }
    if (last == 0 || image_end > image.size()) {
        error = "the installer stub's section table does not describe the file";
        return false;
    }
    // Whatever sits after the last section (alignment padding) belongs to that
    // section once it grows, so the payload lands at the end of the file and the
    // section covers everything between.
    const std::uint64_t added = static_cast<std::uint64_t>(image.size()) - image_end +
                                static_cast<std::uint64_t>(payload.size());
    const std::uint64_t raw_growth = ((added + file_align - 1) / file_align) * file_align;
    const std::uint64_t virtual_growth = ((added + virtual_align - 1) / virtual_align) * virtual_align;
    put_le32_at(image, last + 16,
                read_le32(&image[last + 16]) + static_cast<std::uint32_t>(raw_growth));
    put_le32_at(image, last + 8,
                read_le32(&image[last + 8]) + static_cast<std::uint32_t>(virtual_growth));
    put_le32_at(image, optional + 56,
                read_le32(&image[optional + 56]) + static_cast<std::uint32_t>(virtual_growth));
    put_le32_at(image, optional + 64, 0);  // the checksum belongs to the signer
    const std::size_t start = image.size();
    image.insert(image.end(), payload.begin(), payload.end());
    image.resize(start + static_cast<std::size_t>(raw_growth), 0);
    return true;
}

bool write_setup(const std::filesystem::path& stub, const std::filesystem::path& payload,
                 const std::filesystem::path& dest, std::string& error) {
    std::vector<std::uint8_t> stub_bytes;
    std::vector<std::uint8_t> zip_bytes;
    if (!zui_tool::read_file_bytes(stub, stub_bytes, error) ||
        !zui_tool::read_file_bytes(payload, zip_bytes, error)) {
        return false;
    }
    // The 16-byte trailer tells the installer where its payload starts. Payload
    // and trailer both go inside the image.
    std::vector<std::uint8_t> body = zip_bytes;
    std::uint8_t tail[16];
    put_le64(tail, zip_bytes.size());
    std::memcpy(tail + 8, "ZUIINST1", 8);
    body.insert(body.end(), tail, tail + 16);
    if (!embed_payload(stub_bytes, body, error)) {
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(dest.parent_path(), ec);
    const std::filesystem::path tmp = dest.string() + ".tmp";
    std::filesystem::remove(tmp, ec);
    std::ofstream out(tmp, std::ios::binary);
    if (!out) {
        error = "cannot write " + dest.string();
        return false;
    }
    out.write(reinterpret_cast<const char*>(stub_bytes.data()),
              static_cast<std::streamsize>(stub_bytes.size()));
    out.close();
    if (!out) {
        error = "cannot write " + dest.string();
        std::filesystem::remove(tmp, ec);
        return false;
    }
    std::filesystem::remove(dest, ec);
    std::filesystem::rename(tmp, dest, ec);
    if (ec) {
        error = "cannot write " + dest.string();
        return false;
    }
    return true;
}

bool replace_zip(const std::filesystem::path& dest, const std::vector<zui_tool::ZipEntry>& entries,
                 std::string& error) {
    const std::filesystem::path tmp = dest.string() + ".tmp";
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    if (!zui_tool::write_zip(tmp, entries, error)) {
        return false;
    }
    std::filesystem::remove(dest, ec);
    std::filesystem::rename(tmp, dest, ec);
    if (ec) {
        error = "cannot write " + dest.string();
        return false;
    }
    return true;
}

// The Windows desktop flow: a portable ZIP and a per-user installer. The
// executable carries its icon and version resource, every library it imports
// travels with it, and the installer's payload lives inside its image so a
// signature can cover it.
int run_windows_package(const zui_tool::PackContext& ctx) {
    std::cout << "zui-pack: desktop package " << ctx.project.string() << std::endl;
    std::error_code ec;
    if (!std::filesystem::is_directory(ctx.project, ec)) {
        std::cerr << "zui-pack: project directory does not exist\n";
        return 1;
    }
    const bool portable = zui_tool::has_target(ctx.decl, "portable");
    const bool installer = zui_tool::has_target(ctx.decl, "installer");
    if (!portable && !installer) {
        std::cerr << "zui-pack: zui.json selects neither portable nor installer\n";
        return 1;
    }
    const zui_tool::Project& decl = ctx.decl;
    const std::string stem = artifact_stem(decl);
    const std::filesystem::path dist = ctx.dist();
    if (portable) {
        std::filesystem::remove(dist / (stem + "-portable.zip"), ec);
    }
    if (installer) {
        std::filesystem::remove(dist / (stem + "-setup.exe"), ec);
    }
    if (ctx.root.empty() || ctx.build.empty()) {
        std::cerr << "zui-pack: set --root and --build, or ZUI_ROOT and ZUI_BUILD\n";
        return 1;
    }
    if (!std::filesystem::is_directory(ctx.root, ec) ||
        !std::filesystem::is_directory(ctx.build, ec)) {
        std::cerr << "zui-pack: zui root or build directory does not exist\n";
        return 1;
    }
    const std::filesystem::path cache = ctx.build / "CMakeCache.txt";
    const std::string generator = cache_value(cache, "CMAKE_GENERATOR");
    if (generator.empty()) {
        std::cerr << "zui-pack: " << cache.string() << " has no CMAKE_GENERATOR\n";
        return 1;
    }
#if defined(_WIN32)
    if (!std::filesystem::is_regular_file(ctx.build / "zui.lib", ec)) {
        std::cerr << "zui-pack: " << (ctx.build / "zui.lib").string()
                  << " is missing; build the zui target first\n";
        return 1;
    }
#endif
    std::string config = cache_value(cache, "CMAKE_BUILD_TYPE");
    if (config.empty()) {
        config = "Debug";
    }
    if (config == "Debug") {
        std::cerr << zui_tool::style_yellow("zui-pack: warning: the build is Debug; the package "
                                            "needs the debug runtime and is not fit to ship")
                  << "\n";
    }
    const bool multi = generator.find("Visual Studio") != std::string::npos ||
                       generator.find("Xcode") != std::string::npos;
    const std::filesystem::path app_build = ctx.project / ".zui" / "build";
    const std::string cmake = zui_tool::cmake_program(ctx.build);
    std::filesystem::path exe;
    std::string error;
    if (decl.target.empty()) {
        std::vector<std::string> configure =
            zui_tool::command(cmake, "-S", ctx.project.string(), "-B", app_build.string(), "-G",
                              generator, "-DZUI_ROOT=" + ctx.root.string(),
                              "-DZUI_BUILD=" + ctx.build.string());
        const std::string platform = cache_value(cache, "CMAKE_GENERATOR_PLATFORM");
        if (!platform.empty()) {
            configure.push_back("-A");
            configure.push_back(platform);
        }
        const std::string toolset = cache_value(cache, "CMAKE_GENERATOR_TOOLSET");
        if (!toolset.empty()) {
            configure.push_back("-T");
            configure.push_back(toolset);
        }
        if (!multi) {
            configure.push_back("-DCMAKE_BUILD_TYPE=" + config);
        }
        add_cache_arg(configure, cache, "CMAKE_C_COMPILER", "CMAKE_C_COMPILER");
        add_cache_arg(configure, cache, "CMAKE_CXX_COMPILER", "CMAKE_CXX_COMPILER");
        add_cache_arg(configure, cache, "CMAKE_LINKER", "CMAKE_LINKER");
        add_cache_arg(configure, cache, "CMAKE_AR", "CMAKE_AR");
        add_cache_arg(configure, cache, "CMAKE_RC_COMPILER", "CMAKE_RC_COMPILER");
        add_cache_arg(configure, cache, "CMAKE_MT", "CMAKE_MT");
        add_cache_arg(configure, cache, "CMAKE_MAKE_PROGRAM", "CMAKE_MAKE_PROGRAM");
        add_cache_arg(configure, cache, "CMAKE_MSVC_RUNTIME_LIBRARY", "CMAKE_MSVC_RUNTIME_LIBRARY");
        add_cache_arg(configure, cache, "CMAKE_SYSTEM_VERSION", "CMAKE_SYSTEM_VERSION");
        if (zui_tool::run_msvc_process(configure, ctx.project / ".zui") != 0) {
            std::cerr << "zui-pack: configure failed\n";
            return 1;
        }
        std::vector<std::string> build =
            zui_tool::command(cmake, "--build", app_build.string());
        if (multi) {
            build.push_back("--config");
            build.push_back(config);
        }
        if (zui_tool::run_msvc_process(build, ctx.project / ".zui") != 0) {
            std::cerr << "zui-pack: build failed\n";
            return 1;
        }
        exe = find_built_exe(app_build, decl.name);
    } else {
        std::vector<std::string> build =
            zui_tool::command(cmake, "--build", ctx.build.string(), "--target", decl.target);
        if (multi) {
            build.push_back("--config");
            build.push_back(config);
        }
        if (zui_tool::run_msvc_process(build, ctx.project / ".zui") != 0) {
            std::cerr << "zui-pack: build failed\n";
            return 1;
        }
        exe = find_built_exe(ctx.build, decl.target);
    }
    if (exe.empty()) {
        std::cerr << "zui-pack: built executable was not found\n";
        return 1;
    }
    const std::string page_rel = decl.page;
    const std::filesystem::path page =
        ctx.project / std::filesystem::path(page_rel, std::filesystem::path::generic_format);
    std::vector<zui_tool::ZipEntry> entries;
#if defined(_WIN32)
    const std::string exe_name = decl.name + ".exe";
#else
    const std::string exe_name = decl.name;
#endif
    if (!load_entry(entries, exe, exe_name, error)) {
        std::cerr << "zui-pack: " << error << "\n";
        return 1;
    }
    const std::size_t exe_index = entries.size() - 1;
    if (!load_entry(entries, page, page_rel, error)) {
        std::cerr << "zui-pack: " << error << "\n";
        return 1;
    }
    const std::size_t page_index = entries.size() - 1;
    const zui_tool::SigningOptions signing = signing_options(ctx.project, decl);
#if defined(_WIN32)
    if (!zui_tool::stamp_exe_bytes(entries[exe_index].bytes, ctx.project, ctx.root, decl.icon,
                                   exe_metadata(decl, exe_name), error)) {
        std::cerr << "zui-pack: " << error << "\n";
        return 1;
    }
    // Stamping runs first: the signature covers the version resource.
    if (zui_tool::signing_requested(signing) &&
        !zui_tool::sign_bytes(signing, entries[exe_index].bytes, exe_name, error)) {
        std::cerr << "zui-pack: " << error << "\n";
        return 1;
    }
#endif
    if (entries[page_index].bytes.size() < 8) {
        std::cerr << "zui-pack: the page in the project is empty\n";
        return 1;
    }
#if defined(_WIN32)
    // Ship the libraries the executable imports, transitively. A fixed list goes
    // stale as soon as a plugin joins the build.
    std::vector<std::filesystem::path> runtime_dlls;
    std::vector<std::string> external_dlls;
    const std::vector<std::filesystem::path> library_dirs = {exe.parent_path(), ctx.build};
    if (!zui_tool::collect_local_dlls(exe, library_dirs, runtime_dlls, external_dlls, error)) {
        std::cerr << "zui-pack: " << error << "\n";
        return 1;
    }
    for (const std::filesystem::path& dll : runtime_dlls) {
        if (!load_entry(entries, dll, dll.filename().string(), error)) {
            std::cerr << "zui-pack: " << error << "\n";
            return 1;
        }
    }
    report_external_dlls(external_dlls);
#endif
    std::vector<std::pair<std::string, std::filesystem::path>> runtime_files;
    if (!zui_tool::ProjectWriter::collect_runtime_files(ctx.project, decl, runtime_files, error)) {
        std::cerr << "zui-pack: " << error << "\n";
        return 1;
    }
    for (const auto& runtime_file : runtime_files) {
        if (runtime_file.first == page_rel || runtime_file.first == exe_name) {
            continue;
        }
        bool already = false;
        for (const zui_tool::ZipEntry& have : entries) {
            if (have.name == runtime_file.first) {
                already = true;
                break;
            }
        }
        if (already) {
            continue;
        }
        if (!load_entry(entries, runtime_file.second, runtime_file.first, error)) {
            std::cerr << "zui-pack: " << error << "\n";
            return 1;
        }
    }
    std::filesystem::create_directories(dist, ec);
    if (portable) {
        const std::filesystem::path zip = dist / (stem + "-portable.zip");
        if (!replace_zip(zip, entries, error)) {
            std::cerr << "zui-pack: " << error << "\n";
            return 1;
        }
        std::cout << zui_tool::style_green(zui_tool::mark_ok()) << " portable " << zip.string()
                  << std::endl;
    }
    if (installer) {
#if !defined(_WIN32)
        std::cerr << "zui-pack: the desktop installer is produced on Windows\n";
        return 1;
#else
        const std::filesystem::path stub = this_executable().parent_path() / "zui_setup_stub.exe";
        if (!std::filesystem::is_regular_file(stub, ec)) {
            std::cerr << "zui-pack: missing installer stub " << stub.string() << "\n";
            return 1;
        }
        zui_tool::ZipEntry product;
        product.name = "product.txt";
        const std::string product_text = decl.name + "\n" + decl.version + "\n";
        product.bytes.assign(product_text.begin(), product_text.end());
        std::vector<std::uint8_t> stub_bytes;
        if (!zui_tool::read_file_bytes(stub, stub_bytes, error) ||
            !zui_tool::stamp_exe_bytes(stub_bytes, ctx.project, ctx.root, decl.icon,
                                       exe_metadata(decl, "Uninstall.exe"), error)) {
            std::cerr << "zui-pack: " << error << "\n";
            return 1;
        }
        // Uninstall.exe is the same program without a payload, so it is signed
        // on its own; the installer image stays unsigned until its payload is
        // inside it, because the signature has to cover that too.
        std::vector<std::uint8_t> uninstaller_bytes = stub_bytes;
        if (zui_tool::signing_requested(signing) &&
            !zui_tool::sign_bytes(signing, uninstaller_bytes, "Uninstall.exe", error)) {
            std::cerr << "zui-pack: " << error << "\n";
            return 1;
        }
        const std::filesystem::path stamped_stub = ctx.project / ".zui" / "setup-stub.exe";
        std::filesystem::create_directories(stamped_stub.parent_path(), ec);
        {
            std::ofstream stub_out(stamped_stub, std::ios::binary);
            stub_out.write(reinterpret_cast<const char*>(stub_bytes.data()),
                           static_cast<std::streamsize>(stub_bytes.size()));
            if (!stub_out) {
                std::cerr << "zui-pack: cannot write the installer stub\n";
                return 1;
            }
        }
        zui_tool::ZipEntry uninstaller;
        uninstaller.name = "Uninstall.exe";
        uninstaller.bytes = std::move(uninstaller_bytes);
        std::vector<zui_tool::ZipEntry> payload = entries;
        payload.push_back(std::move(product));
        payload.push_back(std::move(uninstaller));
        const std::filesystem::path payload_zip = ctx.project / ".zui" / "payload.zip";
        if (!zui_tool::write_zip(payload_zip, payload, error)) {
            std::cerr << "zui-pack: " << error << "\n";
            return 1;
        }
        const std::filesystem::path setup = dist / (stem + "-setup.exe");
        if (!write_setup(stamped_stub, payload_zip, setup, error)) {
            std::cerr << "zui-pack: " << error << "\n";
            return 1;
        }
        if (zui_tool::signing_requested(signing) && !zui_tool::sign_file(signing, setup, error)) {
            std::cerr << "zui-pack: " << error << "\n";
            return 1;
        }
        std::cout << zui_tool::style_green(zui_tool::mark_ok()) << " installer " << setup.string()
                  << std::endl;
#endif
    }
    if (!zui_tool::signing_requested(signing)) {
        std::cout << "zui-pack: did not sign (set windows.certificate in zui.json)" << std::endl;
    }
    return 0;
}

}  // namespace

namespace zui_tool {

class WindowsPacker final : public Packer {
public:
    const char* platform() const override { return "windows"; }

    std::string missing_host_tool(const PackContext& ctx) const override {
#if defined(_WIN32)
        (void)ctx;
        return {};
#else
        (void)ctx;
        return "a windows package (the installer, and an executable with a version resource) "
               "needs a Windows host";
#endif
    }

    int pack(const PackContext& ctx) override { return run_windows_package(ctx); }
};

std::unique_ptr<Packer> make_windows_packer() { return std::make_unique<WindowsPacker>(); }

}  // namespace zui_tool

