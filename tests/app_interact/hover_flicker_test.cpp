// Hello's window flashes on open and again while the pointer sits on a control.
// RecordingRenderer does not clear, so a damage-sized list can keep the same length as a
// full page and still present white once a real target is cleared. This drives
// App::pump_frame on example/tests/hello and checks the submitted list, then the same
// list through probe_read_frame.

#include "handler_rig.h"

#include "backends/frame_probe.h"
#include "backends/renderer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <tuple>
#include <vector>

#include "../../example/tests/hello/actions.h"

namespace {

constexpr std::uint8_t kCanvasR = 0x10;
constexpr std::uint8_t kCanvasG = 0x14;
constexpr std::uint8_t kCanvasB = 0x1c;
constexpr std::uint8_t kPrimary = 0x2f;
constexpr std::uint8_t kPrimaryG = 0x6f;
constexpr std::uint8_t kPrimaryB = 0xeb;
constexpr std::uint8_t kPrimaryHoverR = 0x3d;
constexpr std::uint8_t kPrimaryHoverG = 0x7d;
constexpr std::uint8_t kPrimaryHoverB = 0xff;

struct Box {
    float x = 0;
    float y = 0;
    float w = 0;
    float h = 0;
};

bool overlaps(const zui::paint::Rect& a, const Box& b) {
    return a.w > 0.f && a.h > 0.f && a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h &&
           b.y < a.y + a.h;
}

bool color_is(const zui::paint::Color& c, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return c.r == r && c.g == g && c.b == b && c.a == 255;
}

bool canvas_fill(const zui::paint::DisplayItem& item, float vw, float vh) {
    return item.op == zui::paint::DisplayOp::FillRect &&
           color_is(item.color, kCanvasR, kCanvasG, kCanvasB) && item.rect.w >= vw * 0.9f &&
           item.rect.h >= vh * 0.9f;
}

struct Key {
    int op = 0;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    std::uint32_t rgba = 0;
    std::string text;

    bool operator==(const Key& o) const {
        return op == o.op && x == o.x && y == o.y && w == o.w && h == o.h && rgba == o.rgba &&
               text == o.text;
    }
};

int q(float v) { return static_cast<int>(std::lround(v * 64.f)); }

Key key_of(const zui::paint::DisplayItem& item) {
    Key key;
    key.op = static_cast<int>(item.op);
    key.x = q(item.rect.x);
    key.y = q(item.rect.y);
    key.w = q(item.rect.w);
    key.h = q(item.rect.h);
    key.rgba = (std::uint32_t{item.color.r} << 24) | (std::uint32_t{item.color.g} << 16) |
               (std::uint32_t{item.color.b} << 8) | item.color.a;
    key.text = item.text;
    return key;
}

std::vector<Key> keys_of(const std::vector<zui::paint::DisplayItem>& items) {
    std::vector<Key> out;
    out.reserve(items.size());
    for (const zui::paint::DisplayItem& item : items) {
        out.push_back(key_of(item));
    }
    return out;
}

std::vector<Key> keys_outside(const std::vector<zui::paint::DisplayItem>& items, const Box& box) {
    std::vector<Key> out;
    for (const zui::paint::DisplayItem& item : items) {
        if (!overlaps(item.rect, box)) {
            out.push_back(key_of(item));
        }
    }
    return out;
}

bool fill_over(const std::vector<zui::paint::DisplayItem>& items, const Box& box, std::uint8_t r,
               std::uint8_t g, std::uint8_t b) {
    for (const zui::paint::DisplayItem& item : items) {
        if (item.op == zui::paint::DisplayOp::FillRect && color_is(item.color, r, g, b) &&
            overlaps(item.rect, box)) {
            return true;
        }
    }
    return false;
}

void log_frame(const char* name, const std::vector<zui::paint::DisplayItem>& items, float vw,
               float vh, bool hovered) {
    int fills = 0;
    bool canvas = false;
    float max_w = 0.f;
    for (const zui::paint::DisplayItem& item : items) {
        if (item.op == zui::paint::DisplayOp::FillRect) {
            ++fills;
            max_w = std::max(max_w, item.rect.w);
            if (canvas_fill(item, vw, vh)) {
                canvas = true;
            }
        }
    }
    std::cout << name << " items=" << items.size() << " fills=" << fills << " maxw=" << max_w
              << " canvas=" << (canvas ? 1 : 0) << " hovered=" << (hovered ? 1 : 0) << "\n";
}

bool hovered_id(zui::App& app, const char* id) {
    return zui::frame::Access::hovered(app, id);
}

struct Sample {
    int x = 0;
    int y = 0;
    bool found = false;
};

Sample canvas_sample(const std::vector<std::uint8_t>& rgba, int w, int h, const Box& avoid) {
    Sample sample;
    if (rgba.size() < static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4) {
        return sample;
    }
    for (int y = 80; y < h - 4; y += 4) {
        for (int x = 220; x < w - 4; x += 4) {
            if (x >= avoid.x - 8.f && x < avoid.x + avoid.w + 8.f && y >= avoid.y - 8.f &&
                y < avoid.y + avoid.h + 8.f) {
                continue;
            }
            const std::size_t i =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) *
                4;
            if (rgba[i] == kCanvasR && rgba[i + 1] == kCanvasG && rgba[i + 2] == kCanvasB) {
                sample.x = x;
                sample.y = y;
                sample.found = true;
                return sample;
            }
        }
    }
    return sample;
}

