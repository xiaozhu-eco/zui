#include "plugins/association/android_query.h"

#include "plugins/android_jni.h"
#include "plugins/association/record.h"

#include <string>

namespace zui {
namespace association_android {

plugin::OpenerReport AndroidAssociationQuery::query(std::string_view type) {
    plugin::OpenerReport report;
#if defined(__ANDROID__)
    report.ran = true;
    const association_detail::Classified kind = association_detail::classify(type);
    android_jni::Frame frame;
    if (frame.failed()) {
        return report;
    }
    JNIEnv* env = frame.env();
    jclass intent_cls = env->FindClass("android/content/Intent");
    jobject intent = env->NewObject(intent_cls, env->GetMethodID(intent_cls, "<init>", "(Ljava/lang/String;)V"),
                                    frame.jstr("android.intent.action.VIEW"));
    if (kind.kind == association_detail::TypeKind::Scheme) {
        report.scheme = kind.value;
        jclass uri_cls = env->FindClass("android/net/Uri");
        jstring text = frame.jstr(kind.value + "://open");
        jobject uri = env->CallStaticObjectMethod(
            uri_cls, env->GetStaticMethodID(uri_cls, "parse", "(Ljava/lang/String;)Landroid/net/Uri;"), text);
        env->CallObjectMethod(intent, env->GetMethodID(intent_cls, "setData", "(Landroid/net/Uri;)Landroid/content/Intent;"),
                              uri);
    } else if (kind.kind == association_detail::TypeKind::Mime) {
        report.mime = kind.value;
        env->CallObjectMethod(intent, env->GetMethodID(intent_cls, "setType", "(Ljava/lang/String;)Landroid/content/Intent;"),
                              frame.jstr(kind.value));
    } else if (kind.kind == association_detail::TypeKind::Extension) {
        report.extension = kind.value;
    }
    jclass ctx = env->GetObjectClass(frame.application());
    jobject pm = env->CallObjectMethod(frame.application(),
                                       env->GetMethodID(ctx, "getPackageManager", "()Landroid/content/pm/PackageManager;"));
    jobject resolved = env->CallObjectMethod(
        pm, env->GetMethodID(env->GetObjectClass(pm), "resolveActivity",
                             "(Landroid/content/Intent;I)Landroid/content/pm/ResolveInfo;"),
        intent, 0);
    if (resolved == nullptr || frame.failed()) {
        return report;
    }
    jstring name = static_cast<jstring>(env->CallObjectMethod(
        resolved, env->GetMethodID(env->GetObjectClass(resolved), "toString", "()Ljava/lang/String;")));
    const char* utf = name != nullptr ? env->GetStringUTFChars(name, nullptr) : nullptr;
    if (utf != nullptr) {
        report.executable = utf;
        report.ok = !report.executable.empty();
        env->ReleaseStringUTFChars(name, utf);
    }
#else
    (void)type;
#endif
    return report;
}

}  // namespace association_android
}  // namespace zui
