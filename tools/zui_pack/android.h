#pragma once

#include <filesystem>

namespace zui_tool {

// Drives android/AndroidManifest.xml. Prints "android package" before it looks
// for the SDK, JDK, and the android-arm64 libzui.so. On a missing tool or
// library, exits non-zero and writes no apk.
// out_dir empty writes <project>/dist. Otherwise the packages go there.
// root and build may be empty; the packer also looks beside this executable.
int package_android(const std::filesystem::path& project, const std::filesystem::path& out_dir,
                    const std::filesystem::path& root, const std::filesystem::path& build);

}  // namespace zui_tool
