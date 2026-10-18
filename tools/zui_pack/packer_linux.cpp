// The Linux packager: a portable tar.gz and a .deb. Both carry the program, the
// page, the declared runtime files, and the engine libraries, plus a launcher
// that puts those libraries on the loader's path (a Linux binary does not look
// next to itself the way a Windows one does).

#include "archive.h"
#include "elf.h"
#include "md5.h"
#include "package_text.h"
#include "packer.h"
#include "project_files.h"
#include "style.h"
#include "subprocess.h"
#include "zip.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <set>

namespace zui_tool {
namespace {

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
        if (line.compare(0, prefix.size(), prefix) == 0) {
            const auto eq = line.find('=');
            if (eq != std::string::npos) {
                found = line.substr(eq + 1);
            }
        }
    }
    return found;
}

/// A program built by the project's own CMake, or a target of the zui build.
/// The same rule the Windows packager follows.
bool build_linux_app(const PackContext& ctx, std::filesystem::path& exe, std::string& error) {
    const std::filesystem::path cache = ctx.build / "CMakeCache.txt";
    const std::string generator = cache_value(cache, "CMAKE_GENERATOR");
    if (generator.empty()) {
        error = cache.string() + " has no CMAKE_GENERATOR";
        return false;
    }
    std::string config = cache_value(cache, "CMAKE_BUILD_TYPE");
    if (config.empty()) {
        config = "Release";
    }
    const bool multi = generator.find("Xcode") != std::string::npos ||
                       generator.find("Visual Studio") != std::string::npos;
    const std::filesystem::path app_build = ctx.project / ".zui" / "build";
    std::vector<std::string> steps;
    std::filesystem::path search;
    if (ctx.decl.target.empty()) {
        const std::string cmake = cmake_program(ctx.build);
        std::vector<std::string> configure =
            command(cmake, "-S", ctx.project.string(), "-B", app_build.string(), "-G", generator,
                    "-DZUI_ROOT=" + ctx.root.string(), "-DZUI_BUILD=" + ctx.build.string());
        if (!multi) {
            configure.push_back("-DCMAKE_BUILD_TYPE=" + config);
        }
        if (run_process(configure) != 0) {
            error = "configure failed";
            return false;
        }
        steps = command(cmake, "--build", app_build.string());
        search = app_build;
    } else {
        steps = command(cmake_program(ctx.build), "--build", ctx.build.string(), "--target",
                        ctx.decl.target);
        search = ctx.build;
    }
    if (multi) {
        steps.push_back("--config");
        steps.push_back(config);
    }
    if (run_process(steps) != 0) {
        error = "build failed";
        return false;
    }
    const std::string want = ctx.decl.target.empty() ? ctx.decl.name : ctx.decl.target;
    std::error_code ec;
    const std::filesystem::path direct = search / want;
    if (std::filesystem::is_regular_file(direct, ec)) {
        exe = direct;
        return true;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(search, ec)) {
        if (ec) {
            break;
        }
        if (entry.is_regular_file() && entry.path().filename() == want) {
            exe = entry.path();
            return true;
        }
    }
    error = "the built program " + want + " was not found under " + search.string();
    return false;
}

/// Every shared library the program could need: the engine and its plugins sit
/// together in the build tree, and a Linux loader finds them through the
/// launcher's LD_LIBRARY_PATH rather than next to the binary.
void collect_shared_libraries(const std::filesystem::path& exe_dir,
                              const std::filesystem::path& build,
                              std::vector<std::filesystem::path>& out) {
    const std::filesystem::path dirs[] = {exe_dir, build, build / "lib"};
    std::set<std::string> seen;
    std::error_code ec;
    for (const std::filesystem::path& dir : dirs) {
        if (!std::filesystem::is_directory(dir, ec)) {
            continue;
        }
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (ec || !entry.is_regular_file()) {
                continue;
            }
            const std::string name = entry.path().filename().string();
            const bool shared = name.find(".so") != std::string::npos;
            if (!shared || !seen.insert(name).second) {
                continue;
            }
            out.push_back(entry.path());
        }
    }
}

