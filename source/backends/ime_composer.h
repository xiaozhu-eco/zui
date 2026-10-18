#ifndef ZUI_BACKENDS_IME_COMPOSER_H
#define ZUI_BACKENDS_IME_COMPOSER_H

// Shared IME policy. GLFW feeds this instead of hardcoding `composing` false.
// SDL keeps its own text-editing events and still calls `ime_set_caret` so the
// OS composition window tracks the caret. Always compiled; GLFW may be off.

#include "backends/window.h"

#include <zui/export.h>

#include <string>

namespace zui {
namespace backends {

/// One OS reading. `poll_hwnd` builds this. Tests pass it to `observe`.
struct ImeSample {
    std::string comp;
    std::string result;
    bool enter = false;
};

/// The only composition transition. Identical strings produce `None`.
enum class ImeAction { None, Preedit, Commit, End };

class ImeComposer {
public:
    /// One decision for @p sample. The same `comp` twice is `None`. Enter while
    /// composing commits the preedit and keeps `last_comp_` equal to that
    /// GCS_COMPSTR. The next sample of the same string is `None`, so it is not
    /// inserted again. A result equal to the string just committed is also `None`.
    ImeAction observe(const ImeSample& sample) {
        if (sample.enter && composing_) {
            const std::string text = preedit_.empty() ? sample.comp : preedit_;
            // Keep the still-open composition string. Clearing it here makes the
            // next poll treat that same GCS_COMPSTR as a new preedit.
            last_comp_ = sample.comp.empty() ? text : sample.comp;
            seen_committed_ = text;
            if (!sample.result.empty()) {
                seen_result_ = sample.result;
            }
            commit_text(text);
            return ImeAction::Commit;
        }
        // A result that is already in the context before any preedit is not a commit.
        if (!sample.result.empty() && sample.result != seen_result_ &&
            sample.result != seen_committed_) {
            if (seen_result_.empty() && !composing_ && last_comp_.empty() && !sample.enter &&
                sample.comp.empty()) {
                seen_result_ = sample.result;
                return ImeAction::None;
            }
            if (seen_result_.empty() && !composing_ && last_comp_.empty() && !sample.enter) {
                seen_result_ = sample.result;
            } else {
                seen_result_ = sample.result;
                last_comp_ = sample.comp;
                seen_committed_ = sample.result;
                commit_text(sample.result);
                return ImeAction::Commit;
            }
        }
        if (sample.comp == last_comp_) {
            return ImeAction::None;
        }
        if (!sample.comp.empty()) {
            last_comp_ = sample.comp;
            preedit_text(sample.comp);
            return ImeAction::Preedit;
        }
        last_comp_.clear();
        if (composing_) {
            composing_ = false;
            phase_ = 3;
            preedit_.clear();
            commit_.clear();
        }
        return ImeAction::End;
    }

    /// GLFW's key callback records Enter. `poll_hwnd` puts it on the next sample.
    /// The callback must not commit before that poll.
    void note_enter() { pending_enter_ = true; }

    bool composing() const { return composing_; }

    void annotate(KeyPress& press) const { press.composing = composing_; }

    /// Copies the latched phase and any committed text onto @p frame, then clears them.
    /// The preedit is written to `composition`, not `text`, so a later poll of the same
    /// preedit does not insert it again.
    void apply(TextInputFrame& frame) {
        frame.composing = composing_;
        if (phase_ != 0) {
            frame.composition_phase = phase_;
        }
        if (!commit_.empty()) {
            // The char callback may already have appended this commit, or Enter's
            // CR. Replacing drops that echo so the string is inserted once.
            frame.text = std::move(commit_);
            frame.composition.clear();
            commit_.clear();
            preedit_.clear();
        } else if (composing_) {
            frame.composition = preedit_;
            // A char the IME echoed this frame is the preedit, not a second insert.
            frame.text.clear();
        } else if (phase_ == 3) {
            // Composition ended with no string. Drop a CR/LF the key echo left behind.
            frame.text.clear();
            frame.composition.clear();
        }
        phase_ = 0;
    }

    /// Reads the OS composition string when @p hwnd has an IME context. No-op elsewhere.
    /// GLFW's poll calls this, then `apply`, so a composition reaches the frame.
    ZUI_EXPORT void poll_hwnd(void* hwnd);

private:
    friend void ime_note_composition(ImeComposer& ime, void* hwnd, unsigned message);

    void preedit_text(std::string text) {
        preedit_ = std::move(text);
        if (!composing_) {
            composing_ = true;
            phase_ = 1;
            return;
        }
        phase_ = 2;
    }

    void commit_text(std::string text) {
        commit_ = std::move(text);
        preedit_.clear();
        composing_ = false;
        phase_ = 3;
    }

    bool composing_ = false;
    bool pending_enter_ = false;
    int phase_ = 0;
    std::string commit_;
    std::string preedit_;
    std::string last_comp_;
    std::string seen_result_;
    std::string seen_committed_;
    bool noted_ = false;
    std::string noted_comp_;
    std::string noted_result_;
};

/// Positions the OS IME caret. @return false when the platform cannot.
ZUI_EXPORT bool ime_set_caret(void* hwnd, float x, float y, float w, float h);

/// Reads the caret rectangle last accepted by the OS, in window pixels.
ZUI_EXPORT bool ime_read_caret(void* hwnd, long& left, long& top, long& right, long& bottom);

}  // namespace backends
}  // namespace zui

#endif
