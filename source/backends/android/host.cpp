#include "backends/mobile_device.h"
#include "backends/permission_policy.h"
#include "backends/posix_platform_util.h"

#include <jni.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace zui {
namespace backends {

PermissionState permission_state_from_java(int value);

namespace {

namespace fs = std::filesystem;
using posix_util::effective_app_id;
using posix_util::ensure_dir;
using posix_util::uri_scheme_allowed;

constexpr int kReqOpen = 0x7A01;
constexpr int kReqSave = 0x7A02;
constexpr int kReqFolder = 0x7A03;

JavaVM* g_vm = nullptr;
jclass g_host_class = nullptr;

struct JniFrame {
    JNIEnv* env = nullptr;
    bool open = false;

    bool attached = false;

    explicit JniFrame(int capacity = 32) {
        if (g_vm == nullptr) {
            return;
        }
        const jint state = g_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
        if (state == JNI_EDETACHED) {
            if (g_vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
                env = nullptr;
                return;
            }
            attached = true;
        } else if (state != JNI_OK || env == nullptr) {
            env = nullptr;
            return;
        }
        if (env->PushLocalFrame(capacity) != 0) {
            if (attached) {
                g_vm->DetachCurrentThread();
                attached = false;
            }
            env = nullptr;
            return;
        }
        open = true;
    }

    ~JniFrame() {
        if (open && env != nullptr) {
            env->PopLocalFrame(nullptr);
        }
        if (attached && g_vm != nullptr) {
            g_vm->DetachCurrentThread();
        }
    }

    JniFrame(const JniFrame&) = delete;
    JniFrame& operator=(const JniFrame&) = delete;
};

bool check(JNIEnv* env) {
    if (env == nullptr) {
        return false;
    }
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    return true;
}

std::string jstring_to_utf(JNIEnv* env, jstring value) {
    if (value == nullptr) {
        return {};
    }
    const char* utf = env->GetStringUTFChars(value, nullptr);
    if (utf == nullptr) {
        return {};
    }
    std::string out(utf);
    env->ReleaseStringUTFChars(value, utf);
    return out;
}

jstring to_jstring(JNIEnv* env, std::string_view text) {
    const std::string owned(text);
    return env->NewStringUTF(owned.c_str());
}

void finish_begin(int kind_request, int code) {
    if (code >= 0) {
        return;
    }
    if (kind_request == kReqOpen) {
        mobile_complete_pick_open({PickStatus::Unsupported, {}});
    } else if (kind_request == kReqSave) {
        mobile_complete_pick_save({PickStatus::Unsupported, {}});
    } else {
        mobile_complete_pick_folder({PickStatus::Unsupported, {}});
    }
}

std::string mime_for_extension(std::string_view ext) {
    if (ext == "txt" || ext == "md" || ext == "csv") {
        return "text/plain";
    }
    if (ext == "html" || ext == "htm") {
        return "text/html";
    }
    if (ext == "json") {
        return "application/json";
    }
    if (ext == "png") {
        return "image/png";
    }
    if (ext == "jpg" || ext == "jpeg") {
        return "image/jpeg";
    }
    if (ext == "gif") {
        return "image/gif";
    }
    if (ext == "pdf") {
        return "application/pdf";
    }
    if (ext == "svg") {
        return "image/svg+xml";
    }
    return "application/octet-stream";
}

std::vector<std::string> mime_list(const FileDialogOptions& opt) {
    std::vector<std::string> mimes;
    for (const FileFilter& filter : opt.filters) {
        std::size_t start = 0;
        while (start <= filter.patterns.size()) {
            const std::size_t semi = filter.patterns.find(';', start);
            std::string part = filter.patterns.substr(
                start, semi == std::string::npos ? std::string::npos : semi - start);
            if (!part.empty() && part != "*") {
                mimes.push_back(mime_for_extension(part));
            }
            if (semi == std::string::npos) {
                break;
            }
            start = semi + 1;
        }
    }
    return mimes;
}

jobjectArray mime_array(JNIEnv* env, const std::vector<std::string>& mimes) {
    jclass string_class = env->FindClass("java/lang/String");
    if (string_class == nullptr) {
        return nullptr;
    }
    jobjectArray array = env->NewObjectArray(static_cast<jsize>(mimes.size()), string_class, nullptr);
    if (array == nullptr) {
        return nullptr;
    }
    for (jsize i = 0; i < static_cast<jsize>(mimes.size()); ++i) {
        jstring item = to_jstring(env, mimes[static_cast<std::size_t>(i)]);
        env->SetObjectArrayElement(array, i, item);
    }
    return array;
}

std::string sandbox_or_temp(const char* java_method, const char* temp_leaf, std::string_view app_id) {
    const std::string id = effective_app_id(app_id);
    JniFrame frame;
    if (frame.open && g_host_class != nullptr) {
        jmethodID method =
            frame.env->GetStaticMethodID(g_host_class, java_method, "()Ljava/lang/String;");
        if (check(frame.env) && method != nullptr) {
            jstring value = static_cast<jstring>(
                frame.env->CallStaticObjectMethod(g_host_class, method));
            if (check(frame.env) && value != nullptr) {
                const std::string root = jstring_to_utf(frame.env, value);
                if (!root.empty()) {
                    return ensure_dir(fs::path(root) / "zui" / id);
                }
            }
        }
    }
    return ensure_dir(fs::temp_directory_path() / temp_leaf / id);
}

class AndroidPlatform final : public IPlatform {
public:
    PickPaths pick_open(NativeWindowId, const FileDialogOptions&) override {
        PickPaths injected;
        if (mobile_consume_injected_open(injected)) {
            return injected;
        }
        return {PickStatus::Unsupported, {}};
    }

