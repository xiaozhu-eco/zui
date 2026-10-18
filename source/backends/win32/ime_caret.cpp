#include "backends/ime_composer.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <imm.h>
#endif

namespace zui {
namespace backends {

bool ime_set_caret(void* hwnd, float x, float y, float w, float h) {
#if defined(_WIN32)
    if (hwnd == nullptr) {
        return false;
    }
    HWND window = static_cast<HWND>(hwnd);
    if (!IsWindow(window)) {
        return false;
    }
    HIMC imc = ImmGetContext(window);
    HIMC created = nullptr;
    if (imc == nullptr) {
        // ImmAssociateContext returns the previous context. NULL means the window had none,
        // which is success, so that return cannot be tested as a boolean.
        created = ImmCreateContext();
        if (created == nullptr) {
            return false;
        }
        ImmAssociateContext(window, created);
        imc = ImmGetContext(window);
        if (imc == nullptr) {
            ImmDestroyContext(created);
            return false;
        }
    }
    const LONG left = static_cast<LONG>(x);
    const LONG top = static_cast<LONG>(y);
    const LONG right = static_cast<LONG>(x + (w > 1.f ? w : 1.f));
    const LONG bottom = static_cast<LONG>(y + (h > 1.f ? h : 1.f));
    COMPOSITIONFORM form{};
    form.dwStyle = CFS_RECT;
    form.ptCurrentPos.x = left;
    form.ptCurrentPos.y = top;
    form.rcArea.left = left;
    form.rcArea.top = top;
    form.rcArea.right = right;
    form.rcArea.bottom = bottom;
    const BOOL ok = ImmSetCompositionWindow(imc, &form);
    // A zero candidate rectangle makes the system IME clear the composition rect.
    // Match the caret rectangle, the same shape SDL gives the candidate window.
    CANDIDATEFORM candidate{};
    candidate.dwIndex = 0;
    candidate.dwStyle = CFS_EXCLUDE;
    candidate.ptCurrentPos.x = left;
    candidate.ptCurrentPos.y = top;
    candidate.rcArea = form.rcArea;
    ImmSetCandidateWindow(imc, &candidate);
    ImmReleaseContext(window, imc);
    return ok != FALSE;
#else
    (void)hwnd;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    return false;
#endif
}

bool ime_read_caret(void* hwnd, long& left, long& top, long& right, long& bottom) {
#if defined(_WIN32)
    if (hwnd == nullptr) {
        return false;
    }
    HWND window = static_cast<HWND>(hwnd);
    HIMC imc = ImmGetContext(window);
    if (imc == nullptr) {
        return false;
    }
    COMPOSITIONFORM form{};
    const BOOL ok = ImmGetCompositionWindow(imc, &form);
    ImmReleaseContext(window, imc);
    if (ok == FALSE) {
        return false;
    }
    left = form.rcArea.left;
    top = form.rcArea.top;
    right = form.rcArea.right;
    bottom = form.rcArea.bottom;
    return true;
#else
    (void)hwnd;
    (void)left;
    (void)top;
    (void)right;
    (void)bottom;
    return false;
#endif
}

}  // namespace backends
}  // namespace zui
