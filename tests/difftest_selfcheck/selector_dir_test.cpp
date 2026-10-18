// Selectors 4 :dir() follows HTML directionality. CSS `direction` is not an input.
// A bad argument list drops that rule only; the rest of the sheet still cascades.

#include "selfcheck_scene.h"

#include <string>
#include <vector>

using namespace difftest_selfcheck;

namespace {

std::string computed_of(const zui::html::Node* node, const char* key) {
    if (node == nullptr || node->computed == nullptr) {
        return "<absent>";
    }
    const auto it = node->computed->find(key);
    return it == node->computed->end() ? std::string("<absent>") : std::string(it->second);
}

void expect_prop(zui::html::Document& doc, const char* id, const char* prop, const char* value) {
    zui::html::Node* node = zui::html::find(doc, id);
    EXPECT_NE(node, nullptr) << id;
    EXPECT_EQ(computed_of(node, prop), value) << id;
}

void expect_not_prop(zui::html::Document& doc, const char* id, const char* prop,
                     const char* value) {
    zui::html::Node* node = zui::html::find(doc, id);
    EXPECT_NE(node, nullptr) << id;
    EXPECT_NE(computed_of(node, prop), value) << id;
}

void restyle(Scene& scene, const std::vector<zui::html::Node*>& roots) {
    const auto closure = scene.css.closure_nodes(scene.doc, roots);
    scene.css.cascade(scene.doc, 800, 600, zui::HostMediaFacts{}, &closure);
}

}  // namespace

TEST(SelectorDir, HtmlDirectionalityAndSheetRecovery) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "selector-dir.html")) << error;
    zui::html::Document& doc = scene.doc;

    expect_prop(doc, "keep", "margin-left", "70px");
    expect_not_prop(doc, "keep", "padding-bottom", "9px");
    expect_not_prop(doc, "keep", "padding-bottom", "8px");
    expect_prop(doc, "keep", "margin-bottom", "74px");
    expect_not_prop(doc, "host-rtl", "margin-bottom", "74px");

    expect_prop(doc, "host-rtl", "padding-right", "72px");
    expect_not_prop(doc, "host-rtl", "padding-right", "10px");

    const char* ltr_ids[] = {"keep",       "up",        "neutral",     "lrm",         "styled",
                             "skip",       "skip-rest", "iso",         "iso-rest",    "auto",
                             "inner",      "peer",      "bdi-empty",   "tel",         "radio-auto",
                             "empty-input"};
    for (const char* id : ltr_ids) {
        expect_prop(doc, id, "padding-top", "84px");
    }
    const char* rtl_ids[] = {"al", "rlm", "digits", "iso-bdi", "host-rtl", "child",
                             "bad", "bdi-he", "text-inherit", "tel-auto"};
    for (const char* id : rtl_ids) {
        expect_prop(doc, id, "padding-top", "83px");
    }

    expect_prop(doc, "styled", "direction", "rtl");

    zui::html::Node* inner = zui::html::find(doc, "inner");
    zui::html::Node* peer = zui::html::find(doc, "peer");
    zui::html::Node* outside = zui::html::find(doc, "keep");
    ASSERT_NE(inner, nullptr);
    ASSERT_NE(peer, nullptr);
    ASSERT_NE(outside, nullptr);
    const auto closure = scene.css.closure_nodes(doc, {inner});
    EXPECT_EQ(closure.count(peer), 1u);
    EXPECT_EQ(closure.count(zui::html::find(doc, "auto")), 1u);
    EXPECT_EQ(closure.count(outside), 0u);

    ASSERT_TRUE(zui::html::set_field(doc, "inner", "text", "\xD7\xAA"));
    restyle(scene, {inner});
    expect_prop(doc, "auto", "padding-top", "83px");
    expect_prop(doc, "inner", "padding-top", "83px");
    expect_prop(doc, "peer", "padding-top", "83px");
    expect_prop(doc, "keep", "padding-top", "84px");
    expect_prop(doc, "child", "padding-top", "83px");
}