    PickPath pick_save(NativeWindowId, const FileDialogOptions&) override {
        PickPath injected;
        if (mobile_consume_injected_save(injected)) {
            return injected;
        }
        return {PickStatus::Unsupported, {}};
    }

    PickPath pick_folder(NativeWindowId, const FileDialogOptions&) override {
        PickPath injected;
        if (mobile_consume_injected_folder(injected)) {
            return injected;
        }
        return {PickStatus::Unsupported, {}};
    }

    bool picks_are_async() const override { return true; }

    void begin_pick_open(NativeWindowId, const FileDialogOptions& opt) override {
        JniFrame frame;
        if (!frame.open || g_host_class == nullptr) {
            return;
        }
        const std::vector<std::string> mimes = mime_list(opt);
        jobjectArray array = mime_array(frame.env, mimes);
        if (!check(frame.env)) {
            return;
        }
        jmethodID method =
            frame.env->GetStaticMethodID(g_host_class, "openDocument", "(Z[Ljava/lang/String;)I");
        if (!check(frame.env) || method == nullptr) {
            return;
        }
        const jint code = frame.env->CallStaticIntMethod(g_host_class, method,
                                                         opt.multi ? JNI_TRUE : JNI_FALSE, array);
        if (!check(frame.env)) {
            finish_begin(kReqOpen, -1);
            return;
        }
        finish_begin(kReqOpen, static_cast<int>(code));
    }

    void begin_pick_save(NativeWindowId, const FileDialogOptions& opt) override {
        JniFrame frame;
        if (!frame.open || g_host_class == nullptr) {
            return;
        }
        std::string mime = "application/octet-stream";
        const std::vector<std::string> mimes = mime_list(opt);
        if (!mimes.empty()) {
            mime = mimes.front();
        }
        jstring mime_j = to_jstring(frame.env, mime);
        jstring title_j = to_jstring(frame.env, opt.default_name);
        jmethodID method = frame.env->GetStaticMethodID(
            g_host_class, "createDocument", "(Ljava/lang/String;Ljava/lang/String;)I");
        if (!check(frame.env) || method == nullptr) {
            return;
        }
        const jint code =
            frame.env->CallStaticIntMethod(g_host_class, method, mime_j, title_j);
        if (!check(frame.env)) {
            finish_begin(kReqSave, -1);
            return;
        }
        finish_begin(kReqSave, static_cast<int>(code));
    }

