#include "css/engine/engine.h"
#include "html/document.h"
#include "layout/engine.h"
#include "layout/paint_map.h"
#include "layout/scrollbar_h.h"
#include "layout/scrollbar_v.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <direct.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#endif

// Internal UI probe for the ui-visual-diff skill: same static layout path as zui_dump,
// plus scroll control via html::Node::scroll_x/scroll_y + LayoutEngine::sync_world.
// Scroll lives only in this test program (never public App API).

namespace {

struct Options {
    std::string path;
    bool brief = false;
    bool list_scrollers = false;
    bool dump_dom = false;
    bool scroll_fraction = false;
    bool have_scroll_top = false;
    bool have_scroll_left = false;
    bool have_scroll_id = false;
    std::string scroll_id;
    float scroll_top = 0;
    float scroll_left = 0;
    std::vector<float> scroll_tops;
    float viewport_w = 1100.0f;
    float viewport_h = 720.0f;
    int wait_ms = 0;
    std::string out_path;
};

bool parse_size(const char* text, float& w, float& h) {
    char* end = nullptr;
    w = std::strtof(text, &end);
    if (end == nullptr || *end != 'x') {
        return false;
    }
    h = std::strtof(end + 1, nullptr);
    return w > 0 && h > 0;
}

std::vector<float> parse_csv_floats(const std::string& text) {
    std::vector<float> out;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && (text[i] == ' ' || text[i] == ',')) {
            ++i;
        }
        if (i >= text.size()) {
            break;
        }
        char* end = nullptr;
        const float v = std::strtof(text.c_str() + i, &end);
        if (end == text.c_str() + i) {
            break;
        }
        out.push_back(v);
        i = static_cast<size_t>(end - text.c_str());
    }
    return out;
}

bool ensure_parent_dirs(const std::string& path) {
    if (path.empty()) {
        return true;
    }
    size_t pos = 0;
    while (true) {
        pos = path.find_first_of("/\\", pos + 1);
        if (pos == std::string::npos) {
            break;
        }
        const std::string dir = path.substr(0, pos);
        if (dir.empty() || dir.back() == ':') {
            continue;
        }
#if defined(_WIN32)
        _mkdir(dir.c_str());
#else
        mkdir(dir.c_str(), 0755);
#endif
    }
    return true;
}

float padding_box_w(const zui::layout::Box& box) {
    return (std::max)(0.0f,
                      box.geo.width - box.geo.border.horizontal() - box.geo.padding.horizontal());
}

float padding_box_h(const zui::layout::Box& box) {
    return (std::max)(0.0f,
                      box.geo.height - box.geo.border.vertical() - box.geo.padding.vertical());
}

float max_scroll_y(const zui::layout::Box& box) {
    return (std::max)(0.0f, box.geo.content_height - padding_box_h(box));
}

float max_scroll_x(const zui::layout::Box& box) {
    return (std::max)(0.0f, box.geo.content_width - padding_box_w(box));
}

// Match app.cpp nearest_scroller: overflow auto/scroll marks a scroller even when max==0.
bool overflow_can_scroll_y(const zui::layout::Box& box) {
    const auto m = box.style().overflow_y;
    return m == zui::layout::OverflowMode::Auto || m == zui::layout::OverflowMode::Scroll;
}

bool overflow_can_scroll_x(const zui::layout::Box& box) {
    const auto m = box.style().overflow_x;
    return m == zui::layout::OverflowMode::Auto || m == zui::layout::OverflowMode::Scroll;
}

bool is_scrollable(const zui::layout::Box& box) {
    return overflow_can_scroll_y(box) || overflow_can_scroll_x(box);
}

const char* overflow_name(zui::layout::OverflowMode m) {
    switch (m) {
        case zui::layout::OverflowMode::Hidden:
            return "hidden";
        case zui::layout::OverflowMode::Auto:
            return "auto";
        case zui::layout::OverflowMode::Scroll:
            return "scroll";
        default:
            return "visible";
    }
}

