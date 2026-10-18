#include "ext/slots.h"
#include "plugins/android_jni.h"
#include "plugins/instance/endpoint.h"
#include "plugins/instance/unix_claim.h"

#include <string>

namespace zui {
namespace {

#if defined(__ANDROID__)

std::string files_dir() {
    android_jni::Frame frame;
    if (frame.failed()) {
        return {};
    }
    JNIEnv* env = frame.env();
    jobject app = frame.application();
    jclass ctx = env->GetObjectClass(app);
    jmethodID get_files = ctx != nullptr ? env->GetMethodID(ctx, "getFilesDir", "()Ljava/io/File;") : nullptr;
    jobject file = get_files != nullptr ? env->CallObjectMethod(app, get_files) : nullptr;
    jclass file_cls = file != nullptr ? env->GetObjectClass(file) : nullptr;
    jmethodID absolute =
        file_cls != nullptr ? env->GetMethodID(file_cls, "getAbsolutePath", "()Ljava/lang/String;") : nullptr;
    jstring path = absolute != nullptr ? static_cast<jstring>(env->CallObjectMethod(file, absolute)) : nullptr;
    if (path == nullptr || frame.failed()) {
        return {};
    }
    const char* chars = env->GetStringUTFChars(path, nullptr);
    std::string out = chars != nullptr ? chars : "";
    if (chars != nullptr) {
        env->ReleaseStringUTFChars(path, chars);
    }
    return out;
}

class AndroidInstance final : public InstanceService {
public:
    bool claim(std::string_view identity) override {
        path_ = instance_detail::android_socket_path(files_dir(), identity);
        if (!claim_.claim(path_)) {
            return false;
        }
        identity_.assign(identity.begin(), identity.end());
        return true;
    }

    bool signal_primary(std::string_view identity, std::string_view args) override {
        return claim_.signal(instance_detail::android_socket_path(files_dir(), identity), args);
    }

    std::string take_args(std::string_view identity) override {
        if (identity_.empty() || identity != identity_) {
            return {};
        }
        return claim_.take();
    }

private:
    instance_detail::UnixClaim claim_;
    std::string path_;
    std::string identity_;
};

#endif

}  // namespace

void register_android_instance(ExtensionRegistry& registry) {
#if defined(__ANDROID__)
    registry.add_instance(std::make_unique<AndroidInstance>());
#else
    (void)registry;
#endif
}

}  // namespace zui