std::string launcher_script(const std::string& app_dir, const std::string& executable) {
    return "#!/bin/sh\n"
           "# Start the application with its libraries on the loader's path.\n"
           "app=\"$(dirname \"$(readlink -f \"$0\")\")/../" +
           app_dir.substr(app_dir.find_last_of('/') + 1) + "/" + executable + "\"\n"
           "app=\"$(readlink -f \"$app\")\"\n"
           "dir=\"$(dirname \"$app\")\"\n"
           "export LD_LIBRARY_PATH=\"$dir/lib:$dir${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}\"\n"
           "exec \"$app\" \"$@\"\n";
}

/// The icon, when the project already ships a PNG: nothing on this host can turn
/// an SVG into a bitmap.
bool find_png_icon(const PackContext& ctx, std::filesystem::path& out) {
    const std::filesystem::path declared =
        ctx.project / std::filesystem::path(ctx.decl.icon, std::filesystem::path::generic_format);
    std::error_code ec;
    if (declared.extension() == ".png" && std::filesystem::is_regular_file(declared, ec)) {
        out = declared;
        return true;
    }
    const std::filesystem::path beside = ctx.project / "icon.png";
    if (std::filesystem::is_regular_file(beside, ec)) {
        out = beside;
        return true;
    }
    return false;
}

bool add_payload(TarWriter& tar, const PackContext& ctx, const std::string& prefix,
                 const std::vector<std::pair<std::string, std::filesystem::path>>& files,
                 std::string& error) {
    const std::filesystem::path page =
        ctx.project / std::filesystem::path(ctx.decl.page, std::filesystem::path::generic_format);
    std::vector<std::uint8_t> bytes;
    if (!read_file_bytes(page, bytes, error)) {
        return false;
    }
    tar.add(linux_portable_page(ctx.decl.name, ctx.decl.page), bytes);
    for (const auto& file : files) {
        if (file.first == ctx.decl.page) {
            continue;
        }
        std::vector<std::uint8_t> content;
        if (!read_file_bytes(file.second, content, error)) {
            return false;
        }
        tar.add(prefix + file.first, content);
    }
    return true;
}

}  // namespace

class LinuxPacker final : public Packer {
public:
    const char* platform() const override { return "linux"; }

    std::string missing_host_tool(const PackContext& ctx) const override {
#if defined(__linux__)
        (void)ctx;
        return {};
#else
        (void)ctx;
        return "a linux package needs a linux host: the program has to be an ELF image, and the "
               ".deb is assembled from it with its libraries";
#endif
    }

    int pack(const PackContext& ctx) override {
        const bool portable = has_target(ctx.decl, "portable");
        const bool deb = has_target(ctx.decl, "installer");
        if (!portable && !deb) {
            std::cerr << "zui-pack: zui.json selects neither portable nor installer\n";
            return 1;
        }
        std::string error;
        std::filesystem::path exe;
        if (!build_linux_app(ctx, exe, error)) {
            std::cerr << "zui-pack: " << error << "\n";
            return 1;
        }
        if (!is_elf(exe)) {
            std::cerr << "zui-pack: " << exe.string() << " is not an ELF image\n";
            return 1;
        }
        const std::string arch = dpkg_architecture_of(exe);
        if (arch.empty()) {
            std::cerr << "zui-pack: the architecture of " << exe.filename().string()
                      << " is not one dpkg names\n";
            return 1;
        }
        std::vector<std::pair<std::string, std::filesystem::path>> files;
        if (!ProjectWriter::collect_runtime_files(ctx.project, ctx.decl, files, error)) {
            std::cerr << "zui-pack: " << error << "\n";
            return 1;
        }
        std::vector<std::filesystem::path> libraries;
        collect_shared_libraries(exe.parent_path(), ctx.build, libraries);
        std::filesystem::path icon;
        const bool have_icon = find_png_icon(ctx, icon);
        if (!have_icon) {
            std::cout << warning_prefix()
                      << "the project has no icon.png; the package ships without an icon"
                      << std::endl;
        }
        std::error_code ec;
        std::filesystem::create_directories(ctx.dist(), ec);
        const std::string stem = ctx.stem();
        if (portable) {
            const std::filesystem::path target =
                ctx.dist() / (stem + "-linux-" + arch + ".tar.gz");
            if (!write_portable(ctx, exe, files, libraries, icon, have_icon, target, error)) {
                std::cerr << "zui-pack: " << error << "\n";
                return 1;
            }
            std::cout << style_green(mark_ok()) << " portable " << target.string() << std::endl;
        }
        if (deb) {
            const std::filesystem::path target =
                ctx.dist() / (linux_package_name(ctx.decl.name) + "_" + ctx.decl.version + "_" +
                              arch + ".deb");
            if (!write_deb(ctx, exe, files, libraries, icon, have_icon, arch, target, error)) {
                std::cerr << "zui-pack: " << error << "\n";
                return 1;
            }
            std::cout << style_green(mark_ok()) << " deb " << target.string() << std::endl;
        }
        return 0;
    }

private:
    static std::string warning_prefix() { return style_yellow("zui-pack: warning: "); }