void dump(const zui::layout::Box* b, int depth) {
    for (int i = 0; i < depth; ++i) {
        std::cout << "  ";
    }
    std::string tag = b->node ? b->node->tag : std::string("anon");
    std::string id = b->node ? b->node->id : "";
    std::string cls = b->node ? b->node->class_name : "";
    const zui::layout::LayoutStyle* st = b->style_ptr.get();
    std::string fg = "flow";
    if (st != nullptr) {
        if (st->inner == zui::layout::DisplayInner::Flex) {
            fg = "flex";
        } else if (st->inner == zui::layout::DisplayInner::Grid) {
            fg = "grid";
        }
    }
    float grow = 0;
    if (b->node != nullptr) {
        if (const std::string* value = b->node->style("flex-grow")) {
            grow = std::strtof(value->c_str(), nullptr);
        }
    }
    std::cout << tag;
    if (!id.empty()) {
        std::cout << "#" << id;
    }
    if (!cls.empty()) {
        std::cout << "." << cls;
    }
    std::cout << " " << fg << " box=" << b->geo.width << "x" << b->geo.height << " @(" << b->geo.x
              << "," << b->geo.y << ")"
              << " world=(" << b->geo.world_x << "," << b->geo.world_y << ")"
              << " content=" << b->geo.content_width << "x" << b->geo.content_height
              << " grow=" << grow;
    if (b->node != nullptr && (b->node->scroll_x != 0 || b->node->scroll_y != 0)) {
        std::cout << " scroll=(" << b->node->scroll_x << "," << b->node->scroll_y << ")";
    }
    if (!b->text.empty()) {
        std::cout << " text=\"" << b->text << "\" baseline=" << b->geo.baseline;
    }
    std::cout << "\n";
    for (const auto& run : b->geo.runs) {
        for (int i = 0; i < depth + 1; ++i) {
            std::cout << "  ";
        }
        const float base_y = b->geo.world_y + run.y + run.ascent;
        std::cout << "run \"" << run.text << "\" local=(" << run.x << "," << run.y
                  << ") ascent=" << run.ascent << " world_baseline=" << base_y << "\n";
    }
    for (const zui::layout::Box* ch : b->children) {
        dump(ch, depth + 1);
    }
}

void dump_brief(const zui::layout::Box* box) {
    const std::string tag = box->node != nullptr ? box->node->tag : std::string("anon");
    const std::string id = box->node != nullptr ? box->node->id : std::string();
    const std::string cls = box->node != nullptr ? box->node->class_name : std::string();
    std::cout << "box|" << tag << "|" << id << "|" << cls << "|" << box->geo.world_x << "|"
              << box->geo.world_y << "|" << box->geo.width << "|" << box->geo.height << "|"
              << box->geo.content_width << "|" << box->geo.content_height << "\n";
    for (const zui::layout::Box* child : box->children) {
        if (child != nullptr) {
            dump_brief(child);
        }
    }
}

void list_scrollers(const zui::layout::Box* box) {
    if (box == nullptr) {
        return;
    }
    if (is_scrollable(*box)) {
        const std::string tag = box->node != nullptr ? box->node->tag : std::string("anon");
        const std::string id = box->node != nullptr ? box->node->id : std::string();
        const std::string cls = box->node != nullptr ? box->node->class_name : std::string();
        const float sx = box->node != nullptr ? box->node->scroll_x : 0;
        const float sy = box->node != nullptr ? box->node->scroll_y : 0;
        const float view_w = padding_box_w(*box);
        const float view_h = padding_box_h(*box);
        const float max_x = max_scroll_x(*box);
        const float max_y = max_scroll_y(*box);
        std::cout << "scroller|id=" << id << "|tag=" << tag << "|class=" << cls
                  << "|overflow_x=" << overflow_name(box->style().overflow_x)
                  << "|overflow_y=" << overflow_name(box->style().overflow_y) << "|scroll_x=" << sx
                  << "|scroll_y=" << sy << "|max_x=" << max_x << "|max_y=" << max_y
                  << "|border=" << box->geo.width << "x" << box->geo.height
                  << "|padding_box=" << view_w << "x" << view_h
                  << "|content=" << box->geo.content_width << "x" << box->geo.content_height
                  << "\n";
    }
    for (const zui::layout::Box* child : box->children) {
        list_scrollers(child);
    }
}

