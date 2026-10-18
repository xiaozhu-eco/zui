// Android package. The engine renders through SDL, so the APK is a real SDL
// application: the activity is an SDL activity, the page and the declared
// runtime files ship as assets, and the app itself is a shared library whose
// main() SDL calls on its native thread. Nothing here needs an emulator to
// explain itself: `aapt2 dump badging` and `apksigner verify` read the result.
//
// The steps, in order: build the app library and the payload bridge for each
// ABI, compile the SDL Java classes and the two generated classes into a dex,
// compile icons and link the manifest and resources, assemble one APK with the
// native libraries stored, align it, then sign it.

#include "android.h"

#include "icon.h"
#include "package_text.h"
#include "project_files.h"
#include "packer.h"
#include "subprocess.h"
#include "project.h"
#include "zip.h"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace zui_tool {
namespace {

constexpr const char* kAppLibrary = "main";
constexpr const char* kBridgeLibrary = "zui_bridge";

std::string env_string(const char* key) {
    const char* value = std::getenv(key);
    return value == nullptr ? std::string() : std::string(value);
}

bool is_file(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

bool is_dir(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::is_directory(path, ec);
}

int leading_number(const std::string& text) {
    int value = 0;
    bool any = false;
    for (char c : text) {
        if (c >= '0' && c <= '9') {
            any = true;
            value = value * 10 + (c - '0');
            if (value > 1000000) {
                break;
            }
            continue;
        }
        if (any) {
            break;
        }
    }
    return any ? value : -1;
}

bool newer_version(const std::string& candidate, const std::string& current) {
    if (current.empty()) {
        return true;
    }
    const int a = leading_number(candidate);
    const int b = leading_number(current);
    if (a != b) {
        return a > b;
    }
    return candidate > current;
}

std::filesystem::path newest_child(const std::filesystem::path& dir) {
    std::string best;
    std::error_code ec;
    if (!is_dir(dir)) {
        return {};
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec || !entry.is_directory()) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        if (newer_version(name, best)) {
            best = name;
        }
    }
    if (best.empty()) {
        return {};
    }
    return dir / best;
}

void consider_dir(std::vector<std::filesystem::path>& out, const std::filesystem::path& path) {
    if (!path.empty() && is_dir(path)) {
        out.push_back(path);
    }
}

std::filesystem::path find_android_jar(const std::filesystem::path& sdk) {
    const std::filesystem::path platform = newest_child(sdk / "platforms");
    if (platform.empty()) {
        return {};
    }
    const std::filesystem::path jar = platform / "android.jar";
    return is_file(jar) ? jar : std::filesystem::path();
}

std::filesystem::path find_build_tools(const std::filesystem::path& sdk) {
    const std::filesystem::path tools = newest_child(sdk / "build-tools");
    return is_dir(tools) ? tools : std::filesystem::path();
}

std::filesystem::path find_ndk(const std::filesystem::path& sdk) {
    std::vector<std::filesystem::path> ndks;
    auto consider = [&ndks](const std::filesystem::path& path) {
        if (!path.empty() && is_file(path / "build" / "cmake" / "android.toolchain.cmake")) {
            ndks.push_back(path);
        }
    };
    consider(env_string("ANDROID_NDK_HOME"));
    consider(env_string("ANDROID_NDK_ROOT"));
    consider(env_string("ANDROID_NDK"));
    consider(newest_child(sdk / "ndk"));
    return ndks.empty() ? std::filesystem::path() : ndks.front();
}

bool usable_jdk(const std::filesystem::path& home) {
#if defined(_WIN32)
    return is_file(home / "bin" / "java.exe") && is_file(home / "bin" / "keytool.exe") &&
           is_file(home / "bin" / "javac.exe") && is_file(home / "bin" / "jar.exe");
#else
    return is_file(home / "bin" / "java") && is_file(home / "bin" / "keytool") &&
           is_file(home / "bin" / "javac") && is_file(home / "bin" / "jar");
#endif
}

std::filesystem::path java_on_path() {
#if defined(_WIN32)
    char buf[MAX_PATH];
    const DWORD n = SearchPathA(nullptr, "java.exe", nullptr, MAX_PATH, buf, nullptr);
    if (n == 0 || n >= MAX_PATH) {
        return {};
    }
    const std::filesystem::path java(buf);
    const std::filesystem::path home = java.parent_path().parent_path();
    return usable_jdk(home) ? home : std::filesystem::path();
#else
    return {};
#endif
}

const char* exe_suffix() {
#if defined(_WIN32)
    return ".exe";
#else
    return "";
#endif
}

// The Android SDK, NDK, and JDK pieces the package needs. Every member is
// required: find_tools either fills them all or names the one that is missing.
struct Tools {
    std::filesystem::path sdk;
    std::filesystem::path ndk;
    std::filesystem::path jdk;
    std::filesystem::path build_tools;
    std::filesystem::path aapt2;
    std::filesystem::path zipalign;
    std::filesystem::path apksigner;
    std::filesystem::path apksigner_jar;
    std::filesystem::path d8_jar;
    std::filesystem::path android_jar;
    std::filesystem::path java;
    std::filesystem::path javac;
    std::filesystem::path jar_tool;
    std::filesystem::path keytool;
    std::filesystem::path clangxx;
    std::filesystem::path llvm_strip;
    std::string abi = "arm64-v8a";
    std::string triple = "aarch64-linux-android";
};

bool find_tools(Tools& tools, std::string& error) {
    std::vector<std::filesystem::path> sdks;
    consider_dir(sdks, env_string("ANDROID_HOME"));
    consider_dir(sdks, env_string("ANDROID_SDK_ROOT"));
    const std::string local = env_string("LOCALAPPDATA");
    if (!local.empty()) {
        consider_dir(sdks, std::filesystem::path(local) / "Android" / "Sdk");
    }
    const std::string home = env_string("USERPROFILE");
    if (!home.empty()) {
        consider_dir(sdks, std::filesystem::path(home) / "AppData" / "Local" / "Android" / "Sdk");
    }
    consider_dir(sdks, "C:/Android/Sdk");
    for (const auto& sdk : sdks) {
        const std::filesystem::path jar = find_android_jar(sdk);
        const std::filesystem::path tools_dir = find_build_tools(sdk);
        if (!jar.empty() && !tools_dir.empty()) {
            tools.sdk = sdk;
            tools.android_jar = jar;
            tools.build_tools = tools_dir;
            break;
        }
    }
    if (tools.sdk.empty()) {
        error = "missing Android SDK; set ANDROID_HOME or install the SDK under "
                "%LOCALAPPDATA%/Android/Sdk";
        return false;
    }
    tools.aapt2 = tools.build_tools / ("aapt2" + std::string(exe_suffix()));
    tools.zipalign = tools.build_tools / ("zipalign" + std::string(exe_suffix()));
    tools.apksigner = tools.build_tools / ("apksigner.bat");
    tools.apksigner_jar = tools.build_tools / "lib" / "apksigner.jar";
    tools.d8_jar = tools.build_tools / "lib" / "d8.jar";
    if (!is_file(tools.aapt2) || !is_file(tools.zipalign) || !is_file(tools.d8_jar)) {
        error = "missing aapt2, zipalign, or d8 in " + tools.build_tools.string();
        return false;
    }
    if (!is_file(tools.apksigner) && !is_file(tools.apksigner_jar)) {
        error = "missing apksigner in " + tools.build_tools.string();
        return false;
    }

    std::vector<std::filesystem::path> jdks;
    consider_dir(jdks, env_string("JAVA_HOME"));
    const std::string program_files = env_string("ProgramFiles");
    if (!program_files.empty()) {
        consider_dir(jdks, std::filesystem::path(program_files) / "Android" / "Android Studio" / "jbr");
        consider_dir(jdks, std::filesystem::path(program_files) / "Java");
    }
    const std::string program_files_x86 = env_string("ProgramFiles(x86)");
    if (!program_files_x86.empty()) {
        consider_dir(jdks, std::filesystem::path(program_files_x86) / "AndroidStudio" / "jbr");
        consider_dir(jdks,
                     std::filesystem::path(program_files_x86) / "Android" / "Android Studio" / "jbr");
    }
    // Android Studio's bundled JBR is a JDK outside the ProgramFiles variables.
    consider_dir(jdks, "D:/Program Files (x86)/AndroidStudio/jbr");
    consider_dir(jdks, "C:/Program Files (x86)/AndroidStudio/jbr");
    if (!local.empty()) {
        consider_dir(jdks, std::filesystem::path(local) / "Programs" / "Android Studio" / "jbr");
    }
    const std::filesystem::path from_path = java_on_path();
    if (!from_path.empty()) {
        jdks.push_back(from_path);
    }
    for (const auto& jdk : jdks) {
        if (usable_jdk(jdk)) {
            tools.jdk = jdk;
            break;
        }
        if (is_dir(jdk)) {
            std::error_code ec;
            for (const auto& child : std::filesystem::directory_iterator(jdk, ec)) {
                if (!ec && child.is_directory() && usable_jdk(child.path())) {
                    tools.jdk = child.path();
                    break;
                }
            }
        }
        if (!tools.jdk.empty()) {
            break;
        }
    }
    if (tools.jdk.empty()) {
        error = "missing JDK; set JAVA_HOME or install Android Studio";
        return false;
    }
    tools.java = tools.jdk / "bin" / ("java" + std::string(exe_suffix()));
    tools.javac = tools.jdk / "bin" / ("javac" + std::string(exe_suffix()));
    tools.jar_tool = tools.jdk / "bin" / ("jar" + std::string(exe_suffix()));
    tools.keytool = tools.jdk / "bin" / ("keytool" + std::string(exe_suffix()));

    tools.ndk = find_ndk(tools.sdk);
    if (tools.ndk.empty()) {
        error = "missing Android NDK; install it under " + tools.sdk.string() + "/ndk";
        return false;
    }
    const std::filesystem::path prebuilt = tools.ndk / "toolchains" / "llvm" / "prebuilt";
#if defined(_WIN32)
    const char* host = "windows-x86_64";
#elif defined(__APPLE__)
    const char* host = "darwin-x86_64";
#else
    const char* host = "linux-x86_64";
#endif
    tools.clangxx = prebuilt / host / "bin" / ("clang++" + std::string(exe_suffix()));
    if (!is_file(tools.clangxx)) {
        error = "missing " + tools.clangxx.string();
        return false;
    }
    std::error_code ec;
    for (const auto& candidate : std::filesystem::directory_iterator(prebuilt, ec)) {
        if (ec || !candidate.is_directory()) {
            continue;
        }
        const std::filesystem::path strip =
            candidate.path() / "bin" / ("llvm-strip" + std::string(exe_suffix()));
        if (is_file(strip)) {
            tools.llvm_strip = strip;
            break;
        }
    }
    return true;
}

std::filesystem::path find_android_zui(const std::filesystem::path& root,
                                       const std::filesystem::path& build) {
    std::vector<std::filesystem::path> candidates;
    auto add = [&candidates](const std::filesystem::path& dir) {
        if (dir.empty()) {
            return;
        }
        candidates.push_back(dir / "libzui.so");
        candidates.push_back(dir / "lib" / "arm64-v8a" / "libzui.so");
    };
    add(build);
    if (!build.empty()) {
        add(build.parent_path() / "android-arm64");
    }
    if (!root.empty()) {
        add(root / "build" / "android-arm64");
    }
    for (const auto& candidate : candidates) {
        if (is_file(candidate)) {
            return candidate;
        }
    }
    return {};
}

// Every shared library the app needs beside the engine: the engine, then the
// plugin libraries built next to it.
std::vector<std::filesystem::path> native_libraries(const std::filesystem::path& libzui) {
    std::vector<std::filesystem::path> libs;
    libs.push_back(libzui);
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(libzui.parent_path(), ec)) {
        if (ec || !entry.is_regular_file()) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        if (name == "libzui.so") {
            continue;
        }
        const bool plugin = name.rfind("libzui_plugin_", 0) == 0 && name.size() > 3 &&
                            name.compare(name.size() - 3, 3, ".so") == 0;
        if (plugin || name == "libc++_shared.so") {
            libs.push_back(entry.path());
        }
    }
    return libs;
}

