// The macOS and iOS packagers. Both write an Apple bundle: the layout and the
// Info.plist are ours, the executable and the signature come from the host.
// A host that cannot build or sign for Apple says so and writes nothing.

#include "apple_bundle.h"
#include "package_text.h"
#include "packer.h"
#include "project_files.h"
#include "style.h"
#include "subprocess.h"
#include "zip.h"

#include <cstdlib>
#include <fstream>
#include <iostream>

namespace zui_tool {
namespace {

std::string env_or_empty(const char* key) {
    const char* value = std::getenv(key);
    return value == nullptr ? std::string() : std::string(value);
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
        if (line.compare(0, prefix.size(), prefix) == 0) {
            const auto eq = line.find('=');
            if (eq != std::string::npos) {
                found = line.substr(eq + 1);
            }
        }
    }
    return found;
}

bool build_apple_app(const PackContext& ctx, std::filesystem::path& exe, std::string& error) {
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
    const bool multi = generator.find("Xcode") != std::string::npos;
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

/// A PNG the project already ships, or the declared .png icon.
bool find_png_icon(const PackContext& ctx, std::filesystem::path& out) {
    std::error_code ec;
    const std::filesystem::path declared =
        ctx.project / std::filesystem::path(ctx.decl.icon, std::filesystem::path::generic_format);
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

/// sips + iconutil turn a PNG into the .icns a macOS bundle names.
bool make_icns(const std::filesystem::path& png, const std::filesystem::path& work,
               std::filesystem::path& out, std::string& error) {
#if defined(__APPLE__)
    const std::filesystem::path iconset = work / "icon.iconset";
    std::error_code ec;
    std::filesystem::create_directories(iconset, ec);
    const struct {
        const char* name;
        int size;
    } sizes[] = {{"icon_16x16.png", 16},       {"icon_16x16@2x.png", 32},
                 {"icon_32x32.png", 32},       {"icon_32x32@2x.png", 64},
                 {"icon_128x128.png", 128},    {"icon_128x128@2x.png", 256},
                 {"icon_256x256.png", 256},    {"icon_256x256@2x.png", 512},
                 {"icon_512x512.png", 512},    {"icon_512x512@2x.png", 1024}};
    for (const auto& size : sizes) {
        if (run_process(command("sips", "-z", std::to_string(size.size),
                                std::to_string(size.size), png.string(), "--out",
                                (iconset / size.name).string())) != 0) {
            error = "sips could not build the icon set";
            return false;
        }
    }
    out = work / "icon.icns";
    if (run_process(command("iconutil", "-c", "icns", iconset.string(), "-o", out.string())) !=
        0) {
        error = "iconutil could not build the icns";
        return false;
    }
    return true;
#else
    (void)png;
    (void)work;
    (void)out;
    error = "an icns is built on macOS";
    return false;
#endif
}

bool sign_bundle(const std::filesystem::path& bundle, const std::string& identity,
                 const std::filesystem::path& entitlements, std::string& error) {
    std::vector<std::string> args =
        command("codesign", "--force", "--timestamp", "--options", "runtime", "--sign", identity);
    if (!entitlements.empty()) {
        args.push_back("--entitlements");
        args.push_back(entitlements.string());
    }
    args.push_back(bundle.string());
    if (run_process(args) != 0) {
        error = "codesign failed for " + bundle.string();
        return false;
    }
    return true;
}

/// An entitlements file from the environment or the project.
std::filesystem::path apple_entitlements(const PackContext& ctx, const std::string& from_env) {
    if (!from_env.empty()) {
        return from_env;
    }
    if (ctx.decl.apple.entitlements.empty()) {
        return {};
    }
    return ctx.project / std::filesystem::path(ctx.decl.apple.entitlements,
                                              std::filesystem::path::generic_format);
}

/// Submit to Apple's notary service and staple the ticket, when the project (or
/// the environment) names a keychain profile for it. Without one, nothing is
/// submitted and the package is simply signed but not notarized.
bool notarize(const PackContext& ctx, const std::filesystem::path& artifact, std::string& error) {
    std::string profile = env_or_empty("ZUI_APPLE_NOTARY_PROFILE");
    if (profile.empty()) {
        profile = ctx.decl.apple.notary_profile;
    }
    if (profile.empty()) {
        std::cout << "zui-pack: not notarized (set apple.notaryProfile or "
                     "ZUI_APPLE_NOTARY_PROFILE)"
                  << std::endl;
        return true;
    }
    if (run_process(command("xcrun", "notarytool", "submit", artifact.string(),
                            "--keychain-profile", profile, "--wait")) != 0) {
        error = "notarytool rejected " + artifact.filename().string();
        return false;
    }
    if (run_process(command("xcrun", "stapler", "staple", artifact.string())) != 0) {
        error = "stapler could not attach the ticket to " + artifact.filename().string();
        return false;
    }
    std::cout << style_green(mark_ok()) << " notarized " << artifact.filename().string()
              << std::endl;
    return true;
}

/// The page and the declared runtime files, at their paths inside the bundle.
bool fill_bundle(const PackContext& ctx, const std::filesystem::path& exe,
                 const std::filesystem::path& destination, bool ios, AppleBundle& bundle,
                 std::string& error) {
    std::vector<std::uint8_t> bytes;
    if (!read_file_bytes(exe, bytes, error)) {
        return false;
    }
    bundle.add_executable(bytes);
    std::vector<std::pair<std::string, std::filesystem::path>> files;
    if (!ProjectWriter::collect_runtime_files(ctx.project, ctx.decl, files, error)) {
        return false;
    }
    std::vector<std::pair<std::string, std::filesystem::path>> payload = files;
    payload.emplace_back(ctx.decl.page,
                         ctx.project / std::filesystem::path(ctx.decl.page,
                                                            std::filesystem::path::generic_format));
    for (const auto& file : payload) {
        if (!read_file_bytes(file.second, bytes, error)) {
            return false;
        }
        bundle.add_resource(file.first, bytes);
    }
    for (const auto& usage : apple_usage_entries(ctx.decl)) {
        bundle.set_extra(usage.first, usage.second);
    }
    const std::string declared = apple_declared_permissions(ctx.decl);
    if (!declared.empty()) {
        bundle.set_extra("ZuiDeclaredPermissions", declared);
    }
    (void)destination;
    (void)ios;
    return true;
}

}  // namespace

class MacosPacker final : public Packer {
public:
    const char* platform() const override { return "macos"; }

    std::string missing_host_tool(const PackContext& ctx) const override {
#if defined(__APPLE__)
        (void)ctx;
        return {};
#else
        (void)ctx;
        return "a macOS package needs a macOS host: the executable has to be a Mach-O image, and "
               "codesign/hdiutil sign and wrap the bundle";
#endif
    }

    int pack(const PackContext& ctx) override {
        std::string error;
        std::filesystem::path exe;
        if (!build_apple_app(ctx, exe, error)) {
            std::cerr << "zui-pack: " << error << "\n";
            return 1;
        }
        if (!is_macho(exe)) {
            std::cerr << "zui-pack: " << exe.string() << " is not a Mach-O image\n";
            return 1;
        }
        std::error_code ec;
        std::filesystem::create_directories(ctx.dist(), ec);
        AppleBundleInfo info;
        info.name = ctx.decl.name;
        info.display_name = ctx.decl.name;
        info.identifier = ctx.decl.identifier;
        info.version = ctx.decl.version;
        info.executable = ctx.decl.name;
        info.minimum_system = "10.15";
        info.category = ctx.decl.apple.category.empty() ? "public.app-category.utilities"
                                                        : ctx.decl.apple.category;
        AppleBundle bundle(ctx.dist(), info);
        if (!fill_bundle(ctx, exe, ctx.dist(), false, bundle, error)) {
            std::cerr << "zui-pack: " << error << "\n";
            return 1;
        }
        // The engine libraries travel inside the bundle, where the loader finds
        // them through the executable's own rpath.
        if (!add_engine_libraries(ctx, bundle, exe, error)) {
            std::cerr << "zui-pack: " << error << "\n";
            return 1;
        }
        std::filesystem::path png;
        if (find_png_icon(ctx, png)) {
            std::filesystem::path icns;
            if (make_icns(png, ctx.project / ".zui", icns, error)) {
                bundle.add_resource_file("icon.icns", icns);
                bundle.set_extra("CFBundleIconFile", "icon.icns");
            } else {
                std::cout << style_yellow("zui-pack: warning: ") << error << std::endl;
            }
        }
        if (!bundle.write(error)) {
            std::cerr << "zui-pack: " << error << "\n";
            return 1;
        }
        std::string identity = env_or_empty("ZUI_APPLE_IDENTITY");
        if (identity.empty()) {
            identity = ctx.decl.apple.identity;
        }
        if (identity.empty()) {
            std::cout << "zui-pack: did not sign (set apple.identity in zui.json, or "
                         "ZUI_APPLE_IDENTITY)"
                      << std::endl;
        } else {
            const std::filesystem::path entitlements =
                apple_entitlements(ctx, env_or_empty("ZUI_APPLE_ENTITLEMENTS"));
            if (!sign_bundle(bundle.app_root(), identity, entitlements, error)) {
                std::cerr << "zui-pack: " << error << "\n";
                return 1;
            }
            std::cout << style_green(mark_ok()) << " signed " << bundle.app_root().string()
                      << std::endl;
        }
        std::cout << style_green(mark_ok()) << " app " << bundle.app_root().string() << std::endl;
        std::filesystem::path wrapped;
        if (ctx.decl.apple.dmg && env_or_empty("ZUI_APPLE_DMG") != "0") {
            const std::filesystem::path dmg = ctx.dist() / (ctx.stem() + ".dmg");
            if (run_process(command("hdiutil", "create", "-volname", ctx.decl.name, "-srcfolder",
                                    bundle.app_root().string(), "-ov", "-format", "UDZO",
                                    dmg.string())) == 0) {
                std::cout << style_green(mark_ok()) << " dmg " << dmg.string() << std::endl;
                wrapped = dmg;
            } else {
                std::cout << style_yellow("zui-pack: warning: hdiutil did not write a dmg")
                          << std::endl;
            }
        }
        if (wrapped.empty()) {
            wrapped = bundle.app_root();
        }
        return notarize(ctx, wrapped, error) ? 0 : 1;
    }

private:
    static bool add_engine_libraries(const PackContext& ctx, AppleBundle& bundle,
                                     const std::filesystem::path& exe, std::string& error) {
        const std::filesystem::path dirs[] = {exe.parent_path(), ctx.build, ctx.build / "lib"};
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
                if (name.find(".dylib") == std::string::npos) {
                    continue;
                }
                bundle.add_resource_file("lib/" + name, entry.path());
            }
        }
        (void)error;
        return true;
    }
};