    void begin_pick_folder(NativeWindowId, const FileDialogOptions&) override {
        JniFrame frame;
        if (!frame.open || g_host_class == nullptr) {
            return;
        }
        jmethodID method = frame.env->GetStaticMethodID(g_host_class, "openTree", "()I");
        if (!check(frame.env) || method == nullptr) {
            return;
        }
        const jint code = frame.env->CallStaticIntMethod(g_host_class, method);
        if (!check(frame.env)) {
            finish_begin(kReqFolder, -1);
            return;
        }
        finish_begin(kReqFolder, static_cast<int>(code));
    }

    bool open_uri(std::string_view uri) override {
        if (!uri_scheme_allowed(uri)) {
            return false;
        }
        JniFrame frame;
        if (!frame.open || g_host_class == nullptr) {
            return false;
        }
        jstring text = to_jstring(frame.env, uri);
        jmethodID method =
            frame.env->GetStaticMethodID(g_host_class, "openUri", "(Ljava/lang/String;)Z");
        if (!check(frame.env) || method == nullptr) {
            return false;
        }
        const jboolean ok =
            frame.env->CallStaticBooleanMethod(g_host_class, method, text);
        return check(frame.env) && ok == JNI_TRUE;
    }

    std::string user_data_dir(std::string_view app_id) const override {
        return sandbox_or_temp("filesDir", "zui-android-data", app_id);
    }

    std::string cache_dir(std::string_view app_id) const override {
        return sandbox_or_temp("cacheDirPath", "zui-android-cache", app_id);
    }

    PermissionState permission_state(std::string_view name) const override {
        JniFrame frame;
        if (!frame.open || g_host_class == nullptr) {
            return PermissionState::Unsupported;
        }
        jmethodID method = frame.env->GetStaticMethodID(g_host_class, "permissionState",
                                                        "(Ljava/lang/String;)I");
        if (!check(frame.env) || method == nullptr) {
            return PermissionState::Unsupported;
        }
        jstring text = to_jstring(frame.env, name);
        const jint value = frame.env->CallStaticIntMethod(g_host_class, method, text);
        if (!check(frame.env)) {
            return PermissionState::Unsupported;
        }
        return zui::backends::permission_state_from_java(static_cast<int>(value));
    }

    bool permissions_are_async() const override { return true; }