std::string replace_all(std::string text, const std::string& from, const std::string& to) {
    for (std::size_t at = text.find(from); at != std::string::npos; at = text.find(from, at)) {
        text.replace(at, from.size(), to);
        at += to.size();
    }
    return text;
}

bool write_text(const std::filesystem::path& path, const std::string& text, std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary);
    out << text;
    if (!out) {
        error = "cannot write " + path.string();
        return false;
    }
    return true;
}

bool remove_tree(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    return !ec;
}

const char* kBridgeSource() {
    return "#include <jni.h>\n"
           "#include <unistd.h>\n"
           "\n"
           "extern \"C\" JNIEXPORT void JNICALL\n"
           "Java_com_zui_runtime_ZuiPayload_chdir(JNIEnv* env, jclass, jstring dir) {\n"
           "    if (dir == nullptr) {\n"
           "        return;\n"
           "    }\n"
           "    const char* path = env->GetStringUTFChars(dir, nullptr);\n"
           "    if (path != nullptr) {\n"
           "        chdir(path);\n"
           "        env->ReleaseStringUTFChars(dir, path);\n"
           "    }\n"
           "}\n";
}

const char* kPayloadJava() {
    // Unpacks assets/payload into the app's files directory once per version,
    // then moves the working directory there, because the engine opens its page
    // relative to the working directory.
    return "package com.zui.runtime;\n"
           "\n"
           "import android.content.Context;\n"
           "import android.content.res.AssetManager;\n"
           "\n"
           "import java.io.File;\n"
           "import java.io.FileInputStream;\n"
           "import java.io.FileOutputStream;\n"
           "import java.io.IOException;\n"
           "import java.io.InputStream;\n"
           "import java.io.OutputStream;\n"
           "\n"
           "public final class ZuiPayload {\n"
           "    private static final String STAMP = \".payload-version\";\n"
           "    private static final String VERSION = \"@VERSION@\";\n"
           "\n"
           "    static {\n"
           "        System.loadLibrary(\"zui_bridge\");\n"
           "    }\n"
           "\n"
           "    private ZuiPayload() {}\n"
           "\n"
           "    private static native void chdir(File dir);\n"
           "\n"
           "    public static void unpack(Context context) {\n"
           "        File root = new File(context.getFilesDir(), \"payload\");\n"
           "        if (root.isDirectory() && VERSION.equals(readStamp(new File(root, STAMP)))) {\n"
           "            chdir(root);\n"
           "            return;\n"
           "        }\n"
           "        deleteTree(root);\n"
           "        try {\n"
           "            copyTree(context.getAssets(), \"payload\", root);\n"
           "            writeStamp(new File(root, STAMP), VERSION);\n"
           "        } catch (IOException error) {\n"
           "            android.util.Log.e(\"zui\", \"payload unpack failed\", error);\n"
           "        }\n"
           "        chdir(root);\n"
           "    }\n"
           "\n"
           "    private static String readStamp(File file) {\n"
           "        if (!file.isFile()) {\n"
           "            return \"\";\n"
           "        }\n"
           "        try {\n"
           "            InputStream in = new FileInputStream(file);\n"
           "            try {\n"
           "                byte[] bytes = new byte[64];\n"
           "                int n = in.read(bytes);\n"
           "                return n <= 0 ? \"\" : new String(bytes, 0, n, \"UTF-8\").trim();\n"
           "            } finally {\n"
           "                in.close();\n"
           "            }\n"
           "        } catch (IOException error) {\n"
           "            return \"\";\n"
           "        }\n"
           "    }\n"
           "\n"
           "    private static void writeStamp(File file, String text) throws IOException {\n"
           "        OutputStream out = new FileOutputStream(file);\n"
           "        try {\n"
           "            out.write(text.getBytes(\"UTF-8\"));\n"
           "        } finally {\n"
           "            out.close();\n"
           "        }\n"
           "    }\n"
           "\n"
           "    private static void copyTree(AssetManager assets, String from, File to)\n"
           "            throws IOException {\n"
           "        String[] children = assets.list(from);\n"
           "        if (children == null || children.length == 0) {\n"
           "            copyFile(assets, from, to);\n"
           "            return;\n"
           "        }\n"
           "        if (!to.isDirectory() && !to.mkdirs()) {\n"
           "            throw new IOException(\"cannot create \" + to.getPath());\n"
           "        }\n"
           "        for (String child : children) {\n"
           "            copyTree(assets, from + \"/\" + child, new File(to, child));\n"
           "        }\n"
           "    }\n"
           "\n"
           "    private static void copyFile(AssetManager assets, String from, File to)\n"
           "            throws IOException {\n"
           "        File parent = to.getParentFile();\n"
           "        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {\n"
           "            throw new IOException(\"cannot create \" + parent.getPath());\n"
           "        }\n"
           "        InputStream in = assets.open(from);\n"
           "        try {\n"
           "            OutputStream out = new FileOutputStream(to);\n"
           "            try {\n"
           "                byte[] buffer = new byte[65536];\n"
           "                for (int n = in.read(buffer); n > 0; n = in.read(buffer)) {\n"
           "                    out.write(buffer, 0, n);\n"
           "                }\n"
           "            } finally {\n"
           "                out.close();\n"
           "            }\n"
           "        } finally {\n"
           "            in.close();\n"
           "        }\n"
           "    }\n"
           "\n"
           "    private static void deleteTree(File file) {\n"
           "        File[] children = file.listFiles();\n"
           "        if (children != null) {\n"
           "            for (File child : children) {\n"
           "                deleteTree(child);\n"
           "            }\n"
           "        }\n"
           "        file.delete();\n"
           "    }\n"
           "}\n";
}

