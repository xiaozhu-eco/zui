#include "plugins/association/android_store.h"

#include "plugins/android_jni.h"
#include "plugins/association/text.h"

#include <string>
#include <string_view>
#include <vector>

namespace zui {
namespace association_android {
namespace {

std::string mime_for_extension(std::string_view extension) {
    std::string ext(extension);
    if (!ext.empty() && ext.front() == '.') {
        ext.erase(ext.begin());
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
    if (ext == "html" || ext == "htm") {
        return "text/html";
    }
    if (ext == "json") {
        return "application/json";
    }
    if (ext == "txt" || ext == "md" || ext == "text") {
        return "text/plain";
    }
    return "application/octet-stream";
}

#if defined(__ANDROID__)

bool jni_ok(JNIEnv* env) {
    if (env == nullptr || env->ExceptionCheck()) {
        if (env != nullptr) {
            env->ExceptionClear();
        }
        return false;
    }
    return true;
}

jint static_int(JNIEnv* env, jclass cls, const char* field, jint fallback) {
    jfieldID id = cls != nullptr ? env->GetStaticFieldID(cls, field, "I") : nullptr;
    if (!jni_ok(env) || id == nullptr) {
        return fallback;
    }
    const jint value = env->GetStaticIntField(cls, id);
    if (!jni_ok(env)) {
        return fallback;
    }
    return value;
}

bool add_text(JNIEnv* env, android_jni::Frame& frame, jobject object, const char* method, const char* signature,
              std::string_view text) {
    if (object == nullptr) {
        return false;
    }
    jmethodID id = env->GetMethodID(env->GetObjectClass(object), method, signature);
    jstring value = frame.jstr(text);
    if (id == nullptr || value == nullptr || !jni_ok(env)) {
        return false;
    }
    if (signature[0] == '(' && std::string(signature).find(")V") != std::string::npos) {
        env->CallVoidMethod(object, id, value);
    } else {
        env->CallObjectMethod(object, id, value);
    }
    return jni_ok(env);
}

jobject own_component(JNIEnv* env, android_jni::Frame& frame, jobject pm, jstring package_name, jobject query) {
    jclass pm_cls = env->GetObjectClass(pm);
    jmethodID launch_id =
        env->GetMethodID(pm_cls, "getLaunchIntentForPackage", "(Ljava/lang/String;)Landroid/content/Intent;");
    jobject launch = launch_id != nullptr ? env->CallObjectMethod(pm, launch_id, package_name) : nullptr;
    if (!jni_ok(env)) {
        launch = nullptr;
    }
    if (launch != nullptr) {
        jmethodID component_id = env->GetMethodID(env->GetObjectClass(launch), "getComponent",
                                                  "()Landroid/content/ComponentName;");
        jobject component = component_id != nullptr ? env->CallObjectMethod(launch, component_id) : nullptr;
        if (jni_ok(env) && component != nullptr) {
            return component;
        }
    }
    const jint match_default = static_int(env, pm_cls, "MATCH_DEFAULT_ONLY", 0x00010000);
    jmethodID query_id =
        env->GetMethodID(pm_cls, "queryIntentActivities", "(Landroid/content/Intent;I)Ljava/util/List;");
    jobject list = query_id != nullptr ? env->CallObjectMethod(pm, query_id, query, match_default) : nullptr;
    if (!jni_ok(env) || list == nullptr) {
        return nullptr;
    }
    jclass list_cls = env->GetObjectClass(list);
    const jint count = env->CallIntMethod(list, env->GetMethodID(list_cls, "size", "()I"));
    if (!jni_ok(env) || count < 1) {
        return nullptr;
    }
    jmethodID get = env->GetMethodID(list_cls, "get", "(I)Ljava/lang/Object;");
    jobject info = env->CallObjectMethod(list, get, 0);
    if (!jni_ok(env) || info == nullptr) {
        return nullptr;
    }
    jclass info_cls = env->GetObjectClass(info);
    jobject activity = env->GetObjectField(info, env->GetFieldID(info_cls, "activityInfo", "Landroid/content/pm/ActivityInfo;"));
    if (!jni_ok(env) || activity == nullptr) {
        return nullptr;
    }
    jclass activity_cls = env->GetObjectClass(activity);
    jstring pkg = static_cast<jstring>(env->GetObjectField(activity, env->GetFieldID(activity_cls, "packageName", "Ljava/lang/String;")));
    jstring name = static_cast<jstring>(env->GetObjectField(activity, env->GetFieldID(activity_cls, "name", "Ljava/lang/String;")));
    jclass component_cls = env->FindClass("android/content/ComponentName");
    jmethodID ctor = component_cls != nullptr ? env->GetMethodID(component_cls, "<init>", "(Ljava/lang/String;Ljava/lang/String;)V")
                                             : nullptr;
    if (ctor == nullptr || pkg == nullptr || name == nullptr || !jni_ok(env)) {
        return nullptr;
    }
    return env->NewObject(component_cls, ctor, pkg, name);
}

jobject view_intent(JNIEnv* env, android_jni::Frame& frame, std::string_view scheme, std::string_view mime) {
    jclass intent_cls = env->FindClass("android/content/Intent");
    jmethodID ctor = intent_cls != nullptr ? env->GetMethodID(intent_cls, "<init>", "(Ljava/lang/String;)V") : nullptr;
    jobject intent = ctor != nullptr ? env->NewObject(intent_cls, ctor, frame.jstr("android.intent.action.VIEW")) : nullptr;
    if (intent == nullptr || !jni_ok(env)) {
        return nullptr;
    }
    if (!scheme.empty()) {
        const std::string uri_text = std::string(scheme) + "://open";
        jclass uri_cls = env->FindClass("android/net/Uri");
        jmethodID parse = uri_cls != nullptr ? env->GetStaticMethodID(uri_cls, "parse", "(Ljava/lang/String;)Landroid/net/Uri;")
                                            : nullptr;
        jobject uri = parse != nullptr ? env->CallStaticObjectMethod(uri_cls, parse, frame.jstr(uri_text)) : nullptr;
        jmethodID set_data = env->GetMethodID(intent_cls, "setData", "(Landroid/net/Uri;)Landroid/content/Intent;");
        if (uri == nullptr || set_data == nullptr || !jni_ok(env)) {
            return nullptr;
        }
        env->CallObjectMethod(intent, set_data, uri);
    }
    if (!mime.empty()) {
        jmethodID set_type = env->GetMethodID(intent_cls, "setType", "(Ljava/lang/String;)Landroid/content/Intent;");
        if (set_type == nullptr) {
            return nullptr;
        }
        env->CallObjectMethod(intent, set_type, frame.jstr(mime));
    }
    return jni_ok(env) ? intent : nullptr;
}

bool add_preferred(android_jni::Frame& frame, std::string_view scheme, std::string_view mime) {
    if (scheme.empty() && mime.empty()) {
        return false;
    }
    JNIEnv* env = frame.env();
    jclass filter_cls = env->FindClass("android/content/IntentFilter");
    jmethodID filter_ctor = filter_cls != nullptr ? env->GetMethodID(filter_cls, "<init>", "()V") : nullptr;
    jobject filter = filter_ctor != nullptr ? env->NewObject(filter_cls, filter_ctor) : nullptr;
    if (filter == nullptr || !jni_ok(env)) {
        return false;
    }
    if (!add_text(env, frame, filter, "addAction", "(Ljava/lang/String;)V", "android.intent.action.VIEW")) {
        return false;
    }
    if (!add_text(env, frame, filter, "addCategory", "(Ljava/lang/String;)V", "android.intent.category.DEFAULT")) {
        return false;
    }
    if (!scheme.empty()) {
        if (!add_text(env, frame, filter, "addCategory", "(Ljava/lang/String;)V", "android.intent.category.BROWSABLE") ||
            !add_text(env, frame, filter, "addDataScheme", "(Ljava/lang/String;)V", scheme)) {
            return false;
        }
    }
    if (!mime.empty()) {
        if (!add_text(env, frame, filter, "addDataScheme", "(Ljava/lang/String;)V", "content") ||
            !add_text(env, frame, filter, "addDataScheme", "(Ljava/lang/String;)V", "file") ||
            !add_text(env, frame, filter, "addDataType", "(Ljava/lang/String;)V", mime)) {
            return false;
        }
    }
    jobject intent = view_intent(env, frame, scheme, mime);
    jclass ctx = env->GetObjectClass(frame.application());
    jmethodID pm_id = env->GetMethodID(ctx, "getPackageManager", "()Landroid/content/pm/PackageManager;");
    jmethodID pkg_id = env->GetMethodID(ctx, "getPackageName", "()Ljava/lang/String;");
    jobject pm = pm_id != nullptr ? env->CallObjectMethod(frame.application(), pm_id) : nullptr;
    jstring package_name = pkg_id != nullptr ? static_cast<jstring>(env->CallObjectMethod(frame.application(), pkg_id))
                                            : nullptr;
    if (intent == nullptr || pm == nullptr || package_name == nullptr || !jni_ok(env)) {
        return false;
    }
    jobject preferred = own_component(env, frame, pm, package_name, intent);
    if (preferred == nullptr || !jni_ok(env)) {
        return false;
    }
    jclass component_cls = env->FindClass("android/content/ComponentName");
    jobjectArray set = component_cls != nullptr ? env->NewObjectArray(1, component_cls, preferred) : nullptr;
    const char* match_field = scheme.empty() ? "MATCH_CATEGORY_TYPE" : "MATCH_CATEGORY_SCHEME";
    const jint fallback = scheme.empty() ? 0x0600000 : 0x0200000;
    const jint match = static_int(env, filter_cls, match_field, fallback);
    jmethodID add = env->GetMethodID(env->GetObjectClass(pm), "addPreferredActivity",
                                     "(Landroid/content/IntentFilter;I[Landroid/content/ComponentName;Landroid/content/ComponentName;)V");
    if (set == nullptr || add == nullptr || !jni_ok(env)) {
        return false;
    }
    env->CallVoidMethod(pm, add, filter, match, set, preferred);
    return jni_ok(env);
}

#endif

}  // namespace

plugin::OpenerReport AndroidAssociationStore::write(const association_detail::Record& record,
                                                     plugin::OpenerReport report) {
#if defined(__ANDROID__)
    report.ran = true;
    report.desktop_entry =
        association_detail::AssociationText::android_manifest(record.extension, record.mime, record.scheme);
    android_jni::Frame frame;
    if (frame.failed()) {
        return report;
    }
    const bool need_scheme = !record.scheme.empty();
    const bool need_type = !record.extension.empty() || !record.mime.empty();
    bool scheme_ok = !need_scheme;
    bool type_ok = !need_type;
    if (need_scheme) {
        scheme_ok = add_preferred(frame, record.scheme, {});
    }
    if (need_type) {
        const std::string mime = record.mime.empty() ? mime_for_extension(record.extension) : record.mime;
        type_ok = add_preferred(frame, {}, mime);
    }
    report.ok = need_scheme || need_type;
    report.ok = report.ok && scheme_ok && type_ok && !frame.failed();
#else
    (void)record;
#endif
    return report;
}

bool AndroidAssociationStore::remove(const association_detail::Record& record) {
#if defined(__ANDROID__)
    android_jni::Frame frame;
    if (frame.failed()) {
        return false;
    }
    JNIEnv* env = frame.env();
    jclass ctx = env->GetObjectClass(frame.application());
    jmethodID pm_id = env->GetMethodID(ctx, "getPackageManager", "()Landroid/content/pm/PackageManager;");
    jobject pm = pm_id != nullptr ? env->CallObjectMethod(frame.application(), pm_id) : nullptr;
    jmethodID clear = pm != nullptr ? env->GetMethodID(env->GetObjectClass(pm), "clearPackagePreferredActivities",
                                                       "(Ljava/lang/String;)V")
                                    : nullptr;
    jmethodID pkg_id = env->GetMethodID(ctx, "getPackageName", "()Ljava/lang/String;");
    jstring pkg = pkg_id != nullptr ? static_cast<jstring>(env->CallObjectMethod(frame.application(), pkg_id)) : nullptr;
    if (clear == nullptr || pkg == nullptr || frame.failed()) {
        return false;
    }
    env->CallVoidMethod(pm, clear, pkg);
    (void)record;
    return !frame.failed();
#else
    (void)record;
    return false;
#endif
}

}  // namespace association_android
}  // namespace zui
