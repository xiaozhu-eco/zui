#include "ext/slots.h"
#include "plugins/android_jni.h"
#include "plugins/clipboard/bmp.h"

#include <string>
#include <vector>

namespace zui {
namespace backends {
namespace {

#if defined(__ANDROID__)

jobject clipboard_manager(android_jni::Frame& frame) {
    JNIEnv* env = frame.env();
    jclass ctx = env->GetObjectClass(frame.application());
    jmethodID service = env->GetMethodID(ctx, "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
    jstring name = frame.jstr("clipboard");
    if (service == nullptr || name == nullptr) {
        return nullptr;
    }
    return env->CallObjectMethod(frame.application(), service, name);
}

class AndroidClipboard final : public ClipboardService {
public:
    ClipboardText text() override {
        ClipboardText out;
        android_jni::Frame frame;
        jobject manager = frame.failed() ? nullptr : clipboard_manager(frame);
        if (manager == nullptr || frame.failed()) {
            return out;
        }
        JNIEnv* env = frame.env();
        jclass cls = env->GetObjectClass(manager);
        jmethodID primary = env->GetMethodID(cls, "getPrimaryClip", "()Landroid/content/ClipData;");
        jobject clip = primary != nullptr ? env->CallObjectMethod(manager, primary) : nullptr;
        if (clip == nullptr || frame.failed()) {
            return out;
        }
        jmethodID item_at = env->GetMethodID(env->GetObjectClass(clip), "getItemAt", "(I)Landroid/content/ClipData$Item;");
        jobject item = item_at != nullptr ? env->CallObjectMethod(clip, item_at, 0) : nullptr;
        jmethodID get_text =
            item != nullptr ? env->GetMethodID(env->GetObjectClass(item), "getText", "()Ljava/lang/CharSequence;")
                            : nullptr;
        jobject sequence = get_text != nullptr ? env->CallObjectMethod(item, get_text) : nullptr;
        if (sequence == nullptr || frame.failed()) {
            return out;
        }
        jmethodID to_string =
            env->GetMethodID(env->GetObjectClass(sequence), "toString", "()Ljava/lang/String;");
        jstring text = to_string != nullptr ? static_cast<jstring>(env->CallObjectMethod(sequence, to_string)) : nullptr;
        if (text == nullptr || frame.failed()) {
            return out;
        }
        const char* utf = env->GetStringUTFChars(text, nullptr);
        out.status = ClipboardStatus::Ok;
        out.text = utf != nullptr ? utf : "";
        if (utf != nullptr) {
            env->ReleaseStringUTFChars(text, utf);
        }
        return out;
    }

    bool set_text(std::string_view value) override {
        android_jni::Frame frame;
        jobject manager = frame.failed() ? nullptr : clipboard_manager(frame);
        if (manager == nullptr) {
            return false;
        }
        JNIEnv* env = frame.env();
        jclass clip_cls = env->FindClass("android/content/ClipData");
        jmethodID neu = clip_cls != nullptr
                            ? env->GetStaticMethodID(clip_cls, "newPlainText",
                                                     "(Ljava/lang/CharSequence;Ljava/lang/CharSequence;)Landroid/content/ClipData;")
                            : nullptr;
        jstring label = frame.jstr("zui");
        jstring body = frame.jstr(value);
        jobject clip = neu != nullptr ? env->CallStaticObjectMethod(clip_cls, neu, label, body) : nullptr;
        jmethodID set_clip = env->GetMethodID(env->GetObjectClass(manager), "setPrimaryClip", "(Landroid/content/ClipData;)V");
        if (clip == nullptr || set_clip == nullptr || frame.failed()) {
            return false;
        }
        env->CallVoidMethod(manager, set_clip, clip);
        return !frame.failed();
    }

    ClipboardImage image() override {
        android_jni::Frame frame;
        jobject manager = frame.failed() ? nullptr : clipboard_manager(frame);
        if (manager == nullptr || frame.failed()) {
            return {};
        }
        JNIEnv* env = frame.env();
        jobject clip = env->CallObjectMethod(
            manager, env->GetMethodID(env->GetObjectClass(manager), "getPrimaryClip", "()Landroid/content/ClipData;"));
        jobject item = clip != nullptr ? env->CallObjectMethod(
                                             clip, env->GetMethodID(env->GetObjectClass(clip), "getItemAt",
                                                                    "(I)Landroid/content/ClipData$Item;"),
                                             0)
                                       : nullptr;
        jobject intent = item != nullptr ? env->CallObjectMethod(
                                               item, env->GetMethodID(env->GetObjectClass(item), "getIntent",
                                                                      "()Landroid/content/Intent;"))
                                         : nullptr;
        jbyteArray raw = intent != nullptr
                             ? static_cast<jbyteArray>(env->CallObjectMethod(
                                   intent, env->GetMethodID(env->GetObjectClass(intent), "getByteArrayExtra",
                                                            "(Ljava/lang/String;)[B"),
                                   frame.jstr("image/bmp")))
                             : nullptr;
        if (raw == nullptr || frame.failed()) {
            return {};
        }
        const jsize length = env->GetArrayLength(raw);
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
        if (length > 0) {
            env->GetByteArrayRegion(raw, 0, length, reinterpret_cast<jbyte*>(bytes.data()));
        }
        if (frame.failed()) {
            return {};
        }
        return clipboard_bmp::decode(bytes.data(), bytes.size());
    }

    bool set_image(const ClipboardImage& image) override {
        const std::vector<std::uint8_t> bmp =
            clipboard_bmp::encode(image.width, image.height, image.rgba.data(), image.rgba.size());
        if (bmp.empty()) {
            return false;
        }
        android_jni::Frame frame;
        jobject manager = frame.failed() ? nullptr : clipboard_manager(frame);
        if (manager == nullptr) {
            return false;
        }
        JNIEnv* env = frame.env();
        jbyteArray raw = env->NewByteArray(static_cast<jsize>(bmp.size()));
        env->SetByteArrayRegion(raw, 0, static_cast<jsize>(bmp.size()), reinterpret_cast<const jbyte*>(bmp.data()));
        jclass intent_cls = env->FindClass("android/content/Intent");
        jobject intent = env->NewObject(intent_cls, env->GetMethodID(intent_cls, "<init>", "(Ljava/lang/String;)V"),
                                        frame.jstr("zui.clipboard.IMAGE"));
        env->CallObjectMethod(intent,
                              env->GetMethodID(intent_cls, "putExtra", "(Ljava/lang/String;[B)Landroid/content/Intent;"),
                              frame.jstr("image/bmp"), raw);
        jclass clip_cls = env->FindClass("android/content/ClipData");
        jobject clip = env->CallStaticObjectMethod(
            clip_cls,
            env->GetStaticMethodID(clip_cls, "newIntent",
                                   "(Ljava/lang/CharSequence;Landroid/content/Intent;)Landroid/content/ClipData;"),
            frame.jstr("image/bmp"), intent);
        jmethodID set_clip =
            env->GetMethodID(env->GetObjectClass(manager), "setPrimaryClip", "(Landroid/content/ClipData;)V");
        if (clip == nullptr || set_clip == nullptr || frame.failed()) {
            return false;
        }
        env->CallVoidMethod(manager, set_clip, clip);
        return !frame.failed();
    }
};

#endif

}  // namespace

void register_android_clipboard(ExtensionRegistry& registry) {
#if defined(__ANDROID__)
    registry.add_clipboard(std::make_unique<AndroidClipboard>());
#else
    (void)registry;
#endif
}

}  // namespace backends
}  // namespace zui