// The permission vocabulary of the C++ catalog, as Java: stable name → Android
// permission, whether the OS asks at run time, and the author's reason. It is
// generated from the same table the manifest is built from, so the two cannot
// drift.
std::string permissions_java() {
    std::string rows;
    for (const CatalogPermission& entry : kPermissionCatalog) {
        rows += "        ANDROID.put(\"" + std::string(entry.name) + "\", \"" + entry.android +
                "\");\n";
        rows += "        REASON.put(\"" + std::string(entry.name) + "\", \"" +
                std::string(entry.reason) + "\");\n";
        rows += "        RUNTIME.put(\"" + std::string(entry.name) + "\", " +
                (entry.runtime ? "Boolean.TRUE" : "Boolean.FALSE") + ");\n";
        rows += "        SETTINGS.put(\"" + std::string(entry.name) + "\", " +
                (entry.settings ? "Boolean.TRUE" : "Boolean.FALSE") + ");\n";
    }
    return "package com.zui.runtime;\n"
           "\n"
           "import java.util.HashMap;\n"
           "import java.util.Map;\n"
           "\n"
           "/** The permission names an app may ask for, and what each one is. */\n"
           "public final class ZuiPermissions {\n"
           "    private static final Map<String, String> ANDROID = new HashMap<String, String>();\n"
           "    private static final Map<String, String> REASON = new HashMap<String, String>();\n"
           "    private static final Map<String, Boolean> RUNTIME =\n"
           "            new HashMap<String, Boolean>();\n"
           "    private static final Map<String, Boolean> SETTINGS =\n"
           "            new HashMap<String, Boolean>();\n"
           "\n"
           "    static {\n" +
           rows +
           "    }\n"
           "\n"
           "    private ZuiPermissions() {}\n"
           "\n"
           "    /** The Android permission for a stable name, or null. */\n"
           "    public static String android(String name) {\n"
           "        return name == null ? null : ANDROID.get(name);\n"
           "    }\n"
           "\n"
           "    /** The author's reason, for a dialog the app shows. */\n"
           "    public static String reason(String name) {\n"
           "        return name == null ? null : REASON.get(name);\n"
           "    }\n"
           "\n"
           "    /** Whether the OS asks the user before the permission works. */\n"
           "    public static boolean runtime(String name) {\n"
           "        return name != null && Boolean.TRUE.equals(RUNTIME.get(name));\n"
           "    }\n"
           "\n"
           "    /** Whether the app opens a system settings page instead of a dialog. */\n"
           "    public static boolean settings(String name) {\n"
           "        return name != null && Boolean.TRUE.equals(SETTINGS.get(name));\n"
           "    }\n"
           "}\n";
}