    bool write_portable(const PackContext& ctx, const std::filesystem::path& exe,
                        const std::vector<std::pair<std::string, std::filesystem::path>>& files,
                        const std::vector<std::filesystem::path>& libraries,
                        const std::filesystem::path& icon, bool have_icon,
                        const std::filesystem::path& target, std::string& error) {
        TarWriter tar;
        const LinuxDesktop desktop = linux_portable_desktop(ctx.decl, have_icon);
        const std::string root = ctx.decl.name + "/";
        tar.add_directory(root);
        std::vector<std::uint8_t> bytes;
        if (!read_file_bytes(exe, bytes, error)) {
            return false;
        }
        tar.add(root + exe.filename().string(), bytes, 0755);
        if (!add_payload(tar, ctx, root, files, error)) {
            return false;
        }
        for (const std::filesystem::path& library : libraries) {
            if (!read_file_bytes(library, bytes, error)) {
                return false;
            }
            tar.add(root + "lib/" + library.filename().string(), bytes, 0755);
        }
        if (have_icon && !read_file_bytes(icon, bytes, error)) {
            return false;
        }
        if (have_icon) {
            tar.add(root + desktop.icon, bytes);
        }
        const std::string launcher = "#!/bin/sh\n"
                                     "self=$(readlink -f \"$0\")\n"
                                     "here=$(dirname \"$self\")\n"
                                     "export LD_LIBRARY_PATH=\"$here/lib:$here"
                                     "${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}\"\n"
                                     "exec \"$here/" + exe.filename().string() + "\" \"$@\"\n";
        tar.add_text(root + desktop.exec, launcher, 0755);
        tar.add_text(root + ctx.decl.name + ".desktop", linux_desktop_entry(desktop), 0644);
        std::vector<std::uint8_t> gz;
        if (!gzip_bytes(tar.finish(), gz, error)) {
            return false;
        }
        std::ofstream out(target, std::ios::binary);
        out.write(reinterpret_cast<const char*>(gz.data()), static_cast<std::streamsize>(gz.size()));
        if (!out) {
            error = "cannot write " + target.string();
            return false;
        }
        return true;
    }