    void begin_permission_request(std::string_view name) override {
        JniFrame frame;
        if (!frame.open || g_host_class == nullptr) {
            mobile_complete_permission(name, PermissionState::Unsupported);
            return;
        }
        auto call_bool = [&](const char* method_name) -> int {
            jmethodID method = frame.env->GetStaticMethodID(g_host_class, method_name,
                                                            "(Ljava/lang/String;)Z");
            if (!check(frame.env) || method == nullptr) {
                return -1;
            }
            jstring text = to_jstring(frame.env, name);
            const jboolean value = frame.env->CallStaticBooleanMethod(g_host_class, method, text);
            if (!check(frame.env)) {
                return -1;
            }
            return value == JNI_TRUE ? 1 : 0;
        };
        const int declared = call_bool("isDeclared");
        jmethodID sdk_method = frame.env->GetStaticMethodID(g_host_class, "sdkInt", "()I");
        int api = 0;
        if (check(frame.env) && sdk_method != nullptr) {
            api = static_cast<int>(frame.env->CallStaticIntMethod(g_host_class, sdk_method));
            if (!check(frame.env)) {
                api = 0;
            }
        }
        if (declared < 0) {
            mobile_complete_permission(name, PermissionState::Unsupported);
            return;
        }
        const PermissionState now = permission_state(name);
        const PermissionAsk decision = permission_ask(name, declared == 1, api, now);
        if (decision.kind == PermissionAskKind::Immediate) {
            mobile_complete_permission(name, decision.state);
            return;
        }
        const char* action = decision.kind == PermissionAskKind::Dialog ? "showRuntimeDialog"
                                                                        : "openPermissionSettings";
        // A started dialog or settings page answers through nativePermissionResult.
        if (call_bool(action) != 1) {
            mobile_complete_permission(name, permission_state(name));
        }
    }
};

void deliver_paths(int request, bool ok, std::vector<std::string> paths) {
    if (!ok || paths.empty()) {
        if (request == kReqOpen) {
            mobile_complete_pick_open({PickStatus::Cancelled, {}});
        } else if (request == kReqSave) {
            mobile_complete_pick_save({PickStatus::Cancelled, {}});
        } else if (request == kReqFolder) {
            mobile_complete_pick_folder({PickStatus::Cancelled, {}});
        }
        return;
    }
    if (request == kReqOpen) {
        mobile_complete_pick_open({PickStatus::Ok, std::move(paths)});
        return;
    }
    if (request == kReqSave) {
        mobile_complete_pick_save({PickStatus::Ok, std::move(paths.front())});
        return;
    }
    if (request == kReqFolder) {
        mobile_complete_pick_folder({PickStatus::Ok, std::move(paths.front())});
    }
}

}  // namespace

PermissionState permission_state_from_java(int value) {
    switch (value) {
    case 1:
        return PermissionState::NotRequested;
    case 2:
        return PermissionState::Granted;
    case 3:
        return PermissionState::Denied;
    case 4:
        return PermissionState::Blocked;
    default:
        return PermissionState::Unsupported;
    }
}

void bind_java_vm(JavaVM* vm, JNIEnv* env) {
    if (g_vm == nullptr) {
        g_vm = vm;
    }
    if (g_host_class == nullptr && env != nullptr) {
        jclass local = env->FindClass("zui/ZuiHost");
        if (local != nullptr) {
            g_host_class = static_cast<jclass>(env->NewGlobalRef(local));
            env->DeleteLocalRef(local);
        }
    }
}

void deliver_pick_from_java(int request, bool ok, std::vector<std::string> paths) {
    deliver_paths(request, ok, std::move(paths));
}

std::unique_ptr<IPlatform> create_android_platform_device() {
    return std::make_unique<AndroidPlatform>();
}

bool device_apply_host_media(HostMediaFacts& facts) {
    facts.hover = false;
    facts.any_hover = false;
    facts.pointer = PointerAccuracy::Coarse;
    facts.any_pointer = PointerAccuracy::Coarse;
    facts.reduced_motion = false;
    facts.color_scheme = ColorSchemePreference::Light;
    JniFrame frame;
    if (!frame.open || g_host_class == nullptr) {
        return true;
    }
    jmethodID night = frame.env->GetStaticMethodID(g_host_class, "nightMode", "()I");
    if (check(frame.env) && night != nullptr) {
        const jint mode = frame.env->CallStaticIntMethod(g_host_class, night);
        if (check(frame.env)) {
            if (mode == 2) {
                facts.color_scheme = ColorSchemePreference::Dark;
            } else if (mode == 1) {
                facts.color_scheme = ColorSchemePreference::Light;
            }
        }
    }
    jmethodID motion =
        frame.env->GetStaticMethodID(g_host_class, "animationsDisabled", "()Z");
    if (check(frame.env) && motion != nullptr) {
        const jboolean off = frame.env->CallStaticBooleanMethod(g_host_class, motion);
        if (check(frame.env)) {
            facts.reduced_motion = off == JNI_TRUE;
        }
    }
    return true;
}

float device_keyboard_bottom() {
    JniFrame frame;
    if (!frame.open || g_host_class == nullptr) {
        return 0.f;
    }
    jmethodID method = frame.env->GetStaticMethodID(g_host_class, "keyboardBottomCss", "()F");
    if (!check(frame.env) || method == nullptr) {
        return 0.f;
    }
    const jfloat bottom = frame.env->CallStaticFloatMethod(g_host_class, method);
    if (!check(frame.env) || bottom < 0.f) {
        return 0.f;
    }
    return static_cast<float>(bottom);
}

void device_install_keyboard_observer() {}

ClipboardText device_clipboard_text() {
    ClipboardText out;
    JniFrame frame;
    if (!frame.open || g_host_class == nullptr) {
        return out;
    }
    jmethodID has = frame.env->GetStaticMethodID(g_host_class, "hasClipboard", "()Z");
    if (!check(frame.env) || has == nullptr) {
        return out;
    }
    const jboolean available = frame.env->CallStaticBooleanMethod(g_host_class, has);
    if (!check(frame.env) || available != JNI_TRUE) {
        return out;
    }
    jmethodID text = frame.env->GetStaticMethodID(g_host_class, "clipboardText", "()Ljava/lang/String;");
    if (!check(frame.env) || text == nullptr) {
        return out;
    }
    jstring value = static_cast<jstring>(frame.env->CallStaticObjectMethod(g_host_class, text));
    if (!check(frame.env)) {
        return out;
    }
    out.status = ClipboardStatus::Ok;
    out.text = jstring_to_utf(frame.env, value);
    return out;
}

bool device_set_clipboard_text(std::string_view text) {
    JniFrame frame;
    if (!frame.open || g_host_class == nullptr) {
        return false;
    }
    jstring value = to_jstring(frame.env, text);
    jmethodID method =
        frame.env->GetStaticMethodID(g_host_class, "setClipboardText", "(Ljava/lang/String;)Z");
    if (!check(frame.env) || method == nullptr) {
        return false;
    }
    const jboolean ok = frame.env->CallStaticBooleanMethod(g_host_class, method, value);
    return check(frame.env) && ok == JNI_TRUE;
}

}  // namespace backends
}  // namespace zui