const char* kActivityJava() {
    // SDL owns the surface and the native thread; the engine owns the document.
    return "package com.zui.runtime;\n"
           "\n"
           "import android.content.Intent;\n"
           "import android.os.Bundle;\n"
           "\n"
           "import org.libsdl.app.SDLActivity;\n"
           "\n"
           "public class ZuiActivity extends SDLActivity {\n"
           "    @Override\n"
           "    protected void onCreate(Bundle savedInstanceState) {\n"
           "        // The engine, with SDL inside it, has to be loaded before the host\n"
           "        // services and before SDL starts the native thread.\n"
           "        System.loadLibrary(\"@LIBRARY@\");\n"
           "        ZuiPayload.unpack(this);\n"
           "        attachHost(this);\n"
           "        super.onCreate(savedInstanceState);\n"
           "    }\n"
           "\n"
           "    private static void attachHost(android.app.Activity activity) {\n"
           "        try {\n"
           "            zui.ZuiHost.attach(activity);\n"
           "        } catch (Throwable ignored) {\n"
           "            // The engine still runs; picks and the clipboard report that\n"
           "            // they are unavailable.\n"
           "        }\n"
           "    }\n"
           "\n"
           "    @Override\n"
           "    protected String[] getLibraries() {\n"
           "        // SDL is built into the engine library, so there is no libSDL3.so.\n"
           "        return new String[] {\"@LIBRARY@\", \"main\"};\n"
           "    }\n"
           "\n"
           "    @Override\n"
           "    protected String getMainFunction() {\n"
           "        return \"main\";\n"
           "    }\n"
           "\n"
           "    @Override\n"
           "    protected void onActivityResult(int request, int result, Intent data) {\n"
           "        if (!zui.ZuiHost.onActivityResult(request, result, data)) {\n"
           "            super.onActivityResult(request, result, data);\n"
           "        }\n"
           "    }\n"
           "\n"
           "    @Override\n"
           "    protected void onResume() {\n"
           "        super.onResume();\n"
           "        try {\n"
           "            zui.ZuiHost.onResume();\n"
           "        } catch (Throwable ignored) {\n"
           "        }\n"
           "    }\n"
           "\n"
           "    @Override\n"
           "    public void onRequestPermissionsResult(int request, String[] permissions,\n"
           "            int[] results) {\n"
           "        // zui.ZuiHost answers the engine's pending request_permission.\n"
           "        if (!zui.ZuiHost.onRequestPermissionsResult(request, permissions, results)) {\n"
           "            super.onRequestPermissionsResult(request, permissions, results);\n"
           "        }\n"
           "    }\n"
           "}\n";
}

