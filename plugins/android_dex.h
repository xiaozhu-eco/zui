#ifndef ZUI_PLUGINS_ANDROID_DEX_H
#define ZUI_PLUGINS_ANDROID_DEX_H

// ART rejects JNI DefineClass of a JVM class file. Callback classes are loaded
// from the APK (zui_pack compiles the plugin Java into classes.dex) or, when
// that class is absent, from the DEX bytes the plugin carries.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#if defined(__ANDROID__)
#include "plugins/android_jni.h"

#include <cstdio>
#include <string>
#endif

namespace zui {
namespace media_key_android {
const std::uint8_t* callback_dex(std::size_t& size);
}
namespace hotkey_android {
const std::uint8_t* callback_dex(std::size_t& size);
}

namespace android_dex {

inline std::uint32_t read_u32(const std::uint8_t* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) | (static_cast<std::uint32_t>(bytes[3]) << 24);
}

inline std::uint32_t adler32(const std::uint8_t* data, std::size_t len) {
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (std::size_t i = 0; i < len; ++i) {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

// True when the buffer is a DEX container whose header checksum matches.
inline bool payload_is_dex(const std::uint8_t* bytes, std::size_t size) {
    if (bytes == nullptr || size < 0x70 || size > 0x7fffffffu) {
        return false;
    }
    if (std::memcmp(bytes, "dex\n", 4) != 0 || bytes[7] != 0) {
        return false;
    }
    if (read_u32(bytes + 0x20) != static_cast<std::uint32_t>(size)) {
        return false;
    }
    if (read_u32(bytes + 0x24) != 0x70u || read_u32(bytes + 0x28) != 0x12345678u) {
        return false;
    }
    return adler32(bytes + 12, size - 12) == read_u32(bytes + 8);
}

inline bool payload_contains(const std::uint8_t* bytes, std::size_t size, const char* ascii) {
    if (bytes == nullptr || ascii == nullptr) {
        return false;
    }
    const std::size_t n = std::strlen(ascii);
    if (n == 0 || size < n) {
        return false;
    }
    return std::search(bytes, bytes + size, ascii, ascii + n) != bytes + size;
}

#if defined(__ANDROID__)

inline bool jni_failed(JNIEnv* env) {
    if (env != nullptr && env->ExceptionCheck()) {
        env->ExceptionClear();
        return true;
    }
    return env == nullptr;
}

inline bool absolute_path(JNIEnv* env, jobject file, std::string& out) {
    jclass cls = env->GetObjectClass(file);
    jmethodID mid = cls != nullptr ? env->GetMethodID(cls, "getAbsolutePath", "()Ljava/lang/String;") : nullptr;
    if (mid == nullptr || jni_failed(env)) {
        return false;
    }
    jstring jpath = static_cast<jstring>(env->CallObjectMethod(file, mid));
    if (jpath == nullptr || jni_failed(env)) {
        return false;
    }
    const char* utf = env->GetStringUTFChars(jpath, nullptr);
    if (utf == nullptr) {
        return false;
    }
    out.assign(utf);
    env->ReleaseStringUTFChars(jpath, utf);
    return !out.empty();
}

inline jclass load_from(JNIEnv* env, jobject loader, const char* dotted) {
    if (loader == nullptr || dotted == nullptr) {
        return nullptr;
    }
    jclass loader_cls = env->GetObjectClass(loader);
    jmethodID load = loader_cls != nullptr
                         ? env->GetMethodID(loader_cls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;")
                         : nullptr;
    if (load == nullptr || jni_failed(env)) {
        return nullptr;
    }
    jstring name = env->NewStringUTF(dotted);
    jobject loaded = name != nullptr ? env->CallObjectMethod(loader, load, name) : nullptr;
    if (loaded == nullptr || jni_failed(env)) {
        return nullptr;
    }
    return static_cast<jclass>(loaded);
}

inline jobject app_class_loader(JNIEnv* env, jobject app) {
    jclass app_cls = env->GetObjectClass(app);
    jmethodID get = app_cls != nullptr ? env->GetMethodID(app_cls, "getClassLoader", "()Ljava/lang/ClassLoader;")
                                      : nullptr;
    if (get == nullptr || jni_failed(env)) {
        return nullptr;
    }
    jobject loader = env->CallObjectMethod(app, get);
    if (loader == nullptr || jni_failed(env)) {
        return nullptr;
    }
    return loader;
}

inline void keep_loader(JNIEnv* env, jobject loader, jobject& kept) {
    if (loader != nullptr && kept == nullptr) {
        kept = env->NewGlobalRef(loader);
    }
}

inline jclass load_in_memory(JNIEnv* env, jobject parent, const char* dotted, const std::uint8_t* bytes,
                             std::size_t size, jobject& kept) {
    jclass loader_cls = env->FindClass("dalvik/system/InMemoryDexClassLoader");
    if (loader_cls == nullptr || jni_failed(env)) {
        return nullptr;
    }
    jobject buffer = env->NewDirectByteBuffer(const_cast<std::uint8_t*>(bytes), static_cast<jlong>(size));
    if (buffer == nullptr || jni_failed(env)) {
        return nullptr;
    }
    jclass buffer_cls = env->FindClass("java/nio/ByteBuffer");
    if (buffer_cls == nullptr || jni_failed(env)) {
        return nullptr;
    }
    jobjectArray buffers = env->NewObjectArray(1, buffer_cls, buffer);
    // The array constructor is the API 26 entry. The single-buffer overload is newer.
    jmethodID ctor = env->GetMethodID(loader_cls, "<init>", "([Ljava/nio/ByteBuffer;Ljava/lang/ClassLoader;)V");
    if (ctor == nullptr || buffers == nullptr || jni_failed(env)) {
        return nullptr;
    }
    jobject loader = env->NewObject(loader_cls, ctor, buffers, parent);
    if (loader == nullptr || jni_failed(env)) {
        return nullptr;
    }
    keep_loader(env, loader, kept);
    return load_from(env, loader, dotted);
}

inline jclass load_from_file(JNIEnv* env, jobject app, jobject parent, const char* dotted, const std::uint8_t* bytes,
                             std::size_t size, const char* cache_name, jobject& kept) {
    jclass file_cls = env->FindClass("java/io/File");
    if (file_cls == nullptr || jni_failed(env)) {
        return nullptr;
    }
    jmethodID cache_dir = env->GetMethodID(env->GetObjectClass(app), "getCodeCacheDir", "()Ljava/io/File;");
    if (cache_dir == nullptr || jni_failed(env)) {
        return nullptr;
    }
    jobject dir = env->CallObjectMethod(app, cache_dir);
    if (dir == nullptr || jni_failed(env)) {
        return nullptr;
    }
    jmethodID child_ctor = env->GetMethodID(file_cls, "<init>", "(Ljava/io/File;Ljava/lang/String;)V");
    jstring jname = cache_name != nullptr ? env->NewStringUTF(cache_name) : nullptr;
    if (child_ctor == nullptr || jname == nullptr || jni_failed(env)) {
        return nullptr;
    }
    jobject file = env->NewObject(file_cls, child_ctor, dir, jname);
    if (file == nullptr || jni_failed(env)) {
        return nullptr;
    }
    std::string dir_path;
    std::string file_path;
    if (!absolute_path(env, dir, dir_path) || !absolute_path(env, file, file_path)) {
        return nullptr;
    }
    FILE* out = std::fopen(file_path.c_str(), "wb");
    if (out == nullptr) {
        return nullptr;
    }
    const std::size_t wrote = std::fwrite(bytes, 1, size, out);
    std::fclose(out);
    if (wrote != size) {
        return nullptr;
    }
    jclass dex_cls = env->FindClass("dalvik/system/DexClassLoader");
    if (dex_cls == nullptr || jni_failed(env)) {
        return nullptr;
    }
    jmethodID ctor = env->GetMethodID(
        dex_cls, "<init>", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/ClassLoader;)V");
    if (ctor == nullptr || jni_failed(env)) {
        return nullptr;
    }
    jstring jfile = env->NewStringUTF(file_path.c_str());
    jstring jdir = env->NewStringUTF(dir_path.c_str());
    if (jfile == nullptr || jdir == nullptr) {
        return nullptr;
    }
    jobject loader = env->NewObject(dex_cls, ctor, jfile, jdir, nullptr, parent);
    if (loader == nullptr || jni_failed(env)) {
        return nullptr;
    }
    keep_loader(env, loader, kept);
    return load_from(env, loader, dotted);
}

// Application ClassLoader first (the class d8 put in the APK). Otherwise the
// plugin's own DEX, which ART can open. Never JNI DefineClass.
inline jclass load_dex_class(android_jni::Frame& frame, const char* dotted, const char* descriptor,
                             const std::uint8_t* bytes, std::size_t size, const char* cache_name, jobject& keep_loader) {
    if (frame.env() == nullptr || frame.application() == nullptr || dotted == nullptr) {
        return nullptr;
    }
    if (jclass packaged = frame.load(dotted)) {
        return packaged;
    }
    if (!payload_is_dex(bytes, size) || !payload_contains(bytes, size, descriptor)) {
        return nullptr;
    }
    JNIEnv* env = frame.env();
    jobject parent = app_class_loader(env, frame.application());
    if (parent == nullptr) {
        return nullptr;
    }
    if (jclass memory = load_in_memory(env, parent, dotted, bytes, size, keep_loader)) {
        return memory;
    }
    return load_from_file(env, frame.application(), parent, dotted, bytes, size, cache_name, keep_loader);
}
#endif

}  // namespace android_dex
}  // namespace zui

#endif