extern "C" JNIEXPORT void JNICALL Java_zui_ZuiHost_nativeSetActivity(JNIEnv* env, jclass,
                                                                    jobject activity) {
    (void)activity;
    JavaVM* vm = nullptr;
    if (env->GetJavaVM(&vm) != JNI_OK) {
        return;
    }
    zui::backends::bind_java_vm(vm, env);
}

extern "C" JNIEXPORT void JNICALL Java_zui_ZuiHost_nativePickFinished(JNIEnv* env, jclass,
                                                                     jint request, jboolean ok,
                                                                     jobjectArray paths) {
    std::vector<std::string> out;
    if (paths != nullptr) {
        const jsize n = env->GetArrayLength(paths);
        out.reserve(static_cast<std::size_t>(n));
        for (jsize i = 0; i < n; ++i) {
            jstring item = static_cast<jstring>(env->GetObjectArrayElement(paths, i));
            if (item == nullptr) {
                continue;
            }
            const char* utf = env->GetStringUTFChars(item, nullptr);
            if (utf != nullptr) {
                if (utf[0] != '\0') {
                    out.emplace_back(utf);
                }
                env->ReleaseStringUTFChars(item, utf);
            }
            env->DeleteLocalRef(item);
        }
    }
    zui::backends::deliver_pick_from_java(static_cast<int>(request), ok == JNI_TRUE, std::move(out));
}

namespace zui {
void zui_plugin_pick_android_complete(const std::vector<std::string>& paths);
}

extern "C" JNIEXPORT void JNICALL Java_zui_ZuiHost_nativePluginPickFinished(JNIEnv* env, jclass, jboolean ok,
                                                                           jobjectArray paths) {
    std::vector<std::string> out;
    if (ok == JNI_TRUE && paths != nullptr) {
        const jsize n = env->GetArrayLength(paths);
        out.reserve(static_cast<std::size_t>(n));
        for (jsize i = 0; i < n; ++i) {
            jstring item = static_cast<jstring>(env->GetObjectArrayElement(paths, i));
            if (item == nullptr) {
                continue;
            }
            const char* utf = env->GetStringUTFChars(item, nullptr);
            if (utf != nullptr) {
                if (utf[0] != '\0') {
                    out.emplace_back(utf);
                }
                env->ReleaseStringUTFChars(item, utf);
            }
            env->DeleteLocalRef(item);
        }
    }
    zui::zui_plugin_pick_android_complete(out);
}

extern "C" JNIEXPORT void JNICALL Java_zui_ZuiHost_nativePermissionResult(JNIEnv* env, jclass,
                                                                         jstring name,
                                                                         jint state) {
    std::string text;
    if (name != nullptr) {
        const char* utf = env->GetStringUTFChars(name, nullptr);
        if (utf != nullptr) {
            text.assign(utf);
            env->ReleaseStringUTFChars(name, utf);
        }
    }
    zui::backends::mobile_complete_permission(text, zui::backends::permission_state_from_java(state));
}