bool collect_tree(const std::filesystem::path& dir, const std::string& prefix,
                  std::vector<std::pair<std::string, std::filesystem::path>>& out,
                  std::string& error) {
    std::error_code ec;
    if (is_file(dir)) {
        out.emplace_back(prefix, dir);
        return true;
    }
    if (!is_dir(dir)) {
        error = "missing " + dir.string();
        return false;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir, ec)) {
        if (ec || !entry.is_regular_file()) {
            continue;
        }
        const std::filesystem::path relative = std::filesystem::relative(entry.path(), dir, ec);
        if (ec) {
            error = "cannot read " + dir.string();
            return false;
        }
        out.emplace_back(prefix + "/" + relative.generic_string(), entry.path());
    }
    return true;
}

bool build_bridge(const Tools& tools, const std::filesystem::path& work, int api,
                  std::filesystem::path& out, std::string& error) {
    const std::filesystem::path source = work / "bridge.cpp";
    if (!write_text(source, kBridgeSource(), error)) {
        return false;
    }
    out = work / "lib" / tools.abi / ("lib" + std::string(kBridgeLibrary) + ".so");
    std::error_code ec;
    std::filesystem::create_directories(out.parent_path(), ec);
    const std::vector<std::string> args = command(
        tools.clangxx.string(), "--target=" + tools.triple + std::to_string(api), "-shared",
        "-fPIC", "-O2", "-std=c++17", "-o", out.string(), source.string());
    if (run_process(args) != 0 || !is_file(out)) {
        error = "cannot build the payload bridge for " + tools.abi;
        return false;
    }
    return true;
}

// The app library: main() in a shared library, which SDL loads and calls. An
// android target in the project wins; a single-file entry is built here.
bool build_app_library(const Project& decl, const std::filesystem::path& project,
                       const std::filesystem::path& root, const std::filesystem::path& android_build,
                       const Tools& tools, const std::filesystem::path& work, int api,
                       std::filesystem::path& out, std::string& error) {
    out = work / "lib" / tools.abi / ("lib" + std::string(kAppLibrary) + ".so");
    std::error_code ec;
    std::filesystem::create_directories(out.parent_path(), ec);
    if (!decl.android.target.empty()) {
        // An in-repo example keeps its android library in the zui build tree. A
        // project of its own carries the target in its CMakeLists, so the packer
        // configures that project with the NDK toolchain itself.
        std::filesystem::path built_in = android_build;
        bool built = false;
        if (is_dir(android_build)) {
            built = run_msvc_process(
                        command(cmake_program(android_build), "--build", android_build.string(),
                                "--target", decl.android.target),
                        work) == 0;
        }
        if (!built) {
            const std::filesystem::path toolchain =
                root / "cmake" / "toolchains" / "android-arm64.cmake";
            if (!is_file(toolchain)) {
                error = "missing " + toolchain.string();
                return false;
            }
            const std::filesystem::path app_build = project / ".zui" / "build" / "android";
            if (run_msvc_process(command(cmake_program(android_build), "-S", project.string(), "-B",
                                         app_build.string(),
                                         "-DCMAKE_TOOLCHAIN_FILE=" + toolchain.string(),
                                         "-DZUI_ROOT=" + root.string(),
                                         "-DZUI_BUILD=" + android_build.string(),
                                         "-DCMAKE_BUILD_TYPE=Release"),
                                 work) != 0) {
                error = "cannot configure " + project.string() + " for android";
                return false;
            }
            if (run_msvc_process(command(cmake_program(android_build), "--build", app_build.string(),
                                         "--target", decl.android.target),
                                 work) != 0) {
                error = "cannot build the android target " + decl.android.target;
                return false;
            }
            built_in = app_build;
        }
        // The convention is one name: libmain.so, whatever the target is called.
        std::vector<std::filesystem::path> found;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(built_in, ec)) {
            if (ec) {
                break;
            }
            if (!entry.is_regular_file()) {
                continue;
            }
            const std::string name = entry.path().filename().string();
            if (name == "lib" + std::string(kAppLibrary) + ".so" ||
                name == "lib" + decl.android.target + ".so") {
                found.push_back(entry.path());
            }
        }
        if (found.empty()) {
            error = "the android target " + decl.android.target + " produced no lib" +
                    kAppLibrary + ".so in " + built_in.string();
            return false;
        }
        std::filesystem::copy_file(found.front(), out,
                                   std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            error = "cannot copy " + found.front().string();
            return false;
        }
        return true;
    }
    const std::filesystem::path entry = project / std::filesystem::path(
                                                       decl.entry, std::filesystem::path::generic_format);
    if (!is_file(entry)) {
        error = "missing entry " + decl.entry;
        return false;
    }
    if (!decl.target.empty()) {
        error = "this project builds the zui target " + decl.target +
                "; name the android library in android.target as well";
        return false;
    }
    const std::filesystem::path zui_so = find_android_zui(root, android_build);
    if (zui_so.empty()) {
        error = "missing the android build of zui (libzui.so)";
        return false;
    }
    const std::vector<std::string> args = command(
        tools.clangxx.string(), "--target=" + tools.triple + std::to_string(api), "-shared",
        "-fPIC", "-O2", "-std=c++17", "-I", (root / "include").string(), "-I",
        (android_build / "generated" / "include").string(), entry.string(), "-o", out.string(), "-L",
        zui_so.parent_path().string(), "-lzui", "-llog", "-landroid");
    if (run_process(args) != 0 || !is_file(out)) {
        error = "cannot build " + entry.string() + " for " + tools.abi;
        return false;
    }
    return true;
}

