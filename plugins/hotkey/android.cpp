#include "ext/slots.h"
#include "plugins/android_dex.h"
#include "plugins/android_jni.h"
#include "plugins/hotkey/chord.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace zui {
namespace hotkey_android {
#include "plugins/hotkey/android_callback_dex.inc"

const std::uint8_t* callback_dex(std::size_t& size) {
    size = sizeof(kCallbackDex);
    return kCallbackDex;
}
}  // namespace hotkey_android

namespace {

static_assert(sizeof(hotkey_android::kCallbackDex) > 0x70, "hotkey dex");
static_assert(hotkey_android::kCallbackDex[0] == 'd' && hotkey_android::kCallbackDex[1] == 'e' &&
                  hotkey_android::kCallbackDex[2] == 'x' && hotkey_android::kCallbackDex[3] == '\n',
              "hotkey payload must be DEX, not a JVM class file");

#if defined(__ANDROID__)

struct HotkeyBinding {
    hotkey_chord::Chord chord;
    plugin::Handler handler;
};

std::mutex g_mu;
std::vector<HotkeyBinding> g_bindings;
jobject g_original_callback = nullptr;
jobject g_loader = nullptr;
bool g_hooked = false;
bool g_natives = false;

bool chord(JNIEnv* env, jobject event) {
    jclass key_cls = env->FindClass("android/view/KeyEvent");
    if (key_cls == nullptr || event == nullptr || !env->IsInstanceOf(event, key_cls)) {
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        }
        return false;
    }
    jclass event_cls = env->GetObjectClass(event);
    const jint action = env->CallIntMethod(event, env->GetMethodID(event_cls, "getAction", "()I"));
    const jint code = env->CallIntMethod(event, env->GetMethodID(event_cls, "getKeyCode", "()I"));
    const jint meta = env->CallIntMethod(event, env->GetMethodID(event_cls, "getMetaState", "()I"));
    const jint repeat = env->CallIntMethod(event, env->GetMethodID(event_cls, "getRepeatCount", "()I"));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    constexpr jint action_down = 0;
    constexpr jint meta_mask = 1 | 2 | 0x1000 | 0x10000;
    if (action != action_down || repeat != 0) {
        return false;
    }
    plugin::Handler handler;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        for (const HotkeyBinding& binding : g_bindings) {
            if (binding.chord.android && code == binding.chord.android_keycode &&
                (meta & meta_mask) == binding.chord.android_meta) {
                handler = binding.handler;
                break;
            }
        }
    }
    if (!handler) {
        return false;
    }
    handler();
    return true;
}

jobject hotkey_invoke(JNIEnv* env, jclass, jobject method, jobjectArray args) {
    if (method == nullptr) {
        return nullptr;
    }
    jclass method_cls = env->GetObjectClass(method);
    jstring jname = static_cast<jstring>(
        env->CallObjectMethod(method, env->GetMethodID(method_cls, "getName", "()Ljava/lang/String;")));
    bool key_event = false;
    if (jname != nullptr) {
        const char* utf = env->GetStringUTFChars(jname, nullptr);
        key_event = utf != nullptr && std::strcmp(utf, "dispatchKeyEvent") == 0;
        if (utf != nullptr) {
            env->ReleaseStringUTFChars(jname, utf);
        }
    }
    if (key_event && args != nullptr && env->GetArrayLength(args) >= 1) {
        jobject event = env->GetObjectArrayElement(args, 0);
        if (chord(env, event)) {
            jclass boolean_cls = env->FindClass("java/lang/Boolean");
            jfieldID truth = env->GetStaticFieldID(boolean_cls, "TRUE", "Ljava/lang/Boolean;");
            return truth != nullptr ? env->GetStaticObjectField(boolean_cls, truth) : nullptr;
        }
    }
    if (g_original_callback == nullptr) {
        return nullptr;
    }
    jmethodID invoke = env->GetMethodID(method_cls, "invoke", "(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;");
    if (invoke == nullptr) {
        return nullptr;
    }
    return env->CallObjectMethod(method, invoke, g_original_callback, args);
}