bool pixel_is(const std::vector<std::uint8_t>& rgba, int w, int x, int y, std::uint8_t r,
              std::uint8_t g, std::uint8_t b) {
    const std::size_t i =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 4;
    if (i + 3 >= rgba.size()) {
        return false;
    }
    return rgba[i] == r && rgba[i + 1] == g && rgba[i + 2] == b;
}

void dump_px(const char* name, const std::vector<std::uint8_t>& rgba, int w, int x, int y) {
    const std::size_t i =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 4;
    std::cout << "px " << name << " " << x << " " << y << " " << static_cast<int>(rgba[i]) << " "
              << static_cast<int>(rgba[i + 1]) << " " << static_cast<int>(rgba[i + 2]) << " "
              << static_cast<int>(rgba[i + 3]) << "\n";
}

zui::backends::ProbeFrame raster(const std::vector<zui::paint::DisplayItem>& items, int w, int h,
                                 std::vector<std::uint8_t>& rgba) {
    zui::paint::DisplayList list;
    list.replace_items(items);
    return zui::backends::probe_read_frame(list, w, h, {}, rgba);
}

}  // namespace

TEST(AppInteract, ExampleHelloHoverDoesNotFlicker) {
#ifdef _WIN32
    _putenv_s("ZUI_PERF_DAMAGE", "1");
#endif
    zui::test::HandlerRig rig;
    const std::filesystem::path html = std::filesystem::path(ZUI_SOURCE_DIR) / "example" / "tests" /
                                        "hello" / "hello.html";
    ASSERT_TRUE(rig.app.document().load(html.string()));
    hello_example::bind_actions(rig.app);
    ASSERT_TRUE(rig.windowed(1100, 720));
    ASSERT_TRUE(rig.pump());
    log_frame("first", rig.renderer().last_items, 1100.f, 720.f, hovered_id(rig.app, "save"));

    const float vw = 1100.f;
    const float vh = 720.f;
    ASSERT_TRUE(rig.pump());
    const std::vector<zui::paint::DisplayItem> idle_a = rig.renderer().last_items;
    ASSERT_TRUE(rig.pump());
    const std::vector<zui::paint::DisplayItem> idle_b = rig.renderer().last_items;
    log_frame("idle_a", idle_a, vw, vh, hovered_id(rig.app, "save"));
    log_frame("idle_b", idle_b, vw, vh, hovered_id(rig.app, "save"));
    ASSERT_FALSE(idle_a.empty());
    ASSERT_FALSE(idle_b.empty());
    EXPECT_EQ(keys_of(idle_a), keys_of(idle_b));
    EXPECT_TRUE(std::any_of(idle_b.begin(), idle_b.end(),
                            [&](const zui::paint::DisplayItem& item) {
                                return canvas_fill(item, vw, vh);
                            }));

    float sx = 0, sy = 0, sw = 0, sh = 0;
    ASSERT_TRUE(zui::frame::Access::box_rect(rig.app, "save", sx, sy, sw, sh));
    const Box save_before{sx, sy, sw, sh};
    // Bottom edge of the layout border. translateY(-1px) moves the painted box off this point.
    const float px = sx + sw * 0.5f;
    const float py = sy + sh - 0.5f;
    ASSERT_TRUE(rig.point(px, py));
    EXPECT_TRUE(hovered_id(rig.app, "save"));

    std::vector<std::vector<zui::paint::DisplayItem>> hover_frames;
    hover_frames.push_back(rig.renderer().last_items);
    const double steps[] = {0.05, 0.05, 0.05, 0.10};
    for (double step : steps) {
        zui::frame::Access::advance_time(rig.app, step);
        ASSERT_TRUE(rig.pump());
        EXPECT_TRUE(hovered_id(rig.app, "save")) << "hover dropped at the layout bottom edge";
        hover_frames.push_back(rig.renderer().last_items);
    }
    float ax = 0, ay = 0, aw = 0, ah = 0;
    ASSERT_TRUE(zui::frame::Access::box_rect(rig.app, "save", ax, ay, aw, ah));
    const Box save_after{ax, ay, aw, ah};
    std::cout << "save_before y=" << sy << " h=" << sh << " save_after y=" << ay << " h=" << ah
              << "\n";
    EXPECT_LT(ay + ah, py) << "hover transform did not move the painted box off the pointer";

    Box avoid = save_before;
    avoid.y = std::min(save_before.y, save_after.y);
    avoid.h = std::max(save_before.y + save_before.h, save_after.y + save_after.h) - avoid.y;

    const std::size_t full_n = idle_b.size();
    for (std::size_t i = 0; i < hover_frames.size(); ++i) {
        log_frame("hover", hover_frames[i], vw, vh, true);
        EXPECT_GE(hover_frames[i].size(), full_n / 2)
            << "frame " << i << " collapsed toward a damage sliver";
        EXPECT_TRUE(std::any_of(hover_frames[i].begin(), hover_frames[i].end(),
                                [&](const zui::paint::DisplayItem& item) {
                                    return canvas_fill(item, vw, vh);
                                }))
            << "frame " << i << " dropped the canvas fill";
    }
    const std::vector<zui::paint::DisplayItem>& settled = hover_frames.back();
    EXPECT_TRUE(fill_over(settled, save_after, kPrimaryHoverR, kPrimaryHoverG, kPrimaryHoverB));
    ASSERT_TRUE(rig.pump());
    EXPECT_EQ(keys_of(rig.renderer().last_items), keys_of(settled));
    EXPECT_TRUE(hovered_id(rig.app, "save"));

    // Leave the control. Every intermediate list still covers the page.
    ASSERT_TRUE(rig.point(4.f, 4.f));
    std::vector<std::vector<zui::paint::DisplayItem>> leave_frames;
    leave_frames.push_back(rig.renderer().last_items);
    for (double step : steps) {
        zui::frame::Access::advance_time(rig.app, step);
        ASSERT_TRUE(rig.pump());
        leave_frames.push_back(rig.renderer().last_items);
    }
    for (std::size_t i = 0; i < leave_frames.size(); ++i) {
        log_frame("leave", leave_frames[i], vw, vh, hovered_id(rig.app, "save"));
        EXPECT_GE(leave_frames[i].size(), full_n / 2);
        EXPECT_TRUE(std::any_of(leave_frames[i].begin(), leave_frames[i].end(),
                                [&](const zui::paint::DisplayItem& item) {
                                    return canvas_fill(item, vw, vh);
                                }));
    }
    EXPECT_FALSE(hovered_id(rig.app, "save"));
    float lx = 0, ly = 0, lw = 0, lh = 0;
    ASSERT_TRUE(zui::frame::Access::box_rect(rig.app, "save", lx, ly, lw, lh));
    EXPECT_TRUE(fill_over(leave_frames.back(), Box{lx, ly, lw, lh}, kPrimary, kPrimaryG, kPrimaryB));
    ASSERT_TRUE(rig.pump());
    EXPECT_EQ(keys_of(rig.renderer().last_items), keys_of(leave_frames.back()));

    // Color-only hover (.nav). No transform, so outside items match once the colour settles.
    float nx = 0, ny = 0, nw = 0, nh = 0;
    ASSERT_TRUE(zui::frame::Access::box_rect(rig.app, "nav_projects", nx, ny, nw, nh));
    const Box nav{nx, ny, nw, nh};
    const std::vector<Key> outside_before = keys_outside(rig.renderer().last_items, nav);
    ASSERT_TRUE(rig.point(nx + nw * 0.5f, ny + nh * 0.5f));
    EXPECT_TRUE(hovered_id(rig.app, "nav_projects"));
    for (double step : steps) {
        zui::frame::Access::advance_time(rig.app, step);
        ASSERT_TRUE(rig.pump());
        EXPECT_TRUE(hovered_id(rig.app, "nav_projects"));
        log_frame("nav", rig.renderer().last_items, vw, vh, true);
        EXPECT_GE(rig.renderer().last_items.size(), full_n / 2);
        EXPECT_TRUE(std::any_of(rig.renderer().last_items.begin(), rig.renderer().last_items.end(),
                                [&](const zui::paint::DisplayItem& item) {
                                    return canvas_fill(item, vw, vh);
                                }));
    }
    EXPECT_EQ(keys_outside(rig.renderer().last_items, nav), outside_before);
    const std::vector<zui::paint::DisplayItem> nav_settled = rig.renderer().last_items;
    ASSERT_TRUE(rig.pump());
    EXPECT_EQ(keys_of(rig.renderer().last_items), keys_of(nav_settled));

    std::vector<std::uint8_t> rgba;
    const zui::backends::ProbeFrame probed = raster(idle_b, 1100, 720, rgba);
    if (probed != zui::backends::ProbeFrame::Ok) {
        std::cout << "READBACK " << static_cast<int>(probed) << "\n";
        return;
    }
    const Sample sample = canvas_sample(rgba, 1100, 720, avoid);
    ASSERT_TRUE(sample.found) << "idle page has no canvas pixel away from the button";
    std::cout << "sample " << sample.x << "," << sample.y << "\n";
    dump_px("idle_b", rgba, 1100, sample.x, sample.y);
    EXPECT_TRUE(pixel_is(rgba, 1100, sample.x, sample.y, kCanvasR, kCanvasG, kCanvasB));

    rgba.clear();
    ASSERT_EQ(raster(idle_a, 1100, 720, rgba), zui::backends::ProbeFrame::Ok);
    EXPECT_TRUE(pixel_is(rgba, 1100, sample.x, sample.y, kCanvasR, kCanvasG, kCanvasB));
    const std::vector<std::uint8_t> idle_px = rgba;

    for (std::size_t i = 0; i < hover_frames.size(); ++i) {
        rgba.clear();
        ASSERT_EQ(raster(hover_frames[i], 1100, 720, rgba), zui::backends::ProbeFrame::Ok);
        EXPECT_TRUE(pixel_is(rgba, 1100, sample.x, sample.y, kCanvasR, kCanvasG, kCanvasB))
            << "hover frame " << i << " canvas sample is not #10141c";
        dump_px("hover", rgba, 1100, sample.x, sample.y);
    }
    for (std::size_t i = 0; i < leave_frames.size(); ++i) {
        rgba.clear();
        ASSERT_EQ(raster(leave_frames[i], 1100, 720, rgba), zui::backends::ProbeFrame::Ok);
        EXPECT_TRUE(pixel_is(rgba, 1100, sample.x, sample.y, kCanvasR, kCanvasG, kCanvasB))
            << "leave frame " << i << " canvas sample is not #10141c";
        dump_px("leave", rgba, 1100, sample.x, sample.y);
    }
    rgba.clear();
    ASSERT_EQ(raster(settled, 1100, 720, rgba), zui::backends::ProbeFrame::Ok);
    int mismatches = 0;
    for (int y = 8; y < 720; y += 16) {
        for (int x = 8; x < 1100; x += 16) {
            if (x >= avoid.x - 8.f && x < avoid.x + avoid.w + 8.f && y >= avoid.y - 8.f &&
                y < avoid.y + avoid.h + 8.f) {
                continue;
            }
            const std::size_t i = (static_cast<std::size_t>(y) * 1100u + static_cast<std::size_t>(x)) * 4;
            if (rgba[i] != idle_px[i] || rgba[i + 1] != idle_px[i + 1] ||
                rgba[i + 2] != idle_px[i + 2]) {
                if (mismatches < 12) {
                    std::cout << "mismatch " << x << "," << y << " idle="
                              << static_cast<int>(idle_px[i]) << ","
                              << static_cast<int>(idle_px[i + 1]) << ","
                              << static_cast<int>(idle_px[i + 2]) << " hover="
                              << static_cast<int>(rgba[i]) << "," << static_cast<int>(rgba[i + 1])
                              << "," << static_cast<int>(rgba[i + 2]) << "\n";
                }
                ++mismatches;
            }
        }
    }
    std::cout << "grid_mismatches " << mismatches << "\n";
    EXPECT_EQ(mismatches, 0);
    rgba.clear();
    ASSERT_EQ(raster(settled, 1100, 720, rgba), zui::backends::ProbeFrame::Ok);
    int hover_px = -1;
    int hover_py = -1;
    const int x0 = std::max(0, static_cast<int>(std::floor(save_after.x)));
    const int y0 = std::max(0, static_cast<int>(std::floor(save_after.y)));
    const int x1 = std::min(1099, static_cast<int>(std::ceil(save_after.x + save_after.w)));
    const int y1 = std::min(719, static_cast<int>(std::ceil(save_after.y + save_after.h)));
    for (int y = y0; y <= y1 && hover_px < 0; ++y) {
        for (int x = x0; x <= x1; ++x) {
            if (pixel_is(rgba, 1100, x, y, kPrimaryHoverR, kPrimaryHoverG, kPrimaryHoverB)) {
                hover_px = x;
                hover_py = y;
                break;
            }
        }
    }
    const int bx = static_cast<int>(save_after.x + 4.f);
    const int by = static_cast<int>(save_after.y + save_after.h * 0.5f);
    if (!pixel_is(rgba, 1100, bx, by, kPrimaryHoverR, kPrimaryHoverG, kPrimaryHoverB)) {
        const std::size_t i =
            (static_cast<std::size_t>(by) * 1100u + static_cast<std::size_t>(bx)) * 4;
        std::cout << "button_px " << bx << "," << by << " rgba="
                  << static_cast<int>(rgba[i]) << "," << static_cast<int>(rgba[i + 1]) << ","
                  << static_cast<int>(rgba[i + 2]) << " hover_hit=" << hover_px << "," << hover_py
                  << "\n";
        for (const zui::paint::DisplayItem& item : settled) {
            if (item.op == zui::paint::DisplayOp::FillRect &&
                color_is(item.color, kPrimaryHoverR, kPrimaryHoverG, kPrimaryHoverB)) {
                std::cout << "hover_fill " << item.rect.x << "," << item.rect.y << " "
                          << item.rect.w << "x" << item.rect.h << "\n";
            }
        }
    }
    dump_px("primary", rgba, 1100, bx, by);
    EXPECT_TRUE(pixel_is(rgba, 1100, bx, by, kPrimaryHoverR, kPrimaryHoverG, kPrimaryHoverB))
        << "settled primary pixel is not #3d7dff";
}