bool compile_java(const Tools& tools, const std::filesystem::path& root,
                  const Project& decl, const std::filesystem::path& work,
                  std::filesystem::path& dex, std::string& error) {
    const std::filesystem::path java_root = root / "third_party" / "SDL" / "android-project" /
                                           "app" / "src" / "main" / "java";
    const std::filesystem::path host_java = root / "source" / "backends" / "android";
    if (!is_dir(java_root)) {
        error = "missing the SDL Java sources under " + java_root.string();
        return false;
    }
    const std::filesystem::path media_java = root / "plugins" / "media_key" / "android" / "ZuiMediaCallback.java";
    const std::filesystem::path hotkey_java = root / "plugins" / "hotkey" / "android" / "ZuiHotkeyInvoke.java";
    const std::filesystem::path tray_java = root / "plugins" / "tray" / "android" / "ZuiTrayReceiver.java";
    if (!is_file(host_java / "zui" / "ZuiHost.java")) {
        error = "missing " + (host_java / "zui" / "ZuiHost.java").string();
        return false;
    }
    if (!is_file(media_java) || !is_file(hotkey_java) || !is_file(tray_java)) {
        error = "missing Android media-key, hotkey, or tray callback Java";
        return false;
    }
    std::vector<std::filesystem::path> sources;
    std::error_code ec;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(java_root, ec)) {
        if (!ec && entry.is_regular_file() && entry.path().extension() == ".java") {
            sources.push_back(entry.path());
        }
    }
    sources.push_back(host_java / "zui" / "ZuiHost.java");
    sources.push_back(media_java);
    sources.push_back(hotkey_java);
    sources.push_back(tray_java);
    if (sources.empty()) {
        error = "no Java sources to compile";
        return false;
    }
    const std::filesystem::path generated = work / "java" / "com" / "zui" / "runtime";
    if (!write_text(generated / "ZuiPayload.java",
                    replace_all(kPayloadJava(), "@VERSION@", decl.version), error) ||
        !write_text(generated / "ZuiActivity.java",
                    replace_all(kActivityJava(), "@LIBRARY@", "zui"), error) ||
        !write_text(generated / "ZuiPermissions.java", permissions_java(), error)) {
        return false;
    }
    sources.push_back(generated / "ZuiPayload.java");
    sources.push_back(generated / "ZuiActivity.java");
    sources.push_back(generated / "ZuiPermissions.java");

    const std::filesystem::path classes = work / "classes";
    remove_tree(classes);
    std::filesystem::create_directories(classes, ec);
    std::vector<std::string> javac = command(tools.javac.string(), "--release", "8", "-encoding",
                                             "UTF-8", "-nowarn", "-classpath",
                                             tools.android_jar.string(), "-d", classes.string());
    for (const std::filesystem::path& source : sources) {
        javac.push_back(source.string());
    }
    if (run_process(javac) != 0) {
        error = "javac failed";
        return false;
    }
    const std::filesystem::path dex_dir = work / "dex";
    remove_tree(dex_dir);
    std::filesystem::create_directories(dex_dir, ec);
    std::vector<std::string> d8 = command(tools.java.string(), "-cp", tools.d8_jar.string(),
                                          "com.android.tools.r8.D8", "--lib",
                                          tools.android_jar.string(), "--min-api",
                                          std::to_string(decl.android.min_sdk), "--output",
                                          dex_dir.string());
    for (const auto& entry : std::filesystem::recursive_directory_iterator(classes, ec)) {
        if (!ec && entry.is_regular_file() && entry.path().extension() == ".class") {
            d8.push_back(entry.path().string());
        }
    }
    if (run_process(d8) != 0) {
        error = "d8 failed";
        return false;
    }
    dex = dex_dir / "classes.dex";
    if (!is_file(dex)) {
        error = "d8 produced no classes.dex";
        return false;
    }
    return true;
}

bool link_resources(const Tools& tools, const std::filesystem::path& project,
                    const Project& decl, const std::filesystem::path& work, int version_code,
                    std::filesystem::path& base_apk, std::string& error) {
    const std::filesystem::path manifest = work / "AndroidManifest.xml";
    const std::filesystem::path res = work / "res";
    std::string icon_error;
    const bool with_icon = write_launcher_icons(res, project, {}, decl.icon, icon_error);
    if (!with_icon) {
#if defined(_WIN32)
        error = icon_error.empty() ? "cannot write the launcher icons" : icon_error;
        return false;
#else
        std::cout << "zui-pack: no launcher icon: " << icon_error << std::endl;
#endif
    }
    if (!write_text(manifest, android_manifest(decl, version_code, with_icon), error)) {
        return false;
    }
    std::vector<std::string> compiled;
    if (with_icon) {
        const std::filesystem::path compiled_dir = work / "compiled";
        remove_tree(compiled_dir);
        std::error_code ec;
        std::filesystem::create_directories(compiled_dir, ec);
        for (const auto& entry : std::filesystem::recursive_directory_iterator(res, ec)) {
            if (ec || !entry.is_regular_file() || entry.path().extension() != ".png") {
                continue;
            }
            const std::filesystem::path zip =
                compiled_dir / (entry.path().parent_path().filename().string() + ".zip");
            if (run_process(command(tools.aapt2.string(), "compile", "-o", zip.string(),
                                    entry.path().string())) != 0 ||
                !is_file(zip)) {
                error = "aapt2 compile failed";
                return false;
            }
            compiled.push_back(zip.string());
        }
        if (compiled.empty()) {
            error = "no compiled launcher icon";
            return false;
        }
    }
    base_apk = work / "resources.apk";
    std::vector<std::string> link =
        command(tools.aapt2.string(), "link", "-o", base_apk.string(), "-I",
                tools.android_jar.string(), "--manifest", manifest.string(), "--min-sdk-version",
                std::to_string(decl.android.min_sdk), "--target-sdk-version",
                std::to_string(decl.android.target_sdk), "--version-code",
                std::to_string(version_code), "--version-name", decl.version);
    for (const std::string& zip : compiled) {
        link.push_back(zip);
    }
    if (run_process(link) != 0 || !is_file(base_apk)) {
        error = "aapt2 link failed";
        return false;
    }
    return true;
}

