#include "ext/slots.h"
#include "plugins/android_jni.h"
#include "plugins/tray/item.h"

#include <string>

namespace zui {
namespace {

#if defined(__ANDROID__)

int g_badge = 0;
bool g_badge_set = false;
plugin::Handler g_activate;
jobject g_pending = nullptr;
bool g_receiver_ready = false;

void tray_on_activate(JNIEnv*, jclass) {
    if (g_activate) {
        g_activate();
    }
}

bool post_ongoing(android_jni::Frame& frame, std::string_view text, int number, jobject pending) {
    if (frame.failed()) {
        return false;
    }
    JNIEnv* env = frame.env();
    jclass ctx = env->GetObjectClass(frame.application());
    jobject nm = env->CallObjectMethod(
        frame.application(), env->GetMethodID(ctx, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;"),
        frame.jstr("notification"));
    jclass builder_cls = env->FindClass("android/app/Notification$Builder");
    jobject builder = env->NewObject(
        builder_cls, env->GetMethodID(builder_cls, "<init>", "(Landroid/content/Context;Ljava/lang/String;)V"),
        frame.application(), frame.jstr("zui"));
    env->CallObjectMethod(builder, env->GetMethodID(builder_cls, "setContentText",
                                                    "(Ljava/lang/CharSequence;)Landroid/app/Notification$Builder;"),
                          frame.jstr(text));
    env->CallObjectMethod(builder, env->GetMethodID(builder_cls, "setOngoing", "(Z)Landroid/app/Notification$Builder;"),
                          static_cast<jboolean>(JNI_TRUE));
    env->CallObjectMethod(builder, env->GetMethodID(builder_cls, "setNumber", "(I)Landroid/app/Notification$Builder;"),
                          number);
    if (pending != nullptr) {
        env->CallObjectMethod(builder,
                              env->GetMethodID(builder_cls, "setContentIntent",
                                               "(Landroid/app/PendingIntent;)Landroid/app/Notification$Builder;"),
                              pending);
    }
    jobject note = env->CallObjectMethod(builder, env->GetMethodID(builder_cls, "build", "()Landroid/app/Notification;"));
    env->CallVoidMethod(nm, env->GetMethodID(env->GetObjectClass(nm), "notify", "(ILandroid/app/Notification;)V"), 7,
                        note);
    return !frame.failed();
}

bool ensure_receiver(android_jni::Frame& frame) {
    if (g_receiver_ready && g_pending != nullptr) {
        return true;
    }
    JNIEnv* env = frame.env();
    jclass receiver_cls = frame.load("zui.ZuiTrayReceiver");
    if (receiver_cls == nullptr) {
        return false;
    }
    JNINativeMethod native_method = {const_cast<char*>("nativeOnActivate"), const_cast<char*>("()V"),
                                     reinterpret_cast<void*>(tray_on_activate)};
    if (env->RegisterNatives(receiver_cls, &native_method, 1) != 0) {
        env->ExceptionClear();
        return false;
    }
    jobject receiver = env->NewObject(receiver_cls, env->GetMethodID(receiver_cls, "<init>", "()V"));
    jclass filter_cls = env->FindClass("android/content/IntentFilter");
    jobject filter = filter_cls != nullptr
                         ? env->NewObject(filter_cls, env->GetMethodID(filter_cls, "<init>", "(Ljava/lang/String;)V"),
                                          frame.jstr("zui.TRAY_ACTIVATE"))
                         : nullptr;
    jmethodID register_receiver = env->GetMethodID(
        env->GetObjectClass(frame.application()), "registerReceiver",
        "(Landroid/content/BroadcastReceiver;Landroid/content/IntentFilter;)Landroid/content/Intent;");
    if (receiver == nullptr || filter == nullptr || register_receiver == nullptr || frame.failed()) {
        return false;
    }
    env->CallObjectMethod(frame.application(), register_receiver, receiver, filter);
    if (frame.failed()) {
        return false;
    }
    jclass intent_cls = env->FindClass("android/content/Intent");
    jobject intent = intent_cls != nullptr
                         ? env->NewObject(intent_cls, env->GetMethodID(intent_cls, "<init>", "(Ljava/lang/String;)V"),
                                          frame.jstr("zui.TRAY_ACTIVATE"))
                         : nullptr;
    jmethodID set_package =
        intent_cls != nullptr ? env->GetMethodID(intent_cls, "setPackage", "(Ljava/lang/String;)Landroid/content/Intent;")
                              : nullptr;
    jmethodID package_name =
        env->GetMethodID(env->GetObjectClass(frame.application()), "getPackageName", "()Ljava/lang/String;");
    jobject package = package_name != nullptr ? env->CallObjectMethod(frame.application(), package_name) : nullptr;
    if (intent != nullptr && set_package != nullptr && package != nullptr) {
        env->CallObjectMethod(intent, set_package, package);
    }
    jclass pending_cls = env->FindClass("android/app/PendingIntent");
    jmethodID get_broadcast =
        pending_cls != nullptr
            ? env->GetStaticMethodID(
                  pending_cls, "getBroadcast",
                  "(Landroid/content/Context;ILandroid/content/Intent;I)Landroid/app/PendingIntent;")
            : nullptr;
    constexpr jint immutable = 0x04000000;
    constexpr jint update = 0x08000000;
    jobject pending = get_broadcast != nullptr && intent != nullptr
                          ? env->CallStaticObjectMethod(pending_cls, get_broadcast, frame.application(), 7, intent,
                                                        immutable | update)
                          : nullptr;
    if (pending == nullptr || frame.failed()) {
        return false;
    }
    g_pending = env->NewGlobalRef(pending);
    g_receiver_ready = g_pending != nullptr;
    return g_receiver_ready;
}

class AndroidTray final : public TrayService {
public:
    plugin::OsResult item(std::string_view tip, plugin::Handler on_activate) override {
        plugin::OsResult result;
        const tray_detail::Item prepared = tray_detail::prepare(tip);
        result.title = prepared.tip;
        if (!prepared.ready) {
            result.body = prepared.reason;
            return result;
        }
        if (!on_activate) {
            result.body = "tray activate handler is empty";
            return result;
        }
        g_activate = std::move(on_activate);
        android_jni::Frame frame;
        if (frame.failed() || !ensure_receiver(frame)) {
            result.body = "Android tray activate receiver is not loaded";
            return result;
        }
        result.ok = post_ongoing(frame, prepared.tip, g_badge, g_pending);
        if (!result.ok) {
            result.body = "Android notification service is not available";
        }
        return result;
    }

    plugin::OsResult badge(int count) override {
        plugin::OsResult result;
        android_jni::Frame frame;
        result.ok = post_ongoing(frame, std::to_string(count), count, g_pending);
        if (result.ok) {
            g_badge = count;
            g_badge_set = true;
            result.title = std::to_string(count);
        } else {
            result.body = "Android notification service is not available";
        }
        return result;
    }

    int badge_count() const override { return g_badge_set ? g_badge : 0; }
};

#endif

}  // namespace

void register_android_tray(ExtensionRegistry& registry) {
#if defined(__ANDROID__)
    registry.add_tray(std::make_unique<AndroidTray>());
#else
    (void)registry;
#endif
}

}  // namespace zui
