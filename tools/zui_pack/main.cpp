// zui-pack: build the packages zui.json selects, for one platform. This file is
// the command line only; each platform lives in its own packager.

#include "packer.h"
#include "project_files.h"
#include "style.h"

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

std::string env_or_empty(const char* key) {
    const char* value = std::getenv(key);
    return value == nullptr ? std::string() : std::string(value);
}

std::string host_platform() {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#else
    return "linux";
#endif
}

void print_usage() {
    std::cout
        << "zui-pack <project-dir> [--platform NAME] [--root DIR] [--build DIR] [--out DIR]\n"
        << "\n"
        << "Reads zui.json from a project written by zui_create and writes the packages it\n"
        << "selects. --platform names the target: windows, linux, macos, android, ios. The\n"
        << "default is this host. A platform whose host tools are missing says so and\n"
        << "writes nothing, instead of a package that only looks like one.\n"
        << "\n"
        << "  windows   portable ZIP and a per-user installer\n"
        << "  linux     portable tar.gz and a .deb (control, desktop entry, launcher)\n"
        << "  macos     a .app bundle, and a .dmg when hdiutil is there\n"
        << "  ios       a .app bundle, and an .ipa when the host can sign\n"
        << "  android   an APK (the engine renders through SDL)\n"
        << "\n"
        << "--root and --build are the zui source tree and an existing zui build. They\n"
        << "default to ZUI_ROOT and ZUI_BUILD. --out is where packages go; the default is\n"
        << "<project>/dist. Package names carry the version.\n";
}

struct Options {
    bool help = false;
    std::string platform;
    std::filesystem::path project;
    std::filesystem::path root;
    std::filesystem::path build;
    std::filesystem::path out;
};

bool parse_args(int argc, char** argv, Options& opt) {
    std::vector<std::string> positional;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            opt.help = true;
        } else if (arg == "--root" || arg == "--build" || arg == "--out" || arg == "--platform") {
            if (i + 1 >= argc) {
                std::cerr << "zui-pack: " << arg << " needs a value\n";
                return false;
            }
            const std::string value = argv[++i];
            if (arg == "--root") {
                opt.root = value;
            } else if (arg == "--build") {
                opt.build = value;
            } else if (arg == "--out") {
                opt.out = value;
            } else {
                opt.platform = value;
                const std::string known[] = {"windows", "linux", "macos", "android", "ios"};
                bool found = false;
                for (const std::string& one : known) {
                    found = found || opt.platform == one;
                }
                if (!found) {
                    std::cerr << "zui-pack: --platform " << opt.platform
                              << " is not one of windows, linux, macos, android, ios\n";
                    return false;
                }
            }
        } else if (!arg.empty() && arg[0] == '-') {
            std::cerr << "zui-pack: unknown option " << arg << "\n";
            return false;
        } else if (arg == "android" && positional.empty() && opt.platform.empty()) {
            opt.platform = "android";
        } else {
            positional.push_back(arg);
        }
    }
    if (positional.size() == 1) {
        opt.project = positional[0];
    } else if (!opt.help) {
        std::cerr << "zui-pack: expected a project directory\n";
        return false;
    }
    if (opt.root.empty()) {
        opt.root = env_or_empty("ZUI_ROOT");
    }
    if (opt.build.empty()) {
        opt.build = env_or_empty("ZUI_BUILD");
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    zui_tool::style_setup();
    Options opt;
    if (argc < 2) {
        print_usage();
        return 1;
    }
    if (!parse_args(argc, argv, opt)) {
        print_usage();
        return 1;
    }
    if (opt.help) {
        print_usage();
        return 0;
    }
    const std::string platform = opt.platform.empty() ? host_platform() : opt.platform;
    zui_tool::PackContext ctx;
    ctx.project = opt.project;
    ctx.root = opt.root;
    ctx.build = opt.build;
    ctx.out = opt.out;
    std::string error;
    if (!zui_tool::ProjectWriter::read(ctx.project / zui_tool::kDeclFile, ctx.decl, error)) {
        std::cerr << zui_tool::style_red(zui_tool::mark_no()) << " zui-pack: " << error << "\n";
        return 1;
    }
    // A package of this version from an earlier run is removed first, so a build
    // that then fails cannot leave something stale behind to be mistaken for the
    // result.
    {
        std::error_code ec;
        const std::filesystem::path dist = ctx.dist();
        const std::string prefix = ctx.stem() + "-";
        if (std::filesystem::is_directory(dist, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(dist, ec)) {
                if (ec || !entry.is_regular_file()) {
                    continue;
                }
                if (entry.path().filename().string().rfind(prefix, 0) == 0) {
                    std::filesystem::remove(entry.path(), ec);
                }
            }
        }
    }
    if (!zui_tool::ProjectWriter::require_inputs(ctx.project, ctx.decl, error)) {
        std::cerr << zui_tool::style_red(zui_tool::mark_no()) << " zui-pack: " << error << "\n";
        return 1;
    }
    for (const std::string& note : zui_tool::ProjectWriter::unshipped_references(ctx.project, ctx.decl)) {
        std::cout << zui_tool::style_yellow("zui-pack: warning: ") << note << std::endl;
    }
    std::unique_ptr<zui_tool::Packer> packer;
    if (platform == "windows") {
        packer = zui_tool::make_windows_packer();
    } else if (platform == "linux") {
        packer = zui_tool::make_linux_packer();
    } else if (platform == "macos") {
        packer = zui_tool::make_macos_packer();
    } else if (platform == "ios") {
        packer = zui_tool::make_ios_packer();
    } else {
        packer = zui_tool::make_android_packer();
    }
    if (!packer) {
        std::cerr << "zui-pack: no packager for " << platform << "\n";
        return 1;
    }
    const std::string missing = packer->missing_host_tool(ctx);
    if (!missing.empty()) {
        std::cerr << zui_tool::style_red(zui_tool::mark_no()) << " zui-pack: " << missing << "\n";
        return 1;
    }
    std::cout << "zui-pack: " << platform << " package " << ctx.project.string() << std::endl;
    return packer->pack(ctx);
}
