#include "plugins/association/android_launch.h"

#include "plugins/android_jni.h"

#include <string>

namespace zui {
namespace association_android {

plugin::OpenerReport AndroidAssociationLaunch::launch(std::string_view executable, std::string_view file_path) {
    plugin::OpenerReport report;
    if (executable.empty() || file_path.empty()) {
        report.ran = true;
        return report;
    }
    report.executable = std::string(executable);
#if defined(__ANDROID__)
    report.ran = true;
    android_jni::Frame frame;
    if (frame.failed()) {
        return report;
    }
    JNIEnv* env = frame.env();
    jclass intent_cls = env->FindClass("android/content/Intent");
    jobject intent = env->NewObject(intent_cls, env->GetMethodID(intent_cls, "<init>", "(Ljava/lang/String;)V"),
                                    frame.jstr("android.intent.action.VIEW"));
    jclass uri_cls = env->FindClass("android/net/Uri");
    const std::string file(file_path);
    jobject uri = env->CallStaticObjectMethod(
        uri_cls, env->GetStaticMethodID(uri_cls, "parse", "(Ljava/lang/String;)Landroid/net/Uri;"), frame.jstr(file));
    env->CallObjectMethod(intent, env->GetMethodID(intent_cls, "setData", "(Landroid/net/Uri;)Landroid/content/Intent;"),
                          uri);
    env->CallObjectMethod(intent, env->GetMethodID(intent_cls, "addFlags", "(I)Landroid/content/Intent;"), 0x10000000);
    jclass ctx = env->GetObjectClass(frame.application());
    env->CallVoidMethod(frame.application(), env->GetMethodID(ctx, "startActivity", "(Landroid/content/Intent;)V"),
                        intent);
    report.ok = !frame.failed();
#endif
    return report;
}

}  // namespace association_android
}  // namespace zui
