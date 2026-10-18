#include "backends/posix_platform_util.h"
#include "ext/slots.h"
#include "plugins/android_jni.h"
#include "plugins/shell/layout.h"

#include <filesystem>
#include <string>

namespace zui {
namespace backends {
namespace {

namespace fs = std::filesystem;
using posix_util::effective_app_id;
using posix_util::ensure_dir;
using posix_util::uri_scheme_allowed;

#if defined(__ANDROID__)

std::string java_dir(const char* method) {
    android_jni::Frame frame;
    if (frame.failed()) {
        return {};
    }
    JNIEnv* env = frame.env();
    jclass cls = env->GetObjectClass(frame.application());
    jmethodID mid = env->GetMethodID(cls, method, "()Ljava/io/File;");
    jobject file = mid != nullptr ? env->CallObjectMethod(frame.application(), mid) : nullptr;
    if (file == nullptr || frame.failed()) {
        return {};
    }
    jclass file_cls = env->GetObjectClass(file);
    jmethodID path_id = env->GetMethodID(file_cls, "getAbsolutePath", "()Ljava/lang/String;");
    jstring path = path_id != nullptr ? static_cast<jstring>(env->CallObjectMethod(file, path_id)) : nullptr;
    if (path == nullptr || frame.failed()) {
        return {};
    }
    const char* utf = env->GetStringUTFChars(path, nullptr);
    std::string out = utf != nullptr ? std::string(utf) : std::string();
    if (utf != nullptr) {
        env->ReleaseStringUTFChars(path, utf);
    }
    return out;
}

bool open_with_intent(std::string_view uri) {
    android_jni::Frame frame;
    if (frame.failed()) {
        return false;
    }
    JNIEnv* env = frame.env();
    jclass intent_cls = env->FindClass("android/content/Intent");
    jmethodID ctor = intent_cls != nullptr ? env->GetMethodID(intent_cls, "<init>", "(Ljava/lang/String;)V") : nullptr;
    jstring action = frame.jstr("android.intent.action.VIEW");
    jobject intent = ctor != nullptr && action != nullptr ? env->NewObject(intent_cls, ctor, action) : nullptr;
    jclass uri_cls = env->FindClass("android/net/Uri");
    jmethodID parse = uri_cls != nullptr
                          ? env->GetStaticMethodID(uri_cls, "parse", "(Ljava/lang/String;)Landroid/net/Uri;")
                          : nullptr;
    jstring uri_text = frame.jstr(uri);
    jobject parsed = parse != nullptr && uri_text != nullptr ? env->CallStaticObjectMethod(uri_cls, parse, uri_text)
                                                             : nullptr;
    jmethodID set_data = intent_cls != nullptr ? env->GetMethodID(intent_cls, "setData",
                                                                  "(Landroid/net/Uri;)Landroid/content/Intent;")
                                               : nullptr;
    if (intent != nullptr && set_data != nullptr && parsed != nullptr) {
        env->CallObjectMethod(intent, set_data, parsed);
    }
    jmethodID add_flags = intent_cls != nullptr ? env->GetMethodID(intent_cls, "addFlags", "(I)Landroid/content/Intent;")
                                                : nullptr;
    if (intent != nullptr && add_flags != nullptr) {
        env->CallObjectMethod(intent, add_flags, 0x10000000);  // FLAG_ACTIVITY_NEW_TASK
    }
    jclass ctx = env->GetObjectClass(frame.application());
    jmethodID start = ctx != nullptr ? env->GetMethodID(ctx, "startActivity", "(Landroid/content/Intent;)V") : nullptr;
    if (intent == nullptr || start == nullptr || frame.failed()) {
        return false;
    }
    env->CallVoidMethod(frame.application(), start, intent);
    return !frame.failed();
}

#endif

class AndroidShell final : public ShellService {
public:
    bool open_uri(std::string_view uri) override {
        if (!uri_scheme_allowed(uri)) {
            return false;
        }
#if defined(__ANDROID__)
        return open_with_intent(uri);
#else
        return false;
#endif
    }

    std::string user_data_dir(std::string_view app_id) const override {
        const std::string id = effective_app_id(app_id);
#if defined(__ANDROID__)
        shell_layout::Roots roots;
        roots.android_files = java_dir("getFilesDir");
        return ensure_dir(fs::path(shell_layout::android_user_data(roots, id)));
#else
        return ensure_dir(fs::temp_directory_path() / shell_layout::android_desktop_data_leaf() / id);
#endif
    }

    std::string cache_dir(std::string_view app_id) const override {
        const std::string id = effective_app_id(app_id);
#if defined(__ANDROID__)
        shell_layout::Roots roots;
        roots.android_cache = java_dir("getCacheDir");
        return ensure_dir(fs::path(shell_layout::android_cache(roots, id)));
#else
        return ensure_dir(fs::temp_directory_path() / shell_layout::android_desktop_cache_leaf() / id);
#endif
    }
};

}  // namespace

void register_android_shell(ExtensionRegistry& registry) {
    registry.add_shell(PlatformKind::Android, std::make_unique<AndroidShell>());
}

}  // namespace backends
}  // namespace zui