class IosPacker final : public Packer {
public:
    const char* platform() const override { return "ios"; }

    std::string missing_host_tool(const PackContext& ctx) const override {
#if defined(__APPLE__)
        (void)ctx;
        return {};
#else
        (void)ctx;
        return "an iOS package needs a macOS host with Xcode, and a signing identity with a "
               "provisioning profile";
#endif
    }

    int pack(const PackContext& ctx) override {
        std::string error;
        std::filesystem::path exe;
        if (!build_apple_app(ctx, exe, error)) {
            std::cerr << "zui-pack: " << error << "\n";
            return 1;
        }
        if (!is_macho(exe)) {
            std::cerr << "zui-pack: " << exe.string() << " is not a Mach-O image\n";
            return 1;
        }
        std::error_code ec;
        const std::filesystem::path payload = ctx.project / ".zui" / "Payload";
        std::filesystem::remove_all(payload, ec);
        std::filesystem::create_directories(payload, ec);
        AppleBundleInfo info;
        info.name = ctx.decl.name;
        info.display_name = ctx.decl.name;
        info.identifier = ctx.decl.identifier;
        info.version = ctx.decl.version;
        info.executable = ctx.decl.name;
        info.ios = true;
        info.orientations = ctx.decl.apple.orientations.empty()
                                ? std::vector<std::string>{"UIInterfaceOrientationPortrait",
                                                           "UIInterfaceOrientationLandscapeLeft",
                                                           "UIInterfaceOrientationLandscapeRight"}
                                : ctx.decl.apple.orientations;
        AppleBundle bundle(payload, info);
        if (!fill_bundle(ctx, exe, payload, true, bundle, error)) {
            std::cerr << "zui-pack: " << error << "\n";
            return 1;
        }
        std::string profile = env_or_empty("ZUI_IPHONE_PROVISIONING");
        if (profile.empty() && !ctx.decl.apple.provisioning.empty()) {
            profile = (ctx.project / std::filesystem::path(ctx.decl.apple.provisioning,
                                                           std::filesystem::path::generic_format))
                          .string();
        }
        std::string identity = env_or_empty("ZUI_APPLE_IDENTITY");
        if (identity.empty()) {
            identity = ctx.decl.apple.identity;
        }
        if (!profile.empty()) {
            bundle.add_resource_file("embedded.mobileprovision", profile);
        }
        if (!bundle.write(error)) {
            std::cerr << "zui-pack: " << error << "\n";
            return 1;
        }
        if (identity.empty() || profile.empty()) {
            std::cerr << "zui-pack: an installable iOS bundle needs ZUI_APPLE_IDENTITY and "
                         "ZUI_IPHONE_PROVISIONING; the bundle was written to "
                      << bundle.app_root().string() << "\n";
            return 1;
        }
        const std::filesystem::path entitlements =
            apple_entitlements(ctx, env_or_empty("ZUI_APPLE_ENTITLEMENTS"));
        if (!sign_bundle(bundle.app_root(), identity, entitlements, error)) {
            std::cerr << "zui-pack: " << error << "\n";
            return 1;
        }
        // An ipa is a zip of Payload/.
        std::vector<ZipEntry> entries;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(bundle.app_root(), ec)) {
            if (ec || !entry.is_regular_file()) {
                continue;
            }
            const std::filesystem::path relative =
                std::filesystem::relative(entry.path(), payload, ec);
            if (ec) {
                continue;
            }
            ZipEntry zip;
            zip.name = "Payload/" + relative.generic_string();
            if (!read_file_bytes(entry.path(), zip.bytes, error)) {
                std::cerr << "zui-pack: " << error << "\n";
                return 1;
            }
            entries.push_back(std::move(zip));
        }
        const std::filesystem::path ipa = ctx.dist() / (ctx.stem() + ".ipa");
        std::filesystem::create_directories(ctx.dist(), ec);
        if (!write_zip(ipa, entries, error)) {
            std::cerr << "zui-pack: " << error << "\n";
            return 1;
        }
        std::cout << style_green(mark_ok()) << " ipa " << ipa.string() << std::endl;
        return 0;
    }
};

std::unique_ptr<Packer> make_macos_packer() { return std::make_unique<MacosPacker>(); }
std::unique_ptr<Packer> make_ios_packer() { return std::make_unique<IosPacker>(); }

}  // namespace zui_tool
