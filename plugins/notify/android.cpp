#include "ext/slots.h"
#include "plugins/android_jni.h"

#include <string>

namespace zui {
namespace {

#if defined(__ANDROID__)

class AndroidNotify final : public NotifyService {
public:
    plugin::OsResult notify(std::string_view title, std::string_view body) override {
        plugin::OsResult result;
        android_jni::Frame frame;
        if (frame.failed()) {
            return result;
        }
        JNIEnv* env = frame.env();
        jclass ctx = env->GetObjectClass(frame.application());
        jobject nm = env->CallObjectMethod(
            frame.application(), env->GetMethodID(ctx, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;"),
            frame.jstr("notification"));
        if (nm == nullptr || frame.failed()) {
            return result;
        }
        jclass channel_cls = env->FindClass("android/app/NotificationChannel");
        if (channel_cls != nullptr) {
            jobject channel = env->NewObject(
                channel_cls, env->GetMethodID(channel_cls, "<init>", "(Ljava/lang/String;Ljava/lang/CharSequence;I)V"),
                frame.jstr("zui"), frame.jstr("Zui"), 3);
            if (channel != nullptr && !frame.failed()) {
                env->CallVoidMethod(nm, env->GetMethodID(env->GetObjectClass(nm), "createNotificationChannel",
                                                         "(Landroid/app/NotificationChannel;)V"),
                                    channel);
            }
        }
        if (frame.failed()) {
            return result;
        }
        jclass builder_cls = env->FindClass("android/app/Notification$Builder");
        jobject builder = env->NewObject(builder_cls,
                                         env->GetMethodID(builder_cls, "<init>", "(Landroid/content/Context;Ljava/lang/String;)V"),
                                         frame.application(), frame.jstr("zui"));
        env->CallObjectMethod(builder, env->GetMethodID(builder_cls, "setContentTitle",
                                                        "(Ljava/lang/CharSequence;)Landroid/app/Notification$Builder;"),
                              frame.jstr(title));
        env->CallObjectMethod(builder, env->GetMethodID(builder_cls, "setContentText",
                                                        "(Ljava/lang/CharSequence;)Landroid/app/Notification$Builder;"),
                              frame.jstr(body));
        jclass drawable = env->FindClass("android/R$drawable");
        const jint icon = env->GetStaticIntField(drawable, env->GetStaticFieldID(drawable, "ic_dialog_info", "I"));
        env->CallObjectMethod(builder, env->GetMethodID(builder_cls, "setSmallIcon", "(I)Landroid/app/Notification$Builder;"),
                              icon);
        jobject note = env->CallObjectMethod(builder, env->GetMethodID(builder_cls, "build", "()Landroid/app/Notification;"));
        env->CallVoidMethod(nm, env->GetMethodID(env->GetObjectClass(nm), "notify", "(ILandroid/app/Notification;)V"), 1,
                            note);
        result.ok = !frame.failed();
        if (result.ok) {
            result.title.assign(title.begin(), title.end());
            result.body.assign(body.begin(), body.end());
        }
        return result;
    }
};

#endif

}  // namespace

void register_android_notify(ExtensionRegistry& registry) {
#if defined(__ANDROID__)
    registry.add_notify(std::make_unique<AndroidNotify>());
#else
    (void)registry;
#endif
}

}  // namespace zui
