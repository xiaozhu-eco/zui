#ifndef ZUI_PLUGINS_ANDROID_JNI_H
#define ZUI_PLUGINS_ANDROID_JNI_H

// Attaches the embedder's JavaVM. Plugin translation units issue the JNI
// calls themselves. Windows builds include this header and compile none of it.

#include <string>
#include <string_view>

#if defined(__ANDROID__)
#include <jni.h>

namespace zui {
namespace android_jni {

class Frame {
public:
    Frame() {
        jint count = 0;
        if (JNI_GetCreatedJavaVMs(&vm_, 1, &count) != JNI_OK || count < 1 || vm_ == nullptr) {
            failed_ = true;
            return;
        }
        const jint env_rc = vm_->GetEnv(reinterpret_cast<void**>(&env_), JNI_VERSION_1_6);
        if (env_rc == JNI_EDETACHED) {
            if (vm_->AttachCurrentThread(&env_, nullptr) != JNI_OK) {
                failed_ = true;
                env_ = nullptr;
                return;
            }
            attached_ = true;
        } else if (env_rc != JNI_OK) {
            failed_ = true;
            return;
        }
        jclass thread = env_->FindClass("android/app/ActivityThread");
        if (thread == nullptr) {
            failed_ = true;
            return;
        }
        jmethodID current = env_->GetStaticMethodID(thread, "currentApplication", "()Landroid/app/Application;");
        if (current == nullptr) {
            failed_ = true;
            return;
        }
        app_ = env_->CallStaticObjectMethod(thread, current);
        if (app_ == nullptr || env_->ExceptionCheck()) {
            failed_ = true;
        }
    }

    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;

    ~Frame() {
        if (attached_ && vm_ != nullptr) {
            vm_->DetachCurrentThread();
        }
    }

    bool failed() {
        if (env_ != nullptr && env_->ExceptionCheck()) {
            env_->ExceptionClear();
            failed_ = true;
        }
        return failed_ || env_ == nullptr || app_ == nullptr;
    }

    JNIEnv* env() const { return env_; }
    jobject application() const { return app_; }

    jstring jstr(std::string_view text) {
        if (env_ == nullptr) {
            return nullptr;
        }
        const std::string owned(text);
        return env_->NewStringUTF(owned.c_str());
    }

    // App classes are not on the bootstrap loader. Framework classes still use FindClass.
    jclass load(const char* dotted) {
        if (env_ == nullptr || app_ == nullptr || dotted == nullptr) {
            return nullptr;
        }
        jclass app_cls = env_->GetObjectClass(app_);
        jmethodID get_loader = app_cls != nullptr ? env_->GetMethodID(app_cls, "getClassLoader", "()Ljava/lang/ClassLoader;")
                                                  : nullptr;
        jobject loader = get_loader != nullptr ? env_->CallObjectMethod(app_, get_loader) : nullptr;
        if (loader == nullptr || env_->ExceptionCheck()) {
            env_->ExceptionClear();
            return nullptr;
        }
        jmethodID load_class =
            env_->GetMethodID(env_->GetObjectClass(loader), "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
        jstring name = jstr(dotted);
        jobject loaded = load_class != nullptr && name != nullptr ? env_->CallObjectMethod(loader, load_class, name)
                                                                  : nullptr;
        if (loaded == nullptr || env_->ExceptionCheck()) {
            env_->ExceptionClear();
            return nullptr;
        }
        return static_cast<jclass>(loaded);
    }

private:
    JavaVM* vm_ = nullptr;
    JNIEnv* env_ = nullptr;
    jobject app_ = nullptr;
    bool attached_ = false;
    bool failed_ = false;
};

}  // namespace android_jni
}  // namespace zui

#endif
#endif