bool assemble_apk(const std::filesystem::path& base_apk, const std::filesystem::path& dex,
                  const std::vector<std::filesystem::path>& libraries,
                  const std::filesystem::path& payload, const std::string& abi,
                  const std::filesystem::path& out, std::string& error) {
    std::vector<std::uint8_t> bytes;
    if (!read_file_bytes(base_apk, bytes, error)) {
        return false;
    }
    std::vector<ZipEntry> entries;
    if (!read_zip(bytes.data(), bytes.size(), entries, error)) {
        return false;
    }
    // aapt2 already decided which entries must stay uncompressed.
    for (ZipEntry& entry : entries) {
        entry.store = true;
    }
    ZipEntry dex_entry;
    dex_entry.name = "classes.dex";
    dex_entry.store = true;
    if (!read_file_bytes(dex, dex_entry.bytes, error)) {
        return false;
    }
    entries.push_back(std::move(dex_entry));
    for (const std::filesystem::path& library : libraries) {
        ZipEntry entry;
        entry.name = "lib/" + abi + "/" + library.filename().string();
        entry.store = true;  // the loader maps them, so they cannot be deflated
        if (!read_file_bytes(library, entry.bytes, error)) {
            return false;
        }
        entries.push_back(std::move(entry));
    }
    std::vector<std::pair<std::string, std::filesystem::path>> payload_files;
    if (!collect_tree(payload, android_asset_entry(""), payload_files, error)) {
        return false;
    }
    if (payload_files.empty()) {
        error = "the payload is empty";
        return false;
    }
    for (const auto& item : payload_files) {
        ZipEntry entry;
        entry.name = item.first;
        if (!read_file_bytes(item.second, entry.bytes, error)) {
            return false;
        }
        entries.push_back(std::move(entry));
    }
    return write_zip(out, entries, error);
}

struct SigningKey {
    std::filesystem::path keystore;
    std::string alias;
    std::string store_password;
    std::string key_password;
    bool development = false;
};

// A release key comes from the project and its passwords come from the
// environment, never from the project file. Without one the package still
// installs, signed with a development key kept beside the project.
bool resolve_signing(const Project& decl, const std::filesystem::path& project, const Tools& tools,
                     const std::filesystem::path& work, SigningKey& key, std::string& error) {
    if (!decl.android.keystore.empty()) {
        key.keystore = project / std::filesystem::path(decl.android.keystore,
                                                       std::filesystem::path::generic_format);
        if (!is_file(key.keystore)) {
            error = "missing android keystore " + decl.android.keystore;
            return false;
        }
        key.alias = decl.android.key_alias.empty() ? "release" : decl.android.key_alias;
        key.store_password = env_string("ZUI_KEYSTORE_PASSWORD");
        key.key_password = env_string("ZUI_KEY_PASSWORD");
        if (key.store_password.empty()) {
            error = "set ZUI_KEYSTORE_PASSWORD to sign with " + decl.android.keystore;
            return false;
        }
        if (key.key_password.empty()) {
            key.key_password = key.store_password;
        }
        return true;
    }
    key.development = true;
    key.keystore = project / ".zui" / "android" / "debug.jks";
    key.alias = "zui";
    key.store_password = "android";
    key.key_password = "android";
    if (is_file(key.keystore)) {
        return true;
    }
    std::error_code ec;
    std::filesystem::create_directories(key.keystore.parent_path(), ec);
    const std::vector<std::string> args = command(
        tools.keytool.string(), "-genkeypair", "-keystore", key.keystore.string(), "-storepass",
        key.store_password, "-keypass", key.key_password, "-alias", key.alias, "-keyalg", "RSA",
        "-keysize", "2048", "-validity", "10000", "-dname", "CN=zui development");
    if (run_process(args) != 0 || !is_file(key.keystore)) {
        error = "cannot create the development signing key " + key.keystore.string();
        return false;
    }
    std::cout << "zui-pack: signed with a development key; set android.keystore and "
                 "ZUI_KEYSTORE_PASSWORD for a release build"
              << std::endl;
    (void)work;
    return true;
}

bool sign_apk(const Tools& tools, const SigningKey& key, const Project& decl,
              const std::filesystem::path& aligned, const std::filesystem::path& out,
              std::string& error) {
    std::vector<std::string> sign;
    if (is_file(tools.apksigner_jar)) {
        sign = command(tools.java.string(), "-jar", tools.apksigner_jar.string());
    } else {
        sign = command(tools.apksigner.string());
    }
    sign.push_back("sign");
    sign.push_back("--ks");
    sign.push_back(key.keystore.string());
    sign.push_back("--ks-key-alias");
    sign.push_back(key.alias);
    sign.push_back("--ks-pass");
    sign.push_back("pass:" + key.store_password);
    sign.push_back("--key-pass");
    sign.push_back("pass:" + key.key_password);
    sign.push_back("--min-sdk-version");
    sign.push_back(std::to_string(decl.android.min_sdk));
    sign.push_back("--out");
    sign.push_back(out.string());
    sign.push_back(aligned.string());
    if (run_process(sign) != 0 || !is_file(out)) {
        error = "apksigner failed";
        return false;
    }
    return true;
}

}  // namespace

