#include "backends/platform.h"
#include "backends/renderer.h"

#include "log/log.h"

#include <cstdlib>
#include <cstring>
#include <string>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace zui {
namespace backends {

#ifdef ZUI_WITH_SDL
std::unique_ptr<IWindow> create_sdl3_window();
#endif
#ifdef ZUI_WITH_GLFW
std::unique_ptr<IWindow> create_glfw3_window();
#endif
std::unique_ptr<IWindow> create_ios_window();
std::unique_ptr<IWindow> create_android_window();
std::unique_ptr<IRenderer> create_gl_renderer();
std::unique_ptr<IRenderer> create_gles_renderer();
#ifdef ZUI_HAS_VULKAN
std::unique_ptr<IRenderer> create_vk_renderer();
#endif

std::unique_ptr<IPlatform> create_platform(std::string_view kind) {
    std::string k(kind);
    if (k.empty()) {
        const char* env = std::getenv("ZUI_PLATFORM");
        if (env != nullptr && env[0] != '\0') {
            k = env;
        } else {
#if defined(__ANDROID__)
            k = "android";
#elif defined(__APPLE__)
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
            k = "ios";
#else
            k = "macos";
#endif
#elif defined(_WIN32)
            k = "win32";
#elif defined(__linux__)
            k = "linux";
#else
            k = "noop";
#endif
        }
    }
    if (k == "noop") {
        return create_noop_platform();
    }
    if (k == "native") {
#if defined(__ANDROID__)
        return create_android_platform();
#elif defined(__APPLE__)
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
        return create_ios_platform();
#else
        return create_macos_platform();
#endif
#elif defined(_WIN32)
        return create_win32_platform();
#elif defined(__linux__)
        return create_linux_platform();
#else
        return create_noop_platform();
#endif
    }
    if (k == "win32") {
        return create_win32_platform();
    }
    if (k == "linux") {
        return create_linux_platform();
    }
    if (k == "macos" || k == "osx" || k == "darwin") {
        return create_macos_platform();
    }
    if (k == "ios") {
        return create_ios_platform();
    }
    if (k == "android") {
        return create_android_platform();
    }
    return create_noop_platform();
}

GfxApi gfx_api_from_renderer(std::string_view kind) {
    if (kind == "gl") {
        return GfxApi::OpenGL;
    }
    if (kind == "gles") {
        return GfxApi::OpenGLES;
    }
    return GfxApi::Vulkan;
}

std::unique_ptr<IWindow> create_window(std::string_view kind) {
    std::string k(kind);
    if (k.empty()) {
        const char* env = std::getenv("ZUI_WINDOW");
        k = env ? env : "sdl3";
    }
    // On a device both mobile platforms render through SDL, which owns the
    // surface: the ios/android factory entries are the headless stubs a desktop
    // build of those kinds gets.
    if (k == "ios" || k == "android") {
#if defined(ZUI_WITH_SDL) && (defined(__ANDROID__) || defined(__APPLE__))
        return create_sdl3_window();
#else
        return k == "ios" ? create_ios_window() : create_android_window();
#endif
    }
    if (k == "glfw3" || k == "glfw") {
#ifdef ZUI_WITH_GLFW
        return create_glfw3_window();
#else
        ZUI_ERROR("zui: GLFW not built");
        return nullptr;
#endif
    }
    if (k == "sdl3" || k == "sdl") {
#ifdef ZUI_WITH_SDL
        return create_sdl3_window();
#else
        ZUI_ERROR("zui: SDL not built");
        return nullptr;
#endif
    }
#ifdef ZUI_WITH_SDL
    ZUI_WARN("zui: unknown window '{}', using sdl3", k);
    return create_sdl3_window();
#else
#ifdef ZUI_WITH_GLFW
    ZUI_WARN("zui: unknown window '{}', using glfw3", k);
    return create_glfw3_window();
#else
    ZUI_ERROR("zui: no window backend");
    return nullptr;
#endif
#endif
}

std::unique_ptr<IRenderer> create_renderer(std::string_view kind) {
    std::string k(kind);
    if (k.empty()) {
        const char* env = std::getenv("ZUI_RENDERER");
        k = env ? env : "";
    }
    if (k == "gl") {
        return create_gl_renderer();
    }
    if (k == "gles") {
        return create_gles_renderer();
    }
#ifdef ZUI_HAS_VULKAN
    if (k.empty() || k == "vk" || k == "vulkan") {
        return create_vk_renderer();
    }
#endif
    if (k.empty() || k == "vk" || k == "vulkan") {
        ZUI_WARN("zui: Vulkan not built, using OpenGL");
        return create_gl_renderer();
    }
    ZUI_WARN("zui: unknown renderer '{}', using OpenGL", k);
    return create_gl_renderer();
}

}  // namespace backends
}  // namespace zui
