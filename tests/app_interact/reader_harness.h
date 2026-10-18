#ifndef ZUI_READER_HARNESS_H
#define ZUI_READER_HARNESS_H

#include "../../example/applications/reader/cpp/session.h"
#include "../../example/applications/reader/cpp/page_mode.h"

#include "app/frame_session.h"
#include "log/log.h"
#include "perf/perf_budget.h"

#include <zui/app.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <utility>

#ifndef ZUI_SOURCE_DIR
#error ZUI_SOURCE_DIR required
#endif

namespace reader_test {

namespace fs = std::filesystem;

inline void force_noop_platform() {
#if defined(_WIN32)
    _putenv_s("ZUI_PLATFORM", "noop");
#else
    setenv("ZUI_PLATFORM", "noop", 1);
#endif
}

inline fs::path source_reader_html() {
    return fs::path(ZUI_SOURCE_DIR) / "example" / "applications" / "reader" / "reader.html";
}

inline fs::path fixture_dir() {
    return fs::path(ZUI_SOURCE_DIR) / "tests" / "fixtures" / "reader";
}

struct ReaderHarness {
    zui::App app;
    reader::Session session;
    fs::path root;
    fs::path books_dir;
    fs::path progress_path;

    ReaderHarness()
        // NoopPlatform: pick_open returns Unsupported (no Win32 modal).
        : app((force_noop_platform(), 0), nullptr) {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root = fs::temp_directory_path() / ("zui_reader_harness_" + std::to_string(stamp));
        books_dir = root / "books";
        progress_path = root / "progress.txt";
        std::error_code ec;
        fs::create_directories(books_dir, ec);
        fs::copy_file(fixture_dir() / "sample.txt", books_dir / "sample.txt",
                      fs::copy_options::overwrite_existing, ec);
        fs::copy_file(fixture_dir() / "catalog.json", books_dir / "catalog.json",
                      fs::copy_options::overwrite_existing, ec);

        if (!app.document().load(source_reader_html().string())) {
            ZUI_FILE_ERROR("ReaderHarness: failed to load reader.html");
        }
        session.app = &app;
        session.books_dir = books_dir;
        session.progress_path = progress_path;
        session.progress = reader::load_progress(progress_path);
        reader::bind_actions(session);
        (void)zui::frame::Access::prepare_interaction(app, 1100, 720);
        reader::fill_shelf(session);
        zui::frame::Access::sync_observation(app);
    }

    ~ReaderHarness() {
        // Cancel deferred open/turn lambdas that capture session before members die.
        ++session.open_token;
        ++session.page.turn_token;
        zui::frame::Access::flush_deferred(app);
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    ReaderHarness(const ReaderHarness&) = delete;
    ReaderHarness& operator=(const ReaderHarness&) = delete;

    template <typename Pred>
    bool settle(Pred&& pred, int max = 64) {
        for (int i = 0; i < max; ++i) {
            zui::frame::Access::flush_deferred(app);
            zui::frame::Access::sync_observation(app);
            if (pred()) {
                return true;
            }
        }
        return false;
    }

    bool open_and_settle(int index) {
        reader::open_book(session, index);
        return settle([&] { return session.current_chapter >= 0; });
    }

    /// Observe until a frame does no work, so a following `measure` starts from quiet.
    void settle_idle(int max_frames = 16) {
        for (int i = 0; i < max_frames; ++i) {
            zui::frame::Access::sync_observation(app);
            const zui::perf::FrameWork f = zui::perf::last_frame_work();
            if (f.cascade_visited == 0 && f.cascade_recomputed == 0 && f.laid_out == 0 &&
                f.boxes_created == 0 && f.painted_walked == 0) {
                return;
            }
        }
    }

    /// The work of one operation, summed from the quiet state back to quiet (docs/PERF.md 6.1).
    template <typename Fn>
    zui::perf::FrameWork measure(Fn&& action, int max_frames = 16) {
        settle_idle();
        action();
        zui::perf::FrameWork total = zui::perf::last_frame_work();
        for (int i = 0; i < max_frames; ++i) {
            zui::frame::Access::sync_observation(app);
            const zui::perf::FrameWork frame = zui::perf::last_frame_work();
            total.cascade_visited += frame.cascade_visited;
            total.cascade_recomputed += frame.cascade_recomputed;
            total.style_interned += frame.style_interned;
            total.boxes_created += frame.boxes_created;
            total.laid_out += frame.laid_out;
            total.painted_walked += frame.painted_walked;
            total.relowers += frame.relowers;
            total.flow_remeasures += frame.flow_remeasures;
            if (frame.cascade_visited == 0 && frame.cascade_recomputed == 0 && frame.laid_out == 0 &&
                frame.boxes_created == 0 && frame.painted_walked == 0) {
                break;
            }
        }
        return total;
    }

    /// Show reading chrome so toolbar controls (toc_toggle, …) accept hit-tests.
    void show_chrome() {
        if (!session.ui_on) {
            reader::tap_reading(session);
        }
        zui::frame::Access::sync_observation(app);
    }

    bool open_toc() {
        show_chrome();
        if (!zui::frame::Access::click_id(app, "toc_toggle")) {
            reader::toggle_toc(session);
        }
        return settle([&] { return session.toc_ready && !session.toc_hidden; }, 16);
    }

    void click_back() { reader::back_to_shelf(session); }
    void click_settings() { reader::toggle_settings(session); }
    void click_import() { reader::import_books(session); }
    void click_toc() { (void)open_toc(); }
    void tap_reading() { reader::tap_reading(session); }
};

}  // namespace reader_test

#endif
