#include "ext/slots.h"
#include "plugins/android_jni.h"
#include "plugins/pick/command.h"

#include <string>
#include <vector>

namespace zui {
namespace {

std::vector<std::string> g_pick_paths;
bool g_pick_ready = false;

}  // namespace

void zui_plugin_pick_android_complete(const std::vector<std::string>& paths) {
    g_pick_paths = paths;
    g_pick_ready = true;
}

namespace backends {
namespace {

#if defined(__ANDROID__)

constexpr int kPluginOpen = 0x7A21;
constexpr int kPluginSave = 0x7A22;
constexpr int kPluginFolder = 0x7A23;

bool jni_ok(JNIEnv* env) {
    if (env == nullptr || env->ExceptionCheck()) {
        if (env != nullptr) {
            env->ExceptionClear();
        }
        return false;
    }
    return true;
}

bool on_main_thread(JNIEnv* env) {
    jclass looper = env->FindClass("android/os/Looper");
    if (looper == nullptr || !jni_ok(env)) {
        return false;
    }
    jobject mine = env->CallStaticObjectMethod(looper, env->GetStaticMethodID(looper, "myLooper", "()Landroid/os/Looper;"));
    jobject main =
        env->CallStaticObjectMethod(looper, env->GetStaticMethodID(looper, "getMainLooper", "()Landroid/os/Looper;"));
    if (!jni_ok(env) || mine == nullptr || main == nullptr) {
        return false;
    }
    return env->IsSameObject(mine, main) == JNI_TRUE;
}

bool call_host(android_jni::Frame& frame, const char* name) {
    JNIEnv* env = frame.env();
    jclass host = frame.load("zui.ZuiHost");
    jmethodID method = host != nullptr ? env->GetStaticMethodID(host, name, "()V") : nullptr;
    if (method == nullptr || !jni_ok(env)) {
        return false;
    }
    env->CallStaticVoidMethod(host, method);
    return jni_ok(env) || g_pick_ready;
}

bool start_document(const char* action, const FileDialogOptions& opt, bool tree, int request) {
    android_jni::Frame frame;
    if (frame.failed() || !on_main_thread(frame.env())) {
        return false;
    }
    JNIEnv* env = frame.env();
    jclass host = frame.load("zui.ZuiHost");
    jmethodID current = host != nullptr ? env->GetStaticMethodID(host, "currentActivity", "()Landroid/app/Activity;")
                                       : nullptr;
    jobject activity = current != nullptr ? env->CallStaticObjectMethod(host, current) : nullptr;
    if (activity == nullptr || !jni_ok(env)) {
        return false;
    }
    jclass intent_cls = env->FindClass("android/content/Intent");
    jmethodID ctor = intent_cls != nullptr ? env->GetMethodID(intent_cls, "<init>", "(Ljava/lang/String;)V") : nullptr;
    jobject intent = ctor != nullptr ? env->NewObject(intent_cls, ctor, frame.jstr(action)) : nullptr;
    if (intent == nullptr || !jni_ok(env)) {
        return false;
    }
    if (!tree) {
        jmethodID add_category =
            env->GetMethodID(intent_cls, "addCategory", "(Ljava/lang/String;)Landroid/content/Intent;");
        if (add_category == nullptr) {
            return false;
        }
        env->CallObjectMethod(intent, add_category, frame.jstr("android.intent.category.OPENABLE"));
        const std::vector<std::string> mimes = pick_detail::DialogCommand::android_mimes(opt.filters);
        jmethodID set_type = env->GetMethodID(intent_cls, "setType", "(Ljava/lang/String;)Landroid/content/Intent;");
        if (set_type == nullptr || mimes.empty() || !jni_ok(env)) {
            return false;
        }
        env->CallObjectMethod(intent, set_type, frame.jstr(mimes.front()));
        if (mimes.size() > 1) {
            jclass string_cls = env->FindClass("java/lang/String");
            jobjectArray array = env->NewObjectArray(static_cast<jsize>(mimes.size()), string_cls, nullptr);
            for (jsize i = 0; i < static_cast<jsize>(mimes.size()); ++i) {
                env->SetObjectArrayElement(array, i, frame.jstr(mimes[static_cast<std::size_t>(i)]));
            }
            jmethodID put_mimes = env->GetMethodID(intent_cls, "putExtra",
                                                   "(Ljava/lang/String;[Ljava/lang/String;)Landroid/content/Intent;");
            if (put_mimes != nullptr && array != nullptr) {
                env->CallObjectMethod(intent, put_mimes, frame.jstr("android.intent.extra.MIME_TYPES"), array);
            }
        }
        if (!opt.default_name.empty() && std::string(action) == "android.intent.action.CREATE_DOCUMENT") {
            jmethodID put_title =
                env->GetMethodID(intent_cls, "putExtra", "(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;");
            if (put_title != nullptr) {
                env->CallObjectMethod(intent, put_title, frame.jstr("android.intent.extra.TITLE"),
                                     frame.jstr(opt.default_name));
            }
        }
    }
    if (opt.multi && !tree) {
        jmethodID put_extra = env->GetMethodID(intent_cls, "putExtra", "(Ljava/lang/String;Z)Landroid/content/Intent;");
        if (put_extra != nullptr) {
            env->CallObjectMethod(intent, put_extra, frame.jstr("android.intent.extra.ALLOW_MULTIPLE"),
                                 static_cast<jboolean>(JNI_TRUE));
        }
    }
    jint grants = 0x1 | 0x40;
    if (tree || std::string(action) == "android.intent.action.CREATE_DOCUMENT") {
        grants |= 0x2;
    }
    if (tree) {
        grants |= 0x80;
    }
    jmethodID add_flags = env->GetMethodID(intent_cls, "addFlags", "(I)Landroid/content/Intent;");
    if (add_flags != nullptr) {
        env->CallObjectMethod(intent, add_flags, grants);
    }
    if (!jni_ok(env) || !call_host(frame, "beginPluginPick")) {
        return false;
    }
    jmethodID start = env->GetMethodID(env->GetObjectClass(activity), "startActivityForResult",
                                      "(Landroid/content/Intent;I)V");
    if (start == nullptr || !jni_ok(env)) {
        call_host(frame, "cancelPluginPick");
        return false;
    }
    env->CallVoidMethod(activity, start, intent, request);
    if (!jni_ok(env)) {
        call_host(frame, "cancelPluginPick");
        return false;
    }
    call_host(frame, "awaitPluginPick");
    return g_pick_ready;
}

PickPaths take_paths() {
    g_pick_ready = false;
    if (g_pick_paths.empty()) {
        return {PickStatus::Cancelled, {}};
    }
    return {PickStatus::Ok, g_pick_paths};
}

PickPath take_one() {
    g_pick_ready = false;
    if (g_pick_paths.empty()) {
        return {PickStatus::Cancelled, {}};
    }
    return {PickStatus::Ok, g_pick_paths.front()};
}

class AndroidDialogs final : public FileDialogService {
public:
    PickPaths open(NativeWindowId, const FileDialogOptions& opt) override {
        g_pick_ready = false;
        g_pick_paths.clear();
        if (!start_document("android.intent.action.OPEN_DOCUMENT", opt, false, kPluginOpen)) {
            return {PickStatus::Unsupported, {}};
        }
        return take_paths();
    }
    PickPath save(NativeWindowId, const FileDialogOptions& opt) override {
        g_pick_ready = false;
        g_pick_paths.clear();
        if (!start_document("android.intent.action.CREATE_DOCUMENT", opt, false, kPluginSave)) {
            return {PickStatus::Unsupported, {}};
        }
        return take_one();
    }
    PickPath folder(NativeWindowId, const FileDialogOptions& opt) override {
        g_pick_ready = false;
        g_pick_paths.clear();
        if (!start_document("android.intent.action.OPEN_DOCUMENT_TREE", opt, true, kPluginFolder)) {
            return {PickStatus::Unsupported, {}};
        }
        return take_one();
    }
};

#endif

}  // namespace

void register_android_dialogs(ExtensionRegistry& registry) {
#if defined(__ANDROID__)
    registry.add_dialogs(PlatformKind::Android, std::make_unique<AndroidDialogs>());
#else
    (void)registry;
#endif
}

}  // namespace backends
}  // namespace zui
