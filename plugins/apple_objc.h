#ifndef ZUI_PLUGINS_APPLE_OBJC_H
#define ZUI_PLUGINS_APPLE_OBJC_H

// Objective-C message send without a .mm translation unit. Windows builds
// include this header and compile none of it.

#include <string>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#include <objc/message.h>
#include <objc/runtime.h>

namespace zui {
namespace apple_objc {

inline id cls(const char* name) { return reinterpret_cast<id>(objc_getClass(name)); }

inline SEL sel(const char* name) { return sel_registerName(name); }

template <typename Ret, typename... Args>
inline Ret msg(id object, SEL method, Args... args) {
    using Fn = Ret (*)(id, SEL, Args...);
    return reinterpret_cast<Fn>(objc_msgSend)(object, method, args...);
}

// `owned` must outlive the call. stringWithUTF8String: copies during the call.
inline id ns_string_kept(const std::string& owned) {
    return msg<id>(cls("NSString"), sel("stringWithUTF8String:"), owned.c_str());
}

}  // namespace apple_objc
}  // namespace zui

#endif
#endif