// Forwards to the process default renderer and keeps the list `pump_frame` submitted.
// `present` / `has_completed_picture` stay the inner renderer's, so a reveal still waits
// on a real queue-present.
class SubmittedListRenderer final : public zui::backends::IRenderer {
public:
    explicit SubmittedListRenderer(zui::backends::IRenderer& inner) : inner_(inner) {}
    bool create(zui::backends::IWindow& window) override { return inner_.create(window); }
    void destroy() override { inner_.destroy(); }
    void resize(int width, int height) override { inner_.resize(width, height); }
    void begin_frame() override { inner_.begin_frame(); }
    void submit(const zui::paint::DisplayList& list) override {
        last_items = list.items();
        inner_.submit(list);
    }
    bool present() override { return inner_.present(); }
    int present_ok_count() const override { return inner_.present_ok_count(); }
    bool has_completed_picture() const override { return inner_.has_completed_picture(); }
    bool read_frame(std::vector<std::uint8_t>& rgba, int& width, int& height) override {
        return inner_.read_frame(rgba, width, height);
    }
    const char* name() const override { return inner_.name(); }
    std::vector<zui::paint::DisplayItem> last_items;

private:
    zui::backends::IRenderer& inner_;
};

int count_color(const std::vector<std::uint8_t>& rgba, std::uint8_t r, std::uint8_t g,
                std::uint8_t b) {
    int n = 0;
    for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
        if (rgba[i] == r && rgba[i + 1] == g && rgba[i + 2] == b) {
            ++n;
        }
    }
    return n;
}

