// Particle workbench handlers, driven through the controls on the shipped page.

#include "browser_oracle.h"
#include "handler_rig.h"

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "../../example/applications/particle/cpp/session.h"

namespace fs = std::filesystem;

namespace {

fs::path particle_html() {
    return fs::path(ZUI_SOURCE_DIR) / "example" / "applications" / "particle" / "index.html";
}

std::string read_bytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void write_bytes(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

struct ParticleRig {
    zui::test::HandlerRig rig;
    particle::Session session;
    fs::path dir;

    bool up() {
        session.app = &rig.app;
        dir = zui::test::scratch_dir("particle");
        session.dir = dir;
        std::error_code ec;
        fs::create_directories(dir, ec);
        if (!rig.app.document().load(particle_html().string())) {
            return false;
        }
        if (!rig.app.window().set_window_size(1100, 720)) {
            return false;
        }
        particle::bind_actions(session);
        particle::show(session);
        if (!rig.windowed(1100, 720) || !rig.pump() || !rig.pump()) {
            return false;
        }
        return true;
    }
};

bool box(const ParticleRig& rig, std::string_view id, float& x, float& y, float& w, float& h) {
    return zui::frame::Access::box_rect(rig.rig.app, id, x, y, w, h);
}

bool dot_pos(const ParticleRig& rig, float& x, float& y) {
    float w = 0, h = 0;
    return box(rig, "dot-0", x, y, w, h) && w > 0.f && h > 0.f;
}

int count_dots(const ParticleRig& rig) {
    int n = 0;
    for (const zui::frame::NodeCensus& row : zui::test::live_census(rig.rig.app)) {
        if (zui::test::class_token(row.class_attr, "dot") && row.w > 0.f && row.h > 0.f) {
            ++n;
        }
    }
    return n;
}

bool log_has(const ParticleRig& rig, std::string_view text) {
    return zui::test::any_text(rig.rig.app, "", "log-line", text);
}

const char kShader[] =
    "#version 300 es\n"
    "precision highp float;\n"
    "#define U_COUNT 12.0\n"
    "#define U_SPEED 1.0\n"
    "#define U_SPREAD 0.6\n"
    "uniform float u_count;\n"
    "uniform float u_speed;\n"
    "uniform float u_spread;\n"
    "// keep-me-zui-particle\n"
    "uniform float u_custom;\n"
    "void main() {\n"
    "  gl_FragColor = vec4(u_custom, 0.0, 0.0, 1.0);\n"
    "}\n";

}  // namespace

TEST(AppInteract, ParticleRegionsSitInTheWorkbench) {
    ParticleRig rig;
    ASSERT_TRUE(rig.up());

    float ex = 0, ey = 0, ew = 0, eh = 0;
    float sx = 0, sy = 0, sw = 0, sh = 0;
    float cx = 0, cy = 0, cw = 0, ch = 0;
    float px = 0, py = 0, pw = 0, ph = 0;
    ASSERT_TRUE(box(rig, "effects", ex, ey, ew, eh));
    ASSERT_TRUE(box(rig, "stage", sx, sy, sw, sh));
    ASSERT_TRUE(box(rig, "console", cx, cy, cw, ch));
    ASSERT_TRUE(box(rig, "params", px, py, pw, ph));
    EXPECT_GT(ew, 40.f);
    EXPECT_GT(sw, 40.f);
    EXPECT_GT(cw, 40.f);
    EXPECT_GT(pw, 40.f);
    EXPECT_GT(eh, 40.f);
    EXPECT_GT(sh, 40.f);
    EXPECT_GT(ch, 40.f);
    EXPECT_LE(ex + ew, sx + 2.f);
    EXPECT_GE(cy + 2.f, sy + sh - 2.f);
    EXPECT_LT(cx, sx + sw);
    EXPECT_LT(sx, cx + cw);
    EXPECT_GE(px + 2.f, sx + sw - 2.f);
}

TEST(AppInteract, ParticleSimulationAdvances) {
    ParticleRig rig;
    ASSERT_TRUE(rig.up());
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    ASSERT_TRUE(dot_pos(rig, x0, y0));
    ASSERT_TRUE(rig.rig.pump());
    ASSERT_TRUE(dot_pos(rig, x1, y1));
    ASSERT_TRUE(rig.rig.pump());
    ASSERT_TRUE(dot_pos(rig, x2, y2));

    EXPECT_GT(std::fabs(y1 - y0), 2.f) << y0 << " -> " << y1;
    EXPECT_GT(std::fabs(y2 - y1), 2.f) << y1 << " -> " << y2;
}

TEST(AppInteract, ParticleEffectSelectUpdatesTheField) {
    ParticleRig rig;
    ASSERT_TRUE(rig.up());
    float x0 = 0, y0 = 0;
    ASSERT_TRUE(dot_pos(rig, x0, y0));

    ASSERT_TRUE(rig.rig.click("effect-snow"));
    ASSERT_TRUE(rig.rig.pump());
    float x1 = 0, y1 = 0;
    ASSERT_TRUE(dot_pos(rig, x1, y1));
    EXPECT_GT(std::fabs(y1 - y0) + std::fabs(x1 - x0), 4.f);
    EXPECT_TRUE(log_has(rig, "飘雪"));

    ASSERT_TRUE(rig.rig.click("effect-orbit"));
    ASSERT_TRUE(rig.rig.pump());
    EXPECT_TRUE(log_has(rig, "环绕"));
    ASSERT_TRUE(rig.rig.click("effect-fountain"));
    ASSERT_TRUE(rig.rig.pump());
    EXPECT_TRUE(log_has(rig, "喷泉"));
    EXPECT_TRUE(rig.rig.has_class("effect-fountain", "is-on"));
}

TEST(AppInteract, ParticleCountChangesTheNextAdvance) {
    ParticleRig rig;
    ASSERT_TRUE(rig.up());
    const int before = count_dots(rig);
    ASSERT_GT(before, 0);

    ASSERT_TRUE(rig.rig.click("count-inc"));
    ASSERT_TRUE(rig.rig.pump());
    const int raised = count_dots(rig);
    EXPECT_EQ(raised, before + 1);
    EXPECT_TRUE(log_has(rig, "数量 " + std::to_string(before + 1)));

    ASSERT_TRUE(rig.rig.click("count-dec"));
    ASSERT_TRUE(rig.rig.pump());
    EXPECT_EQ(count_dots(rig), before);
    EXPECT_TRUE(log_has(rig, "数量 " + std::to_string(before)));
}

TEST(AppInteract, ParticleSpeedChangesTheNextAdvance) {
    ParticleRig rig;
    ASSERT_TRUE(rig.up());
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    ASSERT_TRUE(dot_pos(rig, x0, y0));
    ASSERT_TRUE(rig.rig.pump());
    ASSERT_TRUE(dot_pos(rig, x1, y1));
    const float slow = std::fabs(y1 - y0);
    ASSERT_GT(slow, 2.f);

    ASSERT_TRUE(rig.rig.click("speed-inc"));
    float xMid = 0, yMid = 0;
    ASSERT_TRUE(dot_pos(rig, xMid, yMid));
    ASSERT_TRUE(rig.rig.pump());
    float x2 = 0, y2 = 0;
    ASSERT_TRUE(dot_pos(rig, x2, y2));
    const float fast = std::fabs(y2 - yMid);
    EXPECT_GT(fast, slow * 1.4f) << "slow " << slow << " fast " << fast;
    EXPECT_TRUE(log_has(rig, "速度 2.0"));

    ASSERT_TRUE(rig.rig.click("speed-dec"));
    ASSERT_TRUE(rig.rig.pump());
    EXPECT_TRUE(log_has(rig, "速度 1.0"));
}

TEST(AppInteract, ParticleSpreadChangesTheNextAdvance) {
    ParticleRig rig;
    ASSERT_TRUE(rig.up());
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    ASSERT_TRUE(dot_pos(rig, x0, y0));
    ASSERT_TRUE(rig.rig.pump());
    ASSERT_TRUE(dot_pos(rig, x1, y1));
    const float narrow = std::fabs(x1 - x0);

    ASSERT_TRUE(rig.rig.click("spread-inc"));
    float xMid = 0, yMid = 0;
    ASSERT_TRUE(dot_pos(rig, xMid, yMid));
    ASSERT_TRUE(rig.rig.pump());
    float x2 = 0, y2 = 0;
    ASSERT_TRUE(dot_pos(rig, x2, y2));
    const float wide = std::fabs(x2 - xMid);
    EXPECT_GT(wide, narrow + 4.f) << "narrow " << narrow << " wide " << wide;
    EXPECT_TRUE(log_has(rig, "散布"));

    ASSERT_TRUE(rig.rig.click("spread-dec"));
    ASSERT_TRUE(rig.rig.pump());
    EXPECT_NE(rig.rig.get("spread-value", "text").find("散布"), std::string::npos);
}

TEST(AppInteract, ParticleDragKeepsTheNewWidths) {
    ParticleRig rig;
    ASSERT_TRUE(rig.up());
    float ex = 0, ey = 0, ew = 0, eh = 0;
    float cx = 0, cy = 0, cw = 0, ch = 0;
    float px = 0, py = 0, pw = 0, ph = 0;
    float sx = 0, sy = 0, sw = 0, sh = 0;
    ASSERT_TRUE(box(rig, "effects", ex, ey, ew, eh));
    ASSERT_TRUE(box(rig, "center", cx, cy, cw, ch));
    ASSERT_TRUE(box(rig, "params", px, py, pw, ph));
    ASSERT_TRUE(box(rig, "split-tree", sx, sy, sw, sh));

    ASSERT_TRUE(rig.rig.drag(sx + sw * 0.5f, sy + sh * 0.5f, sx + sw * 0.5f + 120.f, sy + sh * 0.5f));
    float ex2 = 0, ey2 = 0, ew2 = 0, eh2 = 0;
    float cx2 = 0, cy2 = 0, cw2 = 0, ch2 = 0;
    ASSERT_TRUE(box(rig, "effects", ex2, ey2, ew2, eh2));
    ASSERT_TRUE(box(rig, "center", cx2, cy2, cw2, ch2));
    EXPECT_GT(ew2, ew + 60.f) << ew << " -> " << ew2;
    EXPECT_LT(cw2, cw - 40.f) << cw << " -> " << cw2;

    float qx = 0, qy = 0, qw = 0, qh = 0;
    ASSERT_TRUE(box(rig, "split-param", qx, qy, qw, qh));
    ASSERT_TRUE(box(rig, "params", px, py, pw, ph));
    ASSERT_TRUE(box(rig, "center", cx, cy, cw, ch));
    ASSERT_TRUE(rig.rig.drag(qx + qw * 0.5f, qy + qh * 0.5f, qx + qw * 0.5f - 120.f, qy + qh * 0.5f));
    float px2 = 0, py2 = 0, pw2 = 0, ph2 = 0;
    float cx3 = 0, cy3 = 0, cw3 = 0, ch3 = 0;
    ASSERT_TRUE(box(rig, "params", px2, py2, pw2, ph2));
    ASSERT_TRUE(box(rig, "center", cx3, cy3, cw3, ch3));
    EXPECT_GT(pw2, pw + 60.f) << pw << " -> " << pw2;
    EXPECT_LT(cw3, cw - 40.f);

    ASSERT_TRUE(rig.rig.pump());
    float ex3 = 0, ey3 = 0, ew3 = 0, eh3 = 0;
    float px3 = 0, py3 = 0, pw3 = 0, ph3 = 0;
    ASSERT_TRUE(box(rig, "effects", ex3, ey3, ew3, eh3));
    ASSERT_TRUE(box(rig, "params", px3, py3, pw3, ph3));
    EXPECT_NEAR(ew3, ew2, 1.f);
    EXPECT_NEAR(pw3, pw2, 1.f);
}

TEST(AppInteract, ParticleShaderRoundTrip) {
    ParticleRig rig;
    ASSERT_TRUE(rig.up());
    const fs::path fixture = rig.dir / "in.glsl";
    const fs::path exported = rig.dir / "out.glsl";
    write_bytes(fixture, kShader);
    ASSERT_TRUE(fs::is_regular_file(fixture));

    ASSERT_TRUE(rig.rig.app.document().set("shader-path", "value", fixture.string()));
    ASSERT_TRUE(rig.rig.click("shader-import"));
    EXPECT_EQ(rig.rig.get("shader-source", "value"), kShader);
    EXPECT_NE(rig.rig.get("shader-source", "value").find("keep-me-zui-particle"), std::string::npos);
    EXPECT_NE(rig.rig.get("shader-source", "value").find("u_custom"), std::string::npos);

    ASSERT_TRUE(rig.rig.type("shader-source", "\n// typed-marker\n"));
    EXPECT_NE(rig.rig.get("shader-source", "value").find("typed-marker"), std::string::npos);

    ASSERT_TRUE(rig.rig.app.document().set("shader-out", "value", exported.string()));
    ASSERT_TRUE(rig.rig.click("shader-export"));
    const std::string first = read_bytes(exported);
    EXPECT_EQ(first, rig.rig.get("shader-source", "value"));
    EXPECT_NE(first.find("keep-me-zui-particle"), std::string::npos);
    EXPECT_NE(first.find("u_custom"), std::string::npos);
    EXPECT_NE(first.find("typed-marker"), std::string::npos);

    ASSERT_TRUE(rig.rig.click("speed-inc"));
    ASSERT_TRUE(rig.rig.pump());
    EXPECT_NE(rig.rig.get("shader-source", "value").find("U_SPEED 2.0"), std::string::npos);
    EXPECT_NE(rig.rig.get("shader-source", "value").find("keep-me-zui-particle"), std::string::npos);
    EXPECT_TRUE(log_has(rig, "速度 2.0"));

    ASSERT_TRUE(rig.rig.click("shader-export"));
    const std::string second = read_bytes(exported);
    EXPECT_EQ(second, rig.rig.get("shader-source", "value"));
    EXPECT_NE(second.find("U_SPEED 2.0"), std::string::npos);
    EXPECT_NE(second.find("u_custom"), std::string::npos);
    EXPECT_NE(second.find("typed-marker"), std::string::npos);

    ASSERT_TRUE(rig.rig.app.document().set("shader-path", "value", exported.string()));
    ASSERT_TRUE(rig.rig.click("shader-import"));
    EXPECT_EQ(rig.rig.get("shader-source", "value"), second);
}

TEST(AppInteract, ParticleEffectAndCountMatchTheBrowser) {
    std::string error;
    const nlohmann::json oracle = zui::test::load_browser_oracle(error);
    ASSERT_TRUE(error.empty()) << error;
    ASSERT_TRUE(oracle.contains("particle")) << "browser oracle has no particle readback";
    const nlohmann::json& spec = oracle.at("particle");

    ParticleRig rig;
    ASSERT_TRUE(rig.up());
    ASSERT_TRUE(rig.rig.click("effect-snow"));
    ASSERT_TRUE(rig.rig.pump());
    EXPECT_TRUE(rig.rig.has_class("effect-snow", "is-on"));
    EXPECT_FALSE(rig.rig.has_class("effect-fountain", "is-on"));
    EXPECT_TRUE(log_has(rig, spec.at("snow_log").get<std::string>()));
    EXPECT_EQ(count_dots(rig), spec.at("snow_dots").get<int>());
    EXPECT_EQ(rig.rig.get("count-value", "text"), spec.at("count_text").get<std::string>());

    ASSERT_TRUE(rig.rig.click("count-inc"));
    ASSERT_TRUE(rig.rig.pump());
    EXPECT_EQ(count_dots(rig), spec.at("raised_dots").get<int>());
    EXPECT_EQ(rig.rig.get("count-value", "text"), spec.at("raised_count_text").get<std::string>());
    EXPECT_TRUE(log_has(rig, spec.at("raised_log").get<std::string>()));
}

TEST(AppInteract, ParticleEffectStaysInsideItsBound) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    ParticleRig rig;
    ASSERT_TRUE(rig.up());
    // The interval restamps the field every frame, so settle() would never see an idle frame.
    particle::stop(rig.session);
    const zui::perf::FrameWork work = rig.rig.measure([&] {
        ASSERT_TRUE(zui::frame::Access::click_id(rig.rig.app, "effect-snow"));
    });
    EXPECT_TRUE(log_has(rig, "飘雪"));
    EXPECT_TRUE(rig.rig.has_class("effect-snow", "is-on"));
    EXPECT_LE(work.cascade_visited, 256u) << "cascade=" << work.cascade_visited;
    EXPECT_LE(work.laid_out, 320u) << "laid_out=" << work.laid_out;
    EXPECT_LE(work.boxes_created, 256u) << "boxes=" << work.boxes_created;
    EXPECT_LE(work.flow_remeasures, 512u) << "flow=" << work.flow_remeasures;
}

TEST(AppInteract, ParticleIdleDoesNotWalkPaint) {
#ifdef NDEBUG
    GTEST_SKIP() << "FrameWork is zero in Release";
#endif
    ParticleRig rig;
    ASSERT_TRUE(rig.up());
    particle::stop(rig.session);
    ASSERT_TRUE(rig.rig.pump());
    ASSERT_TRUE(rig.rig.pump());
    const zui::perf::FrameWork second = zui::perf::last_frame_work();
    EXPECT_EQ(second.cascade_visited, 0u);
    EXPECT_EQ(second.laid_out, 0u);
    EXPECT_EQ(second.painted_walked, 0u);
    ASSERT_TRUE(rig.rig.pump());
    const zui::perf::FrameWork third = zui::perf::last_frame_work();
    EXPECT_EQ(third.cascade_visited, 0u);
    EXPECT_EQ(third.laid_out, 0u);
    EXPECT_EQ(third.painted_walked, 0u);
}
