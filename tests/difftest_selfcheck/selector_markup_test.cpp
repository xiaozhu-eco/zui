// MARKUP §3: one author sheet keeps later rules, a matching @media, and @keyframes
// when it also contains an unknown at-rule, an unknown declaration, and a selector
// the engine still does not implement.

#include "selfcheck_scene.h"

#include "css/transition/animation.h"

#include <cmath>
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

TEST(SelectorMarkup, SheetRecoveryKeepsLaterRuleAndAnimation) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "selector-markup.html")) << error;

    zui::html::Node* later = zui::html::find(scene.doc, "later");
    zui::html::Node* mixed = zui::html::find(scene.doc, "mixed");
    zui::html::Node* media = zui::html::find(scene.doc, "media");
    zui::html::Node* poison = zui::html::find(scene.doc, "poison");
    zui::html::Node* mover = zui::html::find(scene.doc, "mover");
    ASSERT_NE(later, nullptr);
    ASSERT_NE(mixed, nullptr);
    ASSERT_NE(media, nullptr);
    ASSERT_NE(poison, nullptr);
    ASSERT_NE(mover, nullptr);

    EXPECT_EQ(computed_of(later, "width"), "40px");
    EXPECT_EQ(computed_of(later, "height"), "18px");
    EXPECT_EQ(computed_of(mixed, "width"), "33px");
    EXPECT_EQ(computed_of(mixed, "height"), "11px");
    EXPECT_EQ(computed_of(media, "width"), "55px");
    EXPECT_EQ(computed_of(media, "height"), "22px");
    EXPECT_NE(computed_of(poison, "width"), "9px");
    EXPECT_NE(computed_of(poison, "height"), "9px");
    EXPECT_NE(computed_of(later, "padding-bottom"), "7px");
    zui::html::Node* line = zui::html::find(scene.doc, "line");
    ASSERT_NE(line, nullptr);
    EXPECT_NE(computed_of(line, "padding-bottom"), "8px");

    const std::string animation = computed_of(mover, "animation");
    EXPECT_NE(animation.find("pulse"), std::string::npos) << animation;

    bool saw_keyframes = false;
    for (const std::string& source : scene.css.sheet_sources()) {
        if (source.find("@keyframes") != std::string::npos &&
            source.find("pulse") != std::string::npos) {
            saw_keyframes = true;
        }
    }
    EXPECT_TRUE(saw_keyframes);

    zui::css::Animations anim;
    anim.sync(scene.doc, scene.css.sheet_sources(), 0.0);
    anim.sample(0.0);
    EXPECT_NE(mover->used.flags & zui::html::VisualUsed::kOpacity, 0u);
    EXPECT_NEAR(mover->used.opacity, 0.f, 0.02f);
    anim.sample(0.5);
    EXPECT_NEAR(mover->used.opacity, 0.5f, 0.02f);
}