    bool write_deb(const PackContext& ctx, const std::filesystem::path& exe,
                   const std::vector<std::pair<std::string, std::filesystem::path>>& files,
                   const std::vector<std::filesystem::path>& libraries,
                   const std::filesystem::path& icon, bool have_icon, const std::string& arch,
                   const std::filesystem::path& target, std::string& error) {
        const LinuxDesktop desktop = linux_deb_desktop(ctx.decl, have_icon);
        const std::string name = desktop.exec;
        const std::string lib_dir = "usr/lib/" + name + "/";
        // The package's files, built once: the tar, md5sums, and the installed
        // size all read the same list.
        struct Member {
            std::string name;
            std::vector<std::uint8_t> bytes;
            std::uint32_t mode;
        };
        std::vector<Member> members;
        auto add_file = [&members](const std::string& path, std::vector<std::uint8_t> bytes,
                                   std::uint32_t mode) {
            members.push_back({path, std::move(bytes), mode});
        };
        TarWriter data;
        std::vector<std::uint8_t> bytes;
        if (!read_file_bytes(exe, bytes, error)) {
            return false;
        }
        add_file("./" + lib_dir + exe.filename().string(), bytes, 0755);
        const std::filesystem::path page =
            ctx.project /
            std::filesystem::path(ctx.decl.page, std::filesystem::path::generic_format);
        if (!read_file_bytes(page, bytes, error)) {
            return false;
        }
        add_file(linux_deb_page(name, ctx.decl.page), bytes, 0644);
        for (const auto& file : files) {
            if (file.first == ctx.decl.page) {
                continue;
            }
            if (!read_file_bytes(file.second, bytes, error)) {
                return false;
            }
            add_file("./" + lib_dir + file.first, bytes, 0644);
        }
        for (const std::filesystem::path& library : libraries) {
            if (!read_file_bytes(library, bytes, error)) {
                return false;
            }
            add_file("./" + lib_dir + "lib/" + library.filename().string(), bytes, 0755);
        }
        if (have_icon) {
            if (!read_file_bytes(icon, bytes, error)) {
                return false;
            }
            add_file("./usr/share/icons/hicolor/256x256/apps/" + desktop.icon + ".png", bytes, 0644);
        }
        const std::string launcher = launcher_script(lib_dir, exe.filename().string());
        add_file("./usr/bin/" + name, std::vector<std::uint8_t>(launcher.begin(), launcher.end()),
                 0755);
        const std::string entry = linux_desktop_entry(desktop);
        add_file("./usr/share/applications/" + name + ".desktop",
                 std::vector<std::uint8_t>(entry.begin(), entry.end()), 0644);
        std::uintmax_t installed = 0;
        std::vector<std::pair<std::string, std::string>> sums;
        for (const Member& member : members) {
            installed += member.bytes.size();
            // md5sums lists the path dpkg knows: no leading "./".
            sums.emplace_back(member.name.substr(2),
                              md5_hex(member.bytes.data(), member.bytes.size()));
            data.add(member.name, member.bytes, member.mode);
        }
        std::sort(sums.begin(), sums.end());
        std::string md5sums;
        for (const auto& sum : sums) {
            md5sums += sum.second + "  " + sum.first + "\n";
        }
        const std::vector<std::uint8_t> data_tar = data.finish();
        std::string maintainer = ctx.decl.windows.company;
        if (maintainer.empty()) {
            maintainer = ctx.decl.name + " <unknown@localhost>";
        }
        std::string description = ctx.decl.windows.description;
        if (description.empty()) {
            description = ctx.decl.name;
        }
        const std::string control =
            linux_deb_control(name, ctx.decl.version, arch, maintainer, description,
                              ctx.decl.identifier, installed);
        TarWriter control_tar;
        control_tar.add_text("./control", control, 0644);
        control_tar.add_text("./md5sums", md5sums, 0644);
        // dpkg runs this after unpacking: refresh the caches the package feeds.
        control_tar.add_text("./postinst",
                             "#!/bin/sh\n"
                             "set -e\n"
                             "if command -v update-desktop-database >/dev/null 2>&1; then\n"
                             "  update-desktop-database -q || true\n"
                             "fi\n"
                             "if command -v gtk-update-icon-cache >/dev/null 2>&1 && \\\n"
                             "   [ -d /usr/share/icons/hicolor ]; then\n"
                             "  gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor || true\n"
                             "fi\n"
                             "exit 0\n",
                             0755);
        std::vector<std::uint8_t> control_gz;
        std::vector<std::uint8_t> data_gz;
        if (!gzip_bytes(control_tar.finish(), control_gz, error) ||
            !gzip_bytes(data_tar, data_gz, error)) {
            return false;
        }
        ArWriter ar;
        ar.add("debian-binary", std::vector<std::uint8_t>{'2', '.', '0', '\n'});
        ar.add("control.tar.gz", control_gz);
        ar.add("data.tar.gz", data_gz);
        const std::vector<std::uint8_t> deb = ar.finish();
        std::ofstream out(target, std::ios::binary);
        out.write(reinterpret_cast<const char*>(deb.data()), static_cast<std::streamsize>(deb.size()));
        if (!out) {
            error = "cannot write " + target.string();
            return false;
        }
        return true;
    }
};

std::unique_ptr<Packer> make_linux_packer() { return std::make_unique<LinuxPacker>(); }

}  // namespace zui_tool