jclass hotkey_class(android_jni::Frame& frame) {
    JNIEnv* env = frame.env();
    // Writable copy. A direct ByteBuffer over a read-only DEX image faults if ART writes it.
    static std::vector<std::uint8_t> dex_bytes;
    if (dex_bytes.empty()) {
        std::size_t dex_size = 0;
        const std::uint8_t* dex = hotkey_android::callback_dex(dex_size);
        dex_bytes.assign(dex, dex + dex_size);
    }
    jclass existing = android_dex::load_dex_class(frame, "zui.ZuiHotkeyInvoke", "Lzui/ZuiHotkeyInvoke;",
                                                  dex_bytes.data(), dex_bytes.size(), "zui-hotkey-invoke.dex", g_loader);
    if (existing == nullptr) {
        return nullptr;
    }
    if (!g_natives) {
        JNINativeMethod native_method = {
            const_cast<char*>("nativeInvoke"),
            const_cast<char*>("(Ljava/lang/reflect/Method;[Ljava/lang/Object;)Ljava/lang/Object;"),
            reinterpret_cast<void*>(hotkey_invoke)};
        if (env->RegisterNatives(existing, &native_method, 1) != 0) {
            env->ExceptionClear();
            return nullptr;
        }
        g_natives = true;
    }
    return existing;
}

bool install_hook(android_jni::Frame& frame) {
    if (g_hooked && g_original_callback != nullptr) {
        return true;
    }
    JNIEnv* env = frame.env();
    jclass host = frame.load("zui.ZuiHost");
    jmethodID current = host != nullptr ? env->GetStaticMethodID(host, "currentActivity", "()Landroid/app/Activity;")
                                       : nullptr;
    jobject activity = current != nullptr ? env->CallStaticObjectMethod(host, current) : nullptr;
    if (activity == nullptr || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    jobject window = env->CallObjectMethod(activity, env->GetMethodID(env->GetObjectClass(activity), "getWindow",
                                                                      "()Landroid/view/Window;"));
    if (window == nullptr || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    jclass window_cls = env->GetObjectClass(window);
    jobject original = env->CallObjectMethod(
        window, env->GetMethodID(window_cls, "getCallback", "()Landroid/view/Window$Callback;"));
    if (original == nullptr || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    jclass handler_cls = hotkey_class(frame);
    if (handler_cls == nullptr) {
        return false;
    }
    jobject handler = env->NewObject(handler_cls, env->GetMethodID(handler_cls, "<init>", "()V"));
    jclass proxy_cls = env->FindClass("java/lang/reflect/Proxy");
    jclass class_cls = env->FindClass("java/lang/Class");
    jclass callback_cls = env->FindClass("android/view/Window$Callback");
    jobjectArray interfaces = env->NewObjectArray(1, class_cls, callback_cls);
    jobject loader = env->CallObjectMethod(
        frame.application(),
        env->GetMethodID(env->GetObjectClass(frame.application()), "getClassLoader", "()Ljava/lang/ClassLoader;"));
    jmethodID new_proxy = env->GetStaticMethodID(
        proxy_cls, "newProxyInstance",
        "(Ljava/lang/ClassLoader;[Ljava/lang/Class;Ljava/lang/reflect/InvocationHandler;)Ljava/lang/Object;");
    jobject proxy = handler != nullptr && interfaces != nullptr && new_proxy != nullptr
                        ? env->CallStaticObjectMethod(proxy_cls, new_proxy, loader, interfaces, handler)
                        : nullptr;
    if (proxy == nullptr || env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    env->CallVoidMethod(window, env->GetMethodID(window_cls, "setCallback", "(Landroid/view/Window$Callback;)V"), proxy);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return false;
    }
    g_original_callback = env->NewGlobalRef(original);
    g_hooked = g_original_callback != nullptr;
    return g_hooked;
}

class AndroidHotkey final : public HotkeyService {
public:
    plugin::OsResult global_hotkey(const plugin::Hotkey& key, plugin::Handler on_fire) override {
        plugin::OsResult result;
        const hotkey_chord::Chord chord = hotkey_chord::encode(key);
        result.title = chord.label;
        if (!chord.android) {
            result.body = chord.reason.empty() ? "Android has no keycode for this hotkey" : chord.reason;
            return result;
        }
        if (!on_fire) {
            result.body = "hotkey handler is empty";
            return result;
        }
        {
            std::lock_guard<std::mutex> lock(g_mu);
            bool replaced = false;
            for (HotkeyBinding& binding : g_bindings) {
                if (hotkey_chord::same(binding.chord, chord)) {
                    binding.handler = std::move(on_fire);
                    replaced = true;
                    break;
                }
            }
            if (!replaced) {
                g_bindings.push_back(HotkeyBinding{chord, std::move(on_fire)});
            }
        }
        android_jni::Frame frame;
        if (frame.failed() || !install_hook(frame)) {
            result.body = "Android window callback is not available";
            return result;
        }
        result.ok = g_hooked;
        if (!result.ok) {
            result.body = "Android window callback is not available";
        }
        return result;
    }
};

#endif

}  // namespace

void register_android_hotkey(ExtensionRegistry& registry) {
#if defined(__ANDROID__)
    registry.add_hotkey(std::make_unique<AndroidHotkey>());
#else
    (void)registry;
#endif
}

}  // namespace zui
