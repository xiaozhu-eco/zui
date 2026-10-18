#include "cpp/session.h"

#include <zui/app.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string_view>

int main(int argc, char** argv) {
    if (argc > 1 && std::string_view(argv[1]) == "--count-chapters") {
        const std::filesystem::path exe_dir = std::filesystem::absolute(argv[0]).parent_path();
        std::filesystem::path books_dir = exe_dir / "books";
        if (argc > 2) {
            books_dir = argv[2];
        }
        std::error_code ec;
        for (std::filesystem::directory_iterator it(books_dir, ec);
             !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
            std::error_code file_ec;
            if (!it->is_regular_file(file_ec) || !reader::is_book(it->path())) {
                continue;
            }
            const reader::Book book = reader::load_book(it->path());
            std::fprintf(stdout, "%s\t%d\n", book.key.c_str(),
                         static_cast<int>(book.chapters.size()));
        }
        return 0;
    }
    if (argc > 1 && std::string_view(argv[1]) == "--dump-chapters") {
        const std::filesystem::path exe_dir = std::filesystem::absolute(argv[0]).parent_path();
        std::filesystem::path book_path;
        if (argc > 2) {
            book_path = argv[2];
        } else {
            std::error_code ec;
            for (std::filesystem::directory_iterator it(exe_dir / "books", ec);
                 !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
                std::error_code file_ec;
                if (it->is_regular_file(file_ec) && reader::is_book(it->path())) {
                    book_path = it->path();
                    break;
                }
            }
        }
        const reader::Book book = reader::load_book(book_path);
        const int limit = argc > 3 ? std::atoi(argv[3]) : 3;
        std::fprintf(stdout, "file=%s chapters=%d chars=%lld\n", book.key.c_str(),
                     static_cast<int>(book.chapters.size()), book.chars);
        for (int i = 0; i < static_cast<int>(book.chapters.size()) && i < limit; ++i) {
            const reader::Chapter& ch = book.chapters[static_cast<size_t>(i)];
            std::fprintf(stdout, "[%d] title=%s paras=%d\n", i, ch.title.c_str(),
                         static_cast<int>(ch.paragraphs.size()));
            const int show = (std::min)(5, static_cast<int>(ch.paragraphs.size()));
            for (int p = 0; p < show; ++p) {
                std::fprintf(stdout, "  p%d: %s\n", p,
                             ch.paragraphs[static_cast<size_t>(p)].c_str());
            }
        }
        return 0;
    }

    zui::App app(argc, argv);

    const std::filesystem::path exe_dir = std::filesystem::absolute(argv[0]).parent_path();
    std::filesystem::path html = exe_dir / "reader.html";
    if (argc > 1) {
        html = argv[1];
    }
    std::filesystem::path books_dir = html.parent_path() / "books";
    if (argc > 2) {
        books_dir = argv[2];
    }
    const std::filesystem::path progress_path = exe_dir / "reader-progress.txt";
    const std::filesystem::path marks_path = exe_dir / "reader-marks.json";

    if (!app.document().load(html.string())) {
        return 1;
    }

    reader::Session session;
    session.app = &app;
    session.books_dir = books_dir;
    session.progress_path = progress_path;
    session.marks_path = marks_path;
    session.progress = reader::load_progress(progress_path);
    reader::load_session_marks(session);
    reader::bind_actions(session);
    reader::show(session);
    // No-op unless a `ZUI_READER_AUTO*` variable is set; the repro scripts in tools/ drive the
    // example through it, so an ordinary run pays one `getenv` per hook.
    reader::arm_debug_harness(session);
    return app.run();
}