void dump_dom_node(const zui::html::Node* n, int depth) {
    if (n == nullptr) {
        return;
    }
    if (zui::html::is_element(n)) {
        for (int i = 0; i < depth; ++i) {
            std::cout << "  ";
        }
        std::cout << "dom|" << n->tag << "|" << n->id << "|" << n->class_name << "\n";
        for (const auto& ch : n->children) {
            dump_dom_node(ch.get(), depth + 1);
        }
    } else {
        for (const auto& ch : n->children) {
            dump_dom_node(ch.get(), depth);
        }
    }
}

zui::layout::Box* find_box_for_node(zui::layout::Box* box, zui::html::Node* node) {
    if (box == nullptr || node == nullptr) {
        return nullptr;
    }
    if (box->node == node) {
        return box;
    }
    for (zui::layout::Box* child : box->children) {
        if (zui::layout::Box* found = find_box_for_node(child, node)) {
            return found;
        }
    }
    return nullptr;
}

zui::layout::Box* find_first_scroller(zui::layout::Box* box) {
    if (box == nullptr) {
        return nullptr;
    }
    if (is_scrollable(*box) && box->node != nullptr) {
        return box;
    }
    for (zui::layout::Box* child : box->children) {
        if (zui::layout::Box* found = find_first_scroller(child)) {
            return found;
        }
    }
    return nullptr;
}

zui::layout::Box* resolve_scroller(zui::layout::LayoutEngine& layout, zui::html::Document& doc,
                                   const Options& opt) {
    if (opt.have_scroll_id && !opt.scroll_id.empty()) {
        zui::html::Node* node = zui::html::find(doc, opt.scroll_id);
        if (node == nullptr) {
            std::cerr << "scroll-id not found: " << opt.scroll_id << "\n";
            return nullptr;
        }
        zui::layout::Box* box = layout.principal_box(node);
        if (box == nullptr) {
            box = find_box_for_node(layout.root(), node);
        }
        if (box == nullptr) {
            std::cerr << "no layout box for scroll-id: " << opt.scroll_id << "\n";
        }
        return box;
    }
    // Empty id: document/root scroll if the root can scroll, else first overflow scroller.
    if (layout.root() != nullptr && is_scrollable(*layout.root()) &&
        layout.root()->node != nullptr) {
        return layout.root();
    }
    return find_first_scroller(layout.root());
}

void apply_scroll(zui::layout::Box* box, float left, float top, bool as_fraction, bool set_x,
                  bool set_y) {
    if (box == nullptr || box->node == nullptr) {
        return;
    }
    const float max_x = max_scroll_x(*box);
    const float max_y = max_scroll_y(*box);
    if (set_x) {
        if (as_fraction) {
            const float x = std::clamp(left, 0.0f, 1.0f) * max_x;
            box->node->scroll_x = std::clamp(x, 0.0f, max_x);
        } else {
            // Absolute CSS px matches wheel (app.cpp): no clamp to max, so sync_world still
            // shifts children when layout left max_scroll at 0.
            box->node->scroll_x = (std::max)(0.0f, left);
        }
    }
    if (set_y) {
        if (as_fraction) {
            const float y = std::clamp(top, 0.0f, 1.0f) * max_y;
            box->node->scroll_y = std::clamp(y, 0.0f, max_y);
        } else {
            box->node->scroll_y = (std::max)(0.0f, top);
        }
    }
}