TEST(SelectorMarkup, HtmlPseudosMatchAndTwinsDoNotShare) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "selector-markup.html")) << error;
    zui::html::Document& doc = scene.doc;

    expect_prop(doc, "ph", "padding-top", "23px");
    expect_prop(doc, "ph-ta", "padding-top", "23px");
    expect_not_prop(doc, "ph-filled", "padding-top", "23px");
    expect_not_prop(doc, "ph-none", "padding-top", "23px");
    expect_not_prop(doc, "ph-ta-x", "padding-top", "23px");

    const char* required_ids[] = {
        "req-text", "req-search", "req-url",     "req-tel",   "req-email", "req-password",
        "req-date", "req-month",  "req-week",    "req-time",  "req-dt",    "req-number",
        "req-check","req-radio",  "req-file",    "sreq",      "treq"};
    for (const char* id : required_ids) {
        expect_prop(doc, id, "padding-top", "24px");
        expect_not_prop(doc, id, "padding-top", "25px");
    }
    const char* optional_ids[] = {"opt-text", "opt-file", "sopt", "topt"};
    for (const char* id : optional_ids) {
        expect_prop(doc, id, "padding-top", "25px");
        expect_not_prop(doc, id, "padding-top", "24px");
    }
    const char* neither_req[] = {"bar-hidden", "bar-range", "bar-color", "bar-button", "bar-submit",
                                 "bar-reset",  "bar-image", "divreq",     "btnreq"};
    for (const char* id : neither_req) {
        expect_not_prop(doc, id, "padding-top", "24px");
        expect_not_prop(doc, id, "padding-top", "25px");
    }

    const char* read_write_ids[] = {
        "rw",        "rw-search", "rw-url",      "rw-tel",     "rw-email",   "rw-password", "rw-date",
        "rw-month",  "rw-week",   "rw-time",     "rw-dt",      "rw-number",  "ta",          "host",
        "edit-child","in-host",   "host-empty",  "edit-empty", "host-plain", "edit-plain",  "host-true",
        "reopen"};
    for (const char* id : read_write_ids) {
        expect_prop(doc, id, "padding-top", "26px");
    }
    const char* read_only_ids[] = {
        "rw-ro",     "ro-dis",    "ro-search", "ta-ro",      "ta-dis",     "cb",
        "ro-radio",  "ro-file",   "ro-hidden", "ro-range",   "ro-color",   "ro-button",
        "ro-submit", "ro-reset",  "ro-image",  "cb-host",    "locked",     "locked-child",
        "locked-case","plain",    "plain-btn"};
    for (const char* id : read_only_ids) {
        expect_prop(doc, id, "padding-top", "27px");
    }

    const char* default_ids[] = {"def-on", "rad-def", "opt-def", "sub1", "ext-only", "img-sub"};
    for (const char* id : default_ids) {
        expect_prop(doc, id, "padding-top", "28px");
    }
    const char* not_default[] = {"def-off", "rad-other", "opt-other", "opt-live", "sub2",
                                 "reset",   "text-in-form", "orphan", "outside", "missing-form",
                                 "img-later"};
    for (const char* id : not_default) {
        expect_not_prop(doc, id, "padding-top", "28px");
    }

    const char* ind_ids[] = {"prog-ind", "r1", "r2", "r-none", "r-none2", "fa1", "fa2"};
    for (const char* id : ind_ids) {
        expect_prop(doc, id, "padding-top", "29px");
    }
    const char* not_ind[] = {"prog-val", "prog-empty", "r3", "r4", "fb1", "fb2"};
    for (const char* id : not_ind) {
        expect_not_prop(doc, id, "padding-top", "29px");
    }

    const char* link_ids[] = {"a-href", "a-empty", "area-href"};
    for (const char* id : link_ids) {
        expect_prop(doc, id, "padding-left", "30px");
        expect_prop(doc, id, "margin-top", "31px");
        expect_prop(doc, id, "margin-bottom", "32px");
        expect_not_prop(doc, id, "padding-right", "40px");
    }
    for (const char* id : {"a-none", "area-none"}) {
        expect_not_prop(doc, id, "padding-left", "30px");
        expect_not_prop(doc, id, "margin-top", "31px");
        expect_not_prop(doc, id, "margin-bottom", "32px");
        expect_not_prop(doc, id, "padding-right", "40px");
    }

    const char* zh_cn[] = {"lang-zhcn", "lang-child", "lang-case", "lang-xml"};
    for (const char* id : zh_cn) {
        expect_prop(doc, id, "padding-top", "33px");
        expect_prop(doc, id, "padding-right", "34px");
        expect_not_prop(doc, id, "padding-bottom", "35px");
        expect_prop(doc, id, "padding-left", "36px");
    }
    expect_prop(doc, "lang-zh", "padding-top", "33px");
    expect_not_prop(doc, "lang-zh", "padding-right", "34px");
    expect_not_prop(doc, "lang-zh", "padding-bottom", "35px");
    expect_not_prop(doc, "lang-zh", "padding-bottom", "7px");
    expect_prop(doc, "lang-zh", "padding-left", "36px");
    for (const char* id : {"lang-en", "lang-override"}) {
        expect_not_prop(doc, id, "padding-top", "33px");
        expect_not_prop(doc, id, "padding-right", "34px");
        expect_prop(doc, id, "padding-bottom", "35px");
        expect_prop(doc, id, "padding-left", "36px");
    }
    for (const char* prop : {"padding-top", "padding-right", "padding-bottom", "padding-left"}) {
        const char* banned = prop[8] == 't' ? "33px" : prop[8] == 'r' ? "34px" : prop[8] == 'b' ? "35px" : "36px";
        expect_not_prop(doc, "lang-none", prop, banned);
    }
    expect_prop(doc, "under-zh", "margin-right", "37px");
    expect_not_prop(doc, "under-en", "margin-right", "37px");

    expect_prop(doc, "dis", "margin-left", "41px");
    expect_not_prop(doc, "foc", "margin-left", "43px");
    expect_prop(doc, "nth2", "margin-left", "44px");
    expect_not_prop(doc, "nth1", "margin-left", "44px");
    expect_not_prop(doc, "fw-a", "padding-top", "21px");
    expect_not_prop(doc, "fw-b", "padding-top", "21px");

    zui::html::Node* ph = zui::html::find(scene.doc, "ph");
    zui::html::Node* ph_filled = zui::html::find(scene.doc, "ph-filled");
    ASSERT_NE(ph, nullptr);
    ASSERT_NE(ph_filled, nullptr);
    ASSERT_TRUE(zui::html::set_field(scene.doc, "ph", "value", "z"));
    restyle(scene, {ph});
    expect_not_prop(doc, "ph", "padding-top", "23px");
    expect_not_prop(doc, "ph-filled", "padding-top", "23px");
    ASSERT_TRUE(zui::html::set_field(scene.doc, "ph-filled", "value", ""));
    restyle(scene, {ph_filled});
    expect_prop(doc, "ph-filled", "padding-top", "23px");
    expect_not_prop(doc, "ph", "padding-top", "23px");
    scene.frame();
    expect_prop(doc, "ph-filled", "padding-top", "23px");
    expect_not_prop(doc, "ph", "padding-top", "23px");

    zui::html::Node* later = zui::html::find(scene.doc, "later");
    zui::html::Node* r1 = zui::html::find(scene.doc, "r1");
    zui::html::Node* r2 = zui::html::find(scene.doc, "r2");
    ASSERT_NE(later, nullptr);
    ASSERT_NE(r1, nullptr);
    ASSERT_NE(r2, nullptr);
    const auto radio_closure = scene.css.closure_nodes(scene.doc, {r1});
    EXPECT_NE(radio_closure.count(r2), 0u);
    EXPECT_EQ(radio_closure.count(later), 0u);
    ASSERT_TRUE(zui::html::set_field(scene.doc, "r1", "checked", "true"));
    restyle(scene, {r1});
    expect_not_prop(doc, "r1", "padding-top", "29px");
    expect_not_prop(doc, "r2", "padding-top", "29px");
    expect_prop(doc, "later", "width", "40px");
    expect_prop(doc, "fa1", "padding-top", "29px");

    // A missing name is not a radio group. Checking r-none drops :indeterminate on that
    // radio alone; r-none2 stays indeterminate.
    zui::html::Node* r_none = zui::html::find(scene.doc, "r-none");
    zui::html::Node* r_none2 = zui::html::find(scene.doc, "r-none2");
    ASSERT_NE(r_none, nullptr);
    ASSERT_NE(r_none2, nullptr);
    const auto none_closure = scene.css.closure_nodes(scene.doc, {r_none});
    EXPECT_EQ(none_closure.count(r_none2), 0u);
    ASSERT_TRUE(zui::html::set_field(scene.doc, "r-none", "checked", "true"));
    restyle(scene, {r_none});
    expect_not_prop(doc, "r-none", "padding-top", "29px");
    expect_prop(doc, "r-none2", "padding-top", "29px");

    ASSERT_TRUE(zui::html::set_field(scene.doc, "def-on", "checked", "false"));
    ASSERT_TRUE(zui::html::set_field(scene.doc, "def-off", "checked", "true"));
    ASSERT_TRUE(zui::html::set_field(scene.doc, "rad-def", "checked", "false"));
    ASSERT_TRUE(zui::html::set_field(scene.doc, "rad-other", "checked", "true"));
    zui::html::select_commit(zui::html::find(scene.doc, "sel-def"),
                             zui::html::find(scene.doc, "opt-other"));
    scene.frame();
    expect_prop(doc, "def-on", "padding-top", "28px");
    expect_not_prop(doc, "def-off", "padding-top", "28px");
    expect_prop(doc, "rad-def", "padding-top", "28px");
    expect_not_prop(doc, "rad-other", "padding-top", "28px");
    expect_prop(doc, "opt-def", "padding-top", "28px");
    expect_not_prop(doc, "opt-other", "padding-top", "28px");
    expect_not_prop(doc, "opt-live", "padding-top", "28px");

    zui::html::Node* focused = nullptr;
    zui::html::Node* hovered = nullptr;
    std::vector<zui::html::Node*> roots;
    zui::input::InteractionHost host;
    host.document = &scene.doc;
    host.layout = scene.layout.get();
    host.focused = &focused;
    host.hovered = &hovered;
    host.restyle_roots = [&roots](const std::vector<zui::html::Node*>& scheduled) {
        roots = scheduled;
    };
    zui::input::Interaction interaction(host);
    zui::html::Node* fw_btn = zui::html::find(scene.doc, "fw-btn");
    zui::html::Node* fw_a = zui::html::find(scene.doc, "fw-a");
    zui::html::Node* fw_b = zui::html::find(scene.doc, "fw-b");
    ASSERT_NE(fw_btn, nullptr);
    ASSERT_TRUE(interaction.focus(fw_btn, false));
    const auto focus_closure = scene.css.closure_nodes(scene.doc, roots);
    EXPECT_NE(focus_closure.count(fw_a), 0u);
    EXPECT_EQ(focus_closure.count(fw_b), 0u);
    EXPECT_EQ(focus_closure.count(later), 0u);
    scene.css.cascade(scene.doc, 800, 600, zui::HostMediaFacts{}, &focus_closure);
    expect_prop(doc, "fw-a", "padding-top", "21px");
    expect_prop(doc, "fw-btn", "margin-top", "21px");
    expect_not_prop(doc, "fw-b", "padding-top", "21px");
    expect_not_prop(doc, "fw-twin", "margin-top", "21px");
    scene.frame();
    expect_prop(doc, "fw-a", "padding-top", "21px");
    expect_prop(doc, "fw-btn", "margin-top", "21px");
    expect_not_prop(doc, "fw-b", "padding-top", "21px");
    expect_not_prop(doc, "fw-twin", "margin-top", "21px");

    zui::html::Node* foc = zui::html::find(scene.doc, "foc");
    ASSERT_NE(foc, nullptr);
    ASSERT_TRUE(interaction.focus(foc, true));
    const auto foc_closure = scene.css.closure_nodes(scene.doc, roots);
    scene.css.cascade(scene.doc, 800, 600, zui::HostMediaFacts{}, &foc_closure);
    expect_prop(doc, "foc", "margin-left", "43px");
    expect_not_prop(doc, "fw-a", "padding-top", "21px");
    expect_not_prop(doc, "fw-btn", "margin-top", "21px");
    expect_prop(doc, "dis", "margin-left", "41px");
    expect_prop(doc, "nth2", "margin-left", "44px");
    scene.frame();
    expect_prop(doc, "foc", "margin-left", "43px");
    expect_not_prop(doc, "fw-a", "padding-top", "21px");
    expect_not_prop(doc, "fw-b", "padding-top", "21px");
    expect_prop(doc, "dis", "margin-left", "41px");
    expect_prop(doc, "nth2", "margin-left", "44px");
}
