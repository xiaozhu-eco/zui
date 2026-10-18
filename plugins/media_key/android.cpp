#include "ext/slots.h"
#include "plugins/android_dex.h"
#include "plugins/android_jni.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace zui {
namespace media_key_android {
#include "plugins/media_key/android_callback_dex.inc"

const std::uint8_t* callback_dex(std::size_t& size) {
    size = sizeof(kCallbackDex);
    return kCallbackDex;
}
}  // namespace media_key_android

namespace {

static_assert(sizeof(media_key_android::kCallbackDex) > 0x70, "media callback dex");
static_assert(media_key_android::kCallbackDex[0] == 'd' && media_key_android::kCallbackDex[1] == 'e' &&
                  media_key_android::kCallbackDex[2] == 'x' && media_key_android::kCallbackDex[3] == '\n',
              "media callback payload must be DEX, not a JVM class file");

#if defined(__ANDROID__)

plugin::Handler g_media_handler[3];
jobject g_session = nullptr;
jobject g_callback = nullptr;
jobject g_loader = nullptr;
bool g_natives = false;

void media_on_play(JNIEnv*, jclass) {
    if (g_media_handler[0]) {
        g_media_handler[0]();
    }
}

void media_on_next(JNIEnv*, jclass) {
    if (g_media_handler[1]) {
        g_media_handler[1]();
    }
}

void media_on_previous(JNIEnv*, jclass) {
    if (g_media_handler[2]) {
        g_media_handler[2]();
    }
}

jclass media_callback_class(android_jni::Frame& frame) {
    JNIEnv* env = frame.env();
    // Writable copy. A direct ByteBuffer over a read-only DEX image faults if ART writes it.
    static std::vector<std::uint8_t> dex_bytes;
    if (dex_bytes.empty()) {
        std::size_t dex_size = 0;
        const std::uint8_t* dex = media_key_android::callback_dex(dex_size);
        dex_bytes.assign(dex, dex + dex_size);
    }
    jclass existing = android_dex::load_dex_class(frame, "zui.ZuiMediaCallback", "Lzui/ZuiMediaCallback;",
                                                  dex_bytes.data(), dex_bytes.size(), "zui-media-callback.dex", g_loader);
    if (existing == nullptr) {
        return nullptr;
    }
    if (!g_natives) {
        JNINativeMethod natives[3] = {
            {const_cast<char*>("nativeOnPlay"), const_cast<char*>("()V"), reinterpret_cast<void*>(media_on_play)},
            {const_cast<char*>("nativeOnNext"), const_cast<char*>("()V"), reinterpret_cast<void*>(media_on_next)},
            {const_cast<char*>("nativeOnPrevious"), const_cast<char*>("()V"),
             reinterpret_cast<void*>(media_on_previous)},
        };
        if (env->RegisterNatives(existing, natives, 3) != 0) {
            env->ExceptionClear();
            return nullptr;
        }
        g_natives = true;
    }
    return existing;
}

jint static_int(JNIEnv* env, jclass cls, const char* name, jint fallback) {
    jfieldID id = env->GetStaticFieldID(cls, name, "I");
    if (id == nullptr || env->ExceptionCheck()) {
        env->ExceptionClear();
        return fallback;
    }
    return env->GetStaticIntField(cls, id);
}

bool ensure_session(android_jni::Frame& frame) {
    if (g_session != nullptr && g_callback != nullptr) {
        return true;
    }
    JNIEnv* env = frame.env();
    jclass callback_cls = media_callback_class(frame);
    if (callback_cls == nullptr) {
        return false;
    }
    jmethodID callback_ctor = env->GetMethodID(callback_cls, "<init>", "()V");
    jobject callback = callback_ctor != nullptr ? env->NewObject(callback_cls, callback_ctor) : nullptr;
    jclass session_cls = env->FindClass("android/media/session/MediaSession");
    jmethodID session_ctor =
        session_cls != nullptr
            ? env->GetMethodID(session_cls, "<init>", "(Landroid/content/Context;Ljava/lang/String;)V")
            : nullptr;
    jobject session = session_ctor != nullptr
                          ? env->NewObject(session_cls, session_ctor, frame.application(), frame.jstr("zui"))
                          : nullptr;
    if (callback == nullptr || session == nullptr || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    const jint flags = static_int(env, session_cls, "FLAG_HANDLES_MEDIA_BUTTONS", 1) |
                       static_int(env, session_cls, "FLAG_HANDLES_TRANSPORT_CONTROLS", 2);
    jmethodID set_flags = env->GetMethodID(session_cls, "setFlags", "(I)V");
    if (set_flags != nullptr) {
        env->CallVoidMethod(session, set_flags, flags);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
    }
    jmethodID set_callback =
        env->GetMethodID(session_cls, "setCallback", "(Landroid/media/session/MediaSession$Callback;)V");
    if (set_callback == nullptr || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    env->CallVoidMethod(session, set_callback, callback);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    jmethodID set_active = env->GetMethodID(session_cls, "setActive", "(Z)V");
    if (set_active != nullptr) {
        env->CallVoidMethod(session, set_active, static_cast<jboolean>(JNI_TRUE));
    }
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    g_callback = env->NewGlobalRef(callback);
    g_session = env->NewGlobalRef(session);
    return g_callback != nullptr && g_session != nullptr;
}

class AndroidMedia final : public MediaKeyService {
public:
    plugin::OsResult media_key(plugin::MediaKey action, plugin::Handler on_fire) override {
        const std::string_view which = plugin::media_key_name(action);
        plugin::OsResult result;
        int index = -1;
        if (which == "play") {
            index = 0;
        } else if (which == "next") {
            index = 1;
        } else if (which == "previous") {
            index = 2;
        } else {
            return result;
        }
        g_media_handler[index] = std::move(on_fire);
        android_jni::Frame frame;
        if (frame.failed() || !ensure_session(frame)) {
            return result;
        }
        result.ok = g_callback != nullptr && g_session != nullptr;
        if (result.ok) {
            result.title.assign(which.begin(), which.end());
        }
        return result;
    }
};

#endif

}  // namespace

void register_android_media_key(ExtensionRegistry& registry) {
#if defined(__ANDROID__)
    registry.add_media_key(std::make_unique<AndroidMedia>());
#else
    (void)registry;
#endif
}

}  // namespace zui