// Default example path: `create_renderer("")` is Vulkan. Each pump's readback is the
// swapchain image present queued, not the offscreen target captured before that copy.
TEST(AppInteract, ExampleHelloDefaultRendererHoverIsNotAWhiteClear) {
    auto window = zui::backends::create_window("sdl3");
    ASSERT_NE(window, nullptr);
    zui::backends::WindowDesc desc;
    desc.title = "zui-hover-present";
    desc.width = 1100;
    desc.height = 720;
    desc.api = zui::backends::GfxApi::Vulkan;
    desc.show_after_present = true;
    ASSERT_TRUE(window->create(desc));
    auto inner = zui::backends::create_renderer("");
    ASSERT_NE(inner, nullptr);
    ASSERT_STREQ(inner->name(), "vk");
    if (!inner->create(*window)) {
        std::cout << "READBACK unavailable default renderer\n";
        GTEST_SKIP() << "default renderer did not create";
    }
    SubmittedListRenderer tap(*inner);
    struct ShowGate {
        const SubmittedListRenderer* tap = nullptr;
        int at_pump = 0;
        int bad = 0;
    } gate;
    gate.tap = &tap;
    window->set_before_show_for_test(
        [](void* user) {
            auto* gate = static_cast<ShowGate*>(user);
            if (gate->tap->present_ok_count() <= gate->at_pump) {
                ++gate->bad;
            }
        },
        &gate);
    zui::App app{0, nullptr};
    const std::filesystem::path html = std::filesystem::path(ZUI_SOURCE_DIR) / "example" / "tests" /
                                        "hello" / "hello.html";
    ASSERT_TRUE(app.document().load(html.string()));
    hello_example::bind_actions(app);
    zui::frame::Session session;
    session.window = window.get();
    session.renderer = &tap;
    ASSERT_TRUE(zui::frame::Access::attach(app, session));
    EXPECT_FALSE(window->is_shown());

    auto pump = [&]() {
        const int before = tap.present_ok_count();
        gate.at_pump = before;
        EXPECT_TRUE(app.pump_frame());
        EXPECT_EQ(gate.bad, 0) << "show() ran before present() queued an image";
        if (window->is_shown()) {
            EXPECT_GT(tap.present_ok_count(), before)
                << "window mapped before a present queued an image";
        }
        std::vector<std::uint8_t> rgba;
        int rw = 0;
        int rh = 0;
        const bool read = tap.present_ok_count() > before && tap.read_frame(rgba, rw, rh);
        return std::tuple<bool, std::vector<std::uint8_t>, int, int>{
            read, std::move(rgba), rw, rh};
    };

    bool revealed = false;
    for (int i = 0; i < 8 && !window->is_shown(); ++i) {
        (void)pump();
        revealed = window->is_shown();
    }
    ASSERT_TRUE(revealed) << "default renderer never presented a picture";
    std::cout << "present_ok " << tap.present_ok_count() << " shown " << window->is_shown()
              << " show_before_present " << gate.bad << "\n";
    EXPECT_EQ(gate.bad, 0);

    auto [idle_read_a, idle_px_a, idle_w, idle_h] = pump();
    (void)idle_px_a;
    const std::vector<zui::paint::DisplayItem> idle_items_a = tap.last_items;
    auto [idle_read_b, idle_px_b, idle_w_b, idle_h_b] = pump();
    const std::vector<zui::paint::DisplayItem> idle_items_b = tap.last_items;
    ASSERT_TRUE(idle_read_a);
    ASSERT_TRUE(idle_read_b);
    EXPECT_EQ(idle_w, idle_w_b);
    EXPECT_EQ(idle_h, idle_h_b);
    ASSERT_FALSE(idle_items_a.empty());
    EXPECT_EQ(keys_of(idle_items_a), keys_of(idle_items_b));
    EXPECT_TRUE(std::any_of(idle_items_b.begin(), idle_items_b.end(), [&](const auto& item) {
        return canvas_fill(item, 1100.f, 720.f);
    }));

    float sx = 0, sy = 0, sw = 0, sh = 0;
    ASSERT_TRUE(zui::frame::Access::box_rect(app, "save", sx, sy, sw, sh));
    const float scale = window->content_scale() > 0.f ? window->content_scale() : 1.f;
    const Box save_before{sx, sy, sw, sh};
    Sample sample;
    for (int y = 0; y < idle_h && !sample.found; y += 4) {
        for (int x = 0; x < idle_w; x += 4) {
            const float cx = static_cast<float>(x) / scale;
            const float cy = static_cast<float>(y) / scale;
            if (cx >= save_before.x - 8.f && cx < save_before.x + save_before.w + 8.f &&
                cy >= save_before.y - 8.f && cy < save_before.y + save_before.h + 8.f) {
                continue;
            }
            if (pixel_is(idle_px_b, idle_w, x, y, kCanvasR, kCanvasG, kCanvasB)) {
                sample.x = x;
                sample.y = y;
                sample.found = true;
                break;
            }
        }
    }
    ASSERT_TRUE(sample.found) << "presented idle frame has no canvas pixel";
    dump_px("vk_idle", idle_px_b, idle_w, sample.x, sample.y);
    const int frame_w = idle_w;
    const int frame_h = idle_h;
    EXPECT_LT(count_color(idle_px_b, 255, 255, 255), (frame_w * frame_h) / 2);

    auto expect_presented = [&](const char* name) {
        auto [ok, px, rw, rh] = pump();
        EXPECT_TRUE(ok) << name << " did not present";
        EXPECT_FALSE(tap.last_items.empty());
        EXPECT_GE(tap.last_items.size(), idle_items_b.size() / 2) << name << " collapsed";
        EXPECT_TRUE(std::any_of(tap.last_items.begin(), tap.last_items.end(), [&](const auto& item) {
            return canvas_fill(item, 1100.f, 720.f);
        })) << name;
        if (ok && rw == frame_w && rh == frame_h) {
            dump_px(name, px, rw, sample.x, sample.y);
            EXPECT_TRUE(pixel_is(px, rw, sample.x, sample.y, kCanvasR, kCanvasG, kCanvasB))
                << name << " canvas sample is not #10141c";
            EXPECT_LT(count_color(px, 255, 255, 255), (rw * rh) / 2) << name << " is a white clear";
        }
        return px;
    };

    const float px = sx + sw * 0.5f;
    const float py = sy + sh - 0.5f;
    window->set_pointer_for_test(px, py, true);
    (void)expect_presented("vk_hover");
    EXPECT_TRUE(zui::frame::Access::hovered(app, "save"));
    const double steps[] = {0.05, 0.05, 0.05, 0.10};
    for (double step : steps) {
        zui::frame::Access::advance_time(app, step);
        (void)expect_presented("vk_hover");
        EXPECT_TRUE(zui::frame::Access::hovered(app, "save"))
            << "hover dropped at the layout bottom edge";
    }
    float ax = 0, ay = 0, aw = 0, ah = 0;
    ASSERT_TRUE(zui::frame::Access::box_rect(app, "save", ax, ay, aw, ah));
    EXPECT_LT(ay + ah, py) << "hover transform did not move the painted box off the pointer";
    const std::vector<zui::paint::DisplayItem> settled_items = tap.last_items;
    EXPECT_TRUE(fill_over(settled_items, Box{ax, ay, aw, ah}, kPrimaryHoverR, kPrimaryHoverG,
                          kPrimaryHoverB));
    auto settled_px = expect_presented("vk_held");
    EXPECT_EQ(keys_of(tap.last_items), keys_of(settled_items));
    const int hover_n = count_color(settled_px, kPrimaryHoverR, kPrimaryHoverG, kPrimaryHoverB);
    const int white_n = count_color(settled_px, 255, 255, 255);
    const int canvas_n = count_color(settled_px, kCanvasR, kCanvasG, kCanvasB);
    std::cout << "vk_frame " << frame_w << " " << frame_h << " canvas " << canvas_n << " white "
              << white_n << " hover " << hover_n << "\n";
    EXPECT_GT(canvas_n, 100);
    EXPECT_GT(hover_n, 10);
    EXPECT_LT(white_n, (frame_w * frame_h) / 2);

    window->set_pointer_for_test(4.f, 4.f, true);
    (void)expect_presented("vk_leave");
    for (double step : steps) {
        zui::frame::Access::advance_time(app, step);
        (void)expect_presented("vk_leave");
    }
    EXPECT_FALSE(zui::frame::Access::hovered(app, "save"));
    float lx = 0, ly = 0, lw = 0, lh = 0;
    ASSERT_TRUE(zui::frame::Access::box_rect(app, "save", lx, ly, lw, lh));
    EXPECT_TRUE(fill_over(tap.last_items, Box{lx, ly, lw, lh}, kPrimary, kPrimaryG, kPrimaryB));
    const std::vector<zui::paint::DisplayItem> left_items = tap.last_items;
    (void)expect_presented("vk_left_idle");
    EXPECT_EQ(keys_of(tap.last_items), keys_of(left_items));

    float nx = 0, ny = 0, nw = 0, nh = 0;
    ASSERT_TRUE(zui::frame::Access::box_rect(app, "nav_projects", nx, ny, nw, nh));
    const Box nav{nx, ny, nw, nh};
    const std::vector<Key> outside_before = keys_outside(tap.last_items, nav);
    window->set_pointer_for_test(nx + nw * 0.5f, ny + nh * 0.5f, true);
    (void)expect_presented("vk_nav");
    EXPECT_TRUE(zui::frame::Access::hovered(app, "nav_projects"));
    for (double step : steps) {
        zui::frame::Access::advance_time(app, step);
        (void)expect_presented("vk_nav");
        EXPECT_TRUE(zui::frame::Access::hovered(app, "nav_projects"));
    }
    EXPECT_EQ(keys_outside(tap.last_items, nav), outside_before);
    zui::frame::Access::detach(app);
}