bool parse_args(int argc, char** argv, Options& opt) {
    opt.path = ZUI_SOURCE_DIR "/example/tests/hello/hello.html";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--brief") {
            opt.brief = true;
            continue;
        }
        if (arg == "--dump") {
            opt.brief = false;
            continue;
        }
        if (arg == "--list-scrollers") {
            opt.list_scrollers = true;
            continue;
        }
        if (arg == "--dom") {
            opt.dump_dom = true;
            continue;
        }
        if (arg == "--scroll-fraction") {
            opt.scroll_fraction = true;
            continue;
        }
        if (arg == "--wait-ms" && i + 1 < argc) {
            opt.wait_ms = std::atoi(argv[++i]);
            continue;
        }
        if (arg == "--size" && i + 1 < argc) {
            if (!parse_size(argv[++i], opt.viewport_w, opt.viewport_h)) {
                std::cerr << "bad --size, want WxH\n";
                return false;
            }
            continue;
        }
        if (arg == "--scroll-id" && i + 1 < argc) {
            opt.have_scroll_id = true;
            opt.scroll_id = argv[++i];
            continue;
        }
        if (arg == "--scroll-top" && i + 1 < argc) {
            opt.have_scroll_top = true;
            opt.scroll_top = std::strtof(argv[++i], nullptr);
            continue;
        }
        if (arg == "--scroll-left" && i + 1 < argc) {
            opt.have_scroll_left = true;
            opt.scroll_left = std::strtof(argv[++i], nullptr);
            continue;
        }
        if (arg == "--scroll-tops" && i + 1 < argc) {
            opt.scroll_tops = parse_csv_floats(argv[++i]);
            continue;
        }
        if (arg == "--out" && i + 1 < argc) {
            opt.out_path = argv[++i];
            continue;
        }
        if (arg == "--help" || arg == "-h") {
            std::cout
                << "zui_ui_probe <page.html> [options]\n"
                << "  --size WxH              default 1100x720\n"
                << "  --brief                 machine lines: box|tag|id|class|x|y|w|h|content_w|content_h\n"
                << "  --dump                  human tree (default without --brief)\n"
                << "  --list-scrollers        print scrollable boxes\n"
                << "  --dom                   list elements tag/id/class (no layout needed beyond load)\n"
                << "  --scroll-id <id>        target element id (empty = root/first scroller)\n"
                << "  --scroll-top <v>        set scroll_y (CSS px, or fraction with --scroll-fraction)\n"
                << "  --scroll-left <v>       set scroll_x similarly\n"
                << "  --scroll-fraction       interpret tops/lefts as fractions of max scroll\n"
                << "  --scroll-tops a,b,c     set scroll per value; emit scroll|... then brief frames\n"
                << "  --out <path>            write stdout capture to file\n"
                << "  --wait-ms <n>           delay before first layout\n";
            return false;
        }
        if (!arg.empty() && arg[0] == '-') {
            std::cerr << "unknown option: " << arg << "\n";
            return false;
        }
        opt.path = arg;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!parse_args(argc, argv, opt)) {
        return 1;
    }

    std::ofstream out_file;
    std::streambuf* old_buf = nullptr;
    if (!opt.out_path.empty()) {
        ensure_parent_dirs(opt.out_path);
        out_file.open(opt.out_path, std::ios::out | std::ios::trunc);
        if (!out_file) {
            std::cerr << "failed to open --out path: " << opt.out_path << "\n";
            return 1;
        }
        old_buf = std::cout.rdbuf(out_file.rdbuf());
    }

    zui::html::Document doc;
    std::string error;
    if (!zui::html::load_file(opt.path, doc, error)) {
        std::cerr << error << "\n";
        if (old_buf != nullptr) {
            std::cout.rdbuf(old_buf);
        }
        return 1;
    }

    if (opt.dump_dom) {
        dump_dom_node(doc.root.get(), 0);
        if (opt.list_scrollers || opt.brief || !opt.scroll_tops.empty() || opt.have_scroll_top ||
            opt.have_scroll_left) {
            // continue into layout for the other modes
        } else {
            if (old_buf != nullptr) {
                std::cout.rdbuf(old_buf);
            }
            return 0;
        }
    }

    zui::css::Engine css;
    css.load_sheets(doc, error);
    // The viewport is part of the cascade: a media query evaluated against a zero viewport makes
    // `@media (min-width: 900px)`-style rules drop out and every measurement below wrong.
    css.cascade(doc, opt.viewport_w, opt.viewport_h);

    if (opt.wait_ms > 0) {
#if defined(_WIN32)
        Sleep(static_cast<DWORD>(opt.wait_ms));
#else
        for (int spun = 0; spun < opt.wait_ms; ++spun) {
        }
#endif
    }

    zui::layout::FreeTypeTextMetrics metrics;
    zui::layout::LayoutEngine layout(metrics);
    layout.layout(doc, zui::layout::Viewport{opt.viewport_w, opt.viewport_h});
    // A replaced element measures 300x150 on its first observation and only bumps its content
    // version then (LAYOUT.md section 4), so one pass reports the placeholder - and a
    // `width: max-content` box around the image keeps that width. A browser is settled before it
    // stops, and the difftest driver drains three frames for the same reason: do the same here or
    // every image measurement is the placeholder.
    for (int i = 0; i < 3; ++i) {
        layout.layout(doc, zui::layout::Viewport{opt.viewport_w, opt.viewport_h});
    }
    // This probe reports *layout* geometry for every box, which is what the box compare joins
    // against the browser's element rects. A frame's paint walk culls the subtrees that cannot
    // reach the viewport (LAYOUT.md section 9 step B) and clears their rectangles, so the probe
    // maps every box instead - the same call the trace export makes.
    zui::layout::map_paint_every_box(layout.paint_map_session(), *layout.root());

    if (opt.list_scrollers) {
        list_scrollers(layout.root());
        if (opt.scroll_tops.empty() && !opt.have_scroll_top && !opt.have_scroll_left &&
            !opt.brief) {
            if (old_buf != nullptr) {
                std::cout.rdbuf(old_buf);
            }
            return 0;
        }
    }

    const bool need_scroll = !opt.scroll_tops.empty() || opt.have_scroll_top ||
                             opt.have_scroll_left || opt.have_scroll_id;
    zui::layout::Box* scroller = nullptr;
    if (need_scroll || !opt.scroll_tops.empty()) {
        scroller = resolve_scroller(layout, doc, opt);
        if (scroller == nullptr &&
            (opt.have_scroll_top || opt.have_scroll_left || !opt.scroll_tops.empty())) {
            std::cerr << "no scroll target resolved\n";
            if (old_buf != nullptr) {
                std::cout.rdbuf(old_buf);
            }
            return 1;
        }
    }

    if (!opt.scroll_tops.empty()) {
        // Multi-frame: for each top, set scroll, sync_world, emit scroll header + brief boxes.
        const std::string sid =
            (scroller != nullptr && scroller->node != nullptr) ? scroller->node->id : opt.scroll_id;
        for (size_t i = 0; i < opt.scroll_tops.size(); ++i) {
            apply_scroll(scroller, opt.have_scroll_left ? opt.scroll_left : 0.0f,
                         opt.scroll_tops[i], opt.scroll_fraction, opt.have_scroll_left, true);
            zui::layout::map_paint_every_box(layout.paint_map_session(), *layout.root());
            const float y = scroller->node != nullptr ? scroller->node->scroll_y : 0;
            const float max_y = max_scroll_y(*scroller);
            std::cout << "scroll|" << sid << "|" << y << "|" << max_y << "|" << i << "\n";
            dump_brief(layout.root());
        }
        if (old_buf != nullptr) {
            std::cout.rdbuf(old_buf);
        }
        return 0;
    }

    if (scroller != nullptr && (opt.have_scroll_top || opt.have_scroll_left)) {
        apply_scroll(scroller, opt.scroll_left, opt.scroll_top, opt.scroll_fraction,
                     opt.have_scroll_left, opt.have_scroll_top);
        zui::layout::map_paint_every_box(layout.paint_map_session(), *layout.root());
    }

    if (opt.brief) {
        dump_brief(layout.root());
    } else if (!opt.list_scrollers || opt.have_scroll_top || opt.have_scroll_left) {
        std::cout << opt.path << " at " << opt.viewport_w << "x" << opt.viewport_h << "\n";
        dump(layout.root(), 0);
    }

    if (old_buf != nullptr) {
        std::cout.rdbuf(old_buf);
    }
    return 0;
}
