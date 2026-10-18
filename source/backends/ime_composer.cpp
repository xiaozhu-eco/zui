#include "backends/ime_composer.h"

#include <string>
#include <unordered_map>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <imm.h>
#endif

namespace zui {
namespace backends {

namespace {

#if defined(_WIN32)

std::string wide_to_utf8(const wchar_t* text, int chars) {
    if (text == nullptr || chars <= 0) {
        return {};
    }
    const int n = WideCharToMultiByte(CP_UTF8, 0, text, chars, nullptr, 0, nullptr, nullptr);
    if (n <= 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, chars, out.data(), n, nullptr, nullptr);
    return out;
}

std::string composition_string(HIMC imc, DWORD index) {
    const LONG bytes = ImmGetCompositionStringW(imc, index, nullptr, 0);
    if (bytes <= 0) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(bytes / sizeof(wchar_t)), L'\0');
    const LONG got = ImmGetCompositionStringW(imc, index, wide.data(), bytes);
    if (got <= 0) {
        return {};
    }
    wide.resize(static_cast<std::size_t>(got / sizeof(wchar_t)));
    return wide_to_utf8(wide.c_str(), static_cast<int>(wide.size()));
}

struct ImeHook {
    WNDPROC previous = nullptr;
    ImeComposer* composer = nullptr;
};

std::unordered_map<HWND, ImeHook>& ime_hooks() {
    static std::unordered_map<HWND, ImeHook> hooks;
    return hooks;
}

LRESULT CALLBACK ime_subclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto& hooks = ime_hooks();
    auto it = hooks.find(hwnd);
    WNDPROC previous = it != hooks.end() ? it->second.previous : nullptr;
    ImeComposer* composer = it != hooks.end() ? it->second.composer : nullptr;
    if (composer != nullptr &&
        (msg == WM_IME_COMPOSITION || msg == WM_IME_STARTCOMPOSITION || msg == WM_IME_ENDCOMPOSITION)) {
        ime_note_composition(*composer, hwnd, msg);
    }
    // DefWindowProc turns a result into WM_CHAR. The composer inserts that
    // string itself, so forwarding it would commit the text twice.
    if (msg == WM_IME_COMPOSITION &&
        (static_cast<DWORD>(lp) & GCS_RESULTSTR) != 0) {
        return 0;
    }
    if (msg == WM_NCDESTROY) {
        if (it != hooks.end()) {
            previous = it->second.previous;
            hooks.erase(it);
        }
        if (previous != nullptr && previous != ime_subclass) {
            SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(previous));
            return CallWindowProcW(previous, hwnd, msg, wp, lp);
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    if (previous != nullptr && previous != ime_subclass) {
        return CallWindowProcW(previous, hwnd, msg, wp, lp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void ensure_hook(HWND hwnd, ImeComposer* composer) {
    auto& hooks = ime_hooks();
    auto it = hooks.find(hwnd);
    if (it != hooks.end()) {
        it->second.composer = composer;
        return;
    }
    auto* current = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hwnd, GWLP_WNDPROC));
    if (current == ime_subclass) {
        return;
    }
    ImeHook hook;
    hook.previous = current;
    hook.composer = composer;
    hooks.emplace(hwnd, hook);
    SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ime_subclass));
}

#endif

}  // namespace

void ime_note_composition(ImeComposer& ime, void* hwnd, unsigned message) {
#if defined(_WIN32)
    if (hwnd == nullptr) {
        return;
    }
    HWND window = static_cast<HWND>(hwnd);
    if (message == WM_IME_ENDCOMPOSITION) {
        ime.noted_ = true;
        ime.noted_comp_.clear();
        return;
    }
    HIMC imc = ImmGetContext(window);
    if (imc == nullptr) {
        return;
    }
    ime.noted_result_ = composition_string(imc, GCS_RESULTSTR);
    ime.noted_comp_ = composition_string(imc, GCS_COMPSTR);
    ime.noted_ = true;
    ImmReleaseContext(window, imc);
#else
    (void)ime;
    (void)hwnd;
    (void)message;
#endif
}

void ImeComposer::poll_hwnd(void* hwnd) {
    ImeSample sample;
    sample.enter = pending_enter_;
    pending_enter_ = false;
#if defined(_WIN32)
    if (hwnd != nullptr) {
        HWND window = static_cast<HWND>(hwnd);
        if (IsWindow(window)) {
            ensure_hook(window, this);
            HIMC imc = ImmGetContext(window);
            if (imc != nullptr) {
                sample.result = composition_string(imc, GCS_RESULTSTR);
                sample.comp = composition_string(imc, GCS_COMPSTR);
                ImmReleaseContext(window, imc);
            }
            if (noted_) {
                if (sample.comp.empty() && !noted_comp_.empty()) {
                    sample.comp = noted_comp_;
                }
                if (sample.result.empty() && !noted_result_.empty()) {
                    sample.result = noted_result_;
                }
                noted_ = false;
                noted_comp_.clear();
                noted_result_.clear();
            }
        }
    }
#else
    (void)hwnd;
#endif
    observe(sample);
}

}  // namespace backends
}  // namespace zui