int package_android(const std::filesystem::path& project, const std::filesystem::path& out_dir,
                    const std::filesystem::path& root, const std::filesystem::path& build) {
    std::cout << "zui-pack: android package " << project.string() << std::endl;
    if (!is_dir(project)) {
        std::cerr << "zui-pack: project directory does not exist\n";
        return 1;
    }
    Project decl;
    std::string error;
    if (!ProjectWriter::read(project / kDeclFile, decl, error)) {
        std::cerr << "zui-pack: " << error << "\n";
        return 1;
    }
    if (!has_target(decl, "android")) {
        std::cerr << "zui-pack: zui.json does not select android\n";
        return 1;
    }
    if (!ProjectWriter::require_inputs(project, decl, error)) {
        std::cerr << "zui-pack: " << error << "\n";
        return 1;
    }
    for (const std::string& note : ProjectWriter::unshipped_references(project, decl)) {
        std::cout << "zui-pack: warning: " << note << std::endl;
    }
    int version_code = 0;
    if (!android_version_code(decl.version, version_code, error)) {
        std::cerr << "zui-pack: " << error << "\n";
        return 1;
    }
    Tools tools;
    if (!find_tools(tools, error)) {
        std::cerr << "zui-pack: " << error << "\n";
        return 1;
    }
    const std::filesystem::path dist = out_dir.empty() ? project / "dist" : out_dir;
    const std::filesystem::path apk = dist / (decl.name + "-" + decl.version + ".apk");
    std::error_code ec;
    std::filesystem::remove(apk, ec);

    const std::filesystem::path zui_so = find_android_zui(root, build);
    if (zui_so.empty()) {
        std::cerr << "zui-pack: missing the android build of zui (libzui.so)\n";
        return 1;
    }
    // The build that holds libzui.so is the one whose headers and generated
    // files the app compiles against.
    const std::filesystem::path android_build = zui_so.parent_path();
    std::cout << "zui-pack: android build " << android_build.string() << std::endl;
    const std::filesystem::path work = project / ".zui" / "android" / "work";
    remove_tree(work);
    std::filesystem::create_directories(work, ec);
    if (ec) {
        std::cerr << "zui-pack: cannot create " << work.string() << "\n";
        return 1;
    }
    const int api = decl.android.min_sdk;
    std::filesystem::path app_library;
    if (!build_app_library(decl, project, root, android_build, tools, work, api, app_library,
                           error)) {
        std::cerr << "zui-pack: " << error << "\n";
        remove_tree(work);
        return 1;
    }
    std::filesystem::path bridge;
    if (!build_bridge(tools, work, api, bridge, error)) {
        std::cerr << "zui-pack: " << error << "\n";
        remove_tree(work);
        return 1;
    }
    std::vector<std::filesystem::path> libraries = native_libraries(zui_so);
    for (std::filesystem::path& library : libraries) {
        if (!tools.llvm_strip.empty()) {
            const std::filesystem::path stripped = work / "lib" / tools.abi /
                                                   library.filename();
            std::filesystem::create_directories(stripped.parent_path(), ec);
            std::filesystem::copy_file(library, stripped,
                                       std::filesystem::copy_options::overwrite_existing, ec);
            if (!ec) {
                run_process(command(tools.llvm_strip.string(), "--strip-unneeded",
                                    stripped.string()));
                library = stripped;
            }
        }
    }
    libraries.push_back(app_library);
    libraries.push_back(bridge);

    std::filesystem::path dex;
    if (!compile_java(tools, root, decl, work, dex, error)) {
        std::cerr << "zui-pack: " << error << "\n";
        remove_tree(work);
        return 1;
    }
    // The payload keeps the paths the project declares: the page, the icon, and
    // the runtime files, at the top of the unpacked directory.
    const std::filesystem::path payload = work / "payload";
    std::vector<std::pair<std::string, std::filesystem::path>> runtime_files;
    if (!ProjectWriter::collect_runtime_files(project, decl, runtime_files, error)) {
        std::cerr << "zui-pack: " << error << "\n";
        remove_tree(work);
        return 1;
    }
    std::filesystem::remove_all(payload, ec);
    const std::filesystem::path page = project / std::filesystem::path(
                                                     decl.page, std::filesystem::path::generic_format);
    const std::filesystem::path page_target =
        payload / std::filesystem::path(decl.page, std::filesystem::path::generic_format);
    std::filesystem::create_directories(page_target.parent_path(), ec);
    std::filesystem::copy_file(page, page_target,
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        std::cerr << "zui-pack: cannot copy " << decl.page << " into the payload\n";
        remove_tree(work);
        return 1;
    }
    for (const auto& item : runtime_files) {
        if (item.first == decl.page) {
            continue;
        }
        const std::filesystem::path target =
            payload / std::filesystem::path(item.first, std::filesystem::path::generic_format);
        std::filesystem::create_directories(target.parent_path(), ec);
        std::error_code copy_ec;
        std::filesystem::copy_file(item.second, target,
                                   std::filesystem::copy_options::overwrite_existing, copy_ec);
        if (copy_ec) {
            std::cerr << "zui-pack: cannot copy " << item.first << " into the payload\n";
            remove_tree(work);
            return 1;
        }
    }

    std::filesystem::path base_apk;
    if (!link_resources(tools, project, decl, work, version_code, base_apk, error)) {
        std::cerr << "zui-pack: " << error << "\n";
        remove_tree(work);
        return 1;
    }
    const std::filesystem::path unsigned_apk = work / "unsigned.apk";
    if (!assemble_apk(base_apk, dex, libraries, payload, tools.abi, unsigned_apk, error)) {
        std::cerr << "zui-pack: " << error << "\n";
        remove_tree(work);
        return 1;
    }
    const std::filesystem::path aligned_apk = work / "aligned.apk";
    if (run_process(command(tools.zipalign.string(), "-f", "-p", "4", unsigned_apk.string(),
                            aligned_apk.string())) != 0 ||
        !is_file(aligned_apk)) {
        std::cerr << "zui-pack: zipalign failed\n";
        remove_tree(work);
        return 1;
    }
    SigningKey key;
    if (!resolve_signing(decl, project, tools, work, key, error)) {
        std::cerr << "zui-pack: " << error << "\n";
        remove_tree(work);
        return 1;
    }
    std::filesystem::create_directories(apk.parent_path(), ec);
    if (!sign_apk(tools, key, decl, aligned_apk, apk, error)) {
        std::cerr << "zui-pack: " << error << "\n";
        std::filesystem::remove(apk, ec);
        remove_tree(work);
        return 1;
    }
    remove_tree(work);
    std::cout << "zui-pack: apk " << apk.string() << std::endl;
    return 0;
}

class AndroidPacker final : public Packer {
public:
    const char* platform() const override { return "android"; }

    int pack(const PackContext& ctx) override {
        return package_android(ctx.project, ctx.out, ctx.root, ctx.build);
    }
};

std::unique_ptr<Packer> make_android_packer() { return std::make_unique<AndroidPacker>(); }

}  // namespace zui_tool
