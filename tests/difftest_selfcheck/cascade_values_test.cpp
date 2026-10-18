// Cascade, computed values, content, fonts, and reintern.
// L6 architecture self-checks (TESTPLAN section 12).
// Shared scene harness: selfcheck_scene.h.

#include "selfcheck_scene.h"

using namespace difftest_selfcheck;

// CSS Content 3: attr(name, "fallback") keeps the fallback; a cache hit must re-resolve against
// this element's attributes (ShareKey does not fingerprint data-* used only in content).
TEST(DifftestSelfCheck, ContentAttrFallback) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "content-attr-fallback.html")) << error;

    auto* has = zui::html::find(scene.doc, "has-label");
    auto* missing = zui::html::find(scene.doc, "missing-label");
    ASSERT_NE(has, nullptr);
    ASSERT_NE(missing, nullptr);
    ASSERT_NE(has->pseudo_node[0], nullptr);
    ASSERT_NE(missing->pseudo_node[0], nullptr);
    const auto& has_map = has->pseudo[0];
    ASSERT_NE(has_map, nullptr);
    auto cit = has_map->find("content");
    ASSERT_NE(cit, has_map->end());
    EXPECT_NE(std::string(cit->second).find("FALLBACK"), std::string::npos)
        << "stored content lost the fallback: " << std::string(cit->second);
    EXPECT_EQ(has->pseudo_node[0]->text, "OK");
    EXPECT_EQ(missing->pseudo_node[0]->text, "FALLBACK");
}

// B1: author counter-reset/increment + content:counter()/counters().
TEST(DifftestSelfCheck, ContentCounters) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "content-counters.html")) << error;

    auto* c1 = zui::html::find(scene.doc, "c1");
    auto* c2 = zui::html::find(scene.doc, "c2");
    auto* inner = zui::html::find(scene.doc, "inner");
    ASSERT_NE(c1, nullptr);
    ASSERT_NE(c2, nullptr);
    ASSERT_NE(inner, nullptr);
    ASSERT_NE(c1->pseudo_node[0], nullptr);
    ASSERT_NE(c2->pseudo_node[0], nullptr);
    ASSERT_NE(inner->pseudo_node[0], nullptr);
    EXPECT_EQ(c1->pseudo_node[0]->text, "1. ");
    EXPECT_EQ(c2->pseudo_node[0]->text, "2. ");
    EXPECT_EQ(inner->pseudo_node[0]->text, "1.1");
}

// B1: `font` shorthand resets omitted longhands (CSS Fonts 3/4 §3.7) and accepts stretch/variant.
TEST(DifftestSelfCheck, FontShorthandResets) {
    using Parts = std::vector<std::pair<std::string, std::string>>;
    auto get = [](const Parts& parts, const char* name) -> std::string {
        for (const auto& p : parts) {
            if (p.first == name) {
                return p.second;
            }
        }
        return {};
    };

    Parts parts;
    ASSERT_TRUE(zui::css::expand_shorthand_value("font", "16px sans-serif", parts));
    EXPECT_EQ(get(parts, "font-style"), "normal");
    EXPECT_EQ(get(parts, "font-variant"), "normal");
    EXPECT_EQ(get(parts, "font-weight"), "normal");
    EXPECT_EQ(get(parts, "font-stretch"), "normal");
    EXPECT_EQ(get(parts, "font-size"), "16px");
    EXPECT_EQ(get(parts, "line-height"), "normal");
    EXPECT_EQ(get(parts, "font-family"), "sans-serif");
    EXPECT_EQ(get(parts, "font-kerning"), "auto");
    EXPECT_EQ(get(parts, "font-feature-settings"), "normal");
    EXPECT_EQ(get(parts, "font-size-adjust"), "none");

    parts.clear();
    ASSERT_TRUE(zui::css::expand_shorthand_value(
        "font", "italic small-caps condensed 700 14px/1.2 Georgia, serif", parts));
    EXPECT_EQ(get(parts, "font-style"), "italic");
    EXPECT_EQ(get(parts, "font-variant"), "small-caps");
    EXPECT_EQ(get(parts, "font-stretch"), "condensed");
    EXPECT_EQ(get(parts, "font-weight"), "700");
    EXPECT_EQ(get(parts, "font-size"), "14px");
    EXPECT_EQ(get(parts, "line-height"), "1.2");
    EXPECT_EQ(get(parts, "font-family"), "Georgia, serif");

    parts.clear();
    ASSERT_TRUE(zui::css::expand_shorthand_value("font", "menu", parts));
    EXPECT_EQ(get(parts, "font-family"), "system-ui");
    EXPECT_EQ(get(parts, "font-variant"), "normal");
    EXPECT_EQ(get(parts, "font-stretch"), "normal");
    EXPECT_EQ(get(parts, "line-height"), "normal");
}

// B1: ch / ex / lh resolve in length parsing (CSS Values §6.1.1).
TEST(DifftestSelfCheck, FontRelativeUnits) {
    zui::css::FontRelativeUnits rel;
    rel.ch = 10.f;
    rel.ex = 8.f;
    rel.lh = 30.f;
    auto r = zui::css::resolve_length("10ch", 20.f, 16.f, rel);
    ASSERT_TRUE(r.valid);
    EXPECT_NEAR(r.px, 100.f, 0.01f);
    r = zui::css::resolve_length("10ex", 20.f, 16.f, rel);
    ASSERT_TRUE(r.valid);
    EXPECT_NEAR(r.px, 80.f, 0.01f);
    r = zui::css::resolve_length("2lh", 20.f, 16.f, rel);
    ASSERT_TRUE(r.valid);
    EXPECT_NEAR(r.px, 60.f, 0.01f);
    r = zui::css::resolve_length("calc(5ch + 10px)", 20.f, 16.f, 0.f, 0.f, rel);
    ASSERT_TRUE(r.valid);
    EXPECT_NEAR(r.px, 60.f, 0.01f);
    // Spec fallbacks when bases are unknown: ch/ex → 0.5em, lh → 1em.
    r = zui::css::resolve_length("2ch", 20.f, 16.f);
    ASSERT_TRUE(r.valid);
    EXPECT_NEAR(r.px, 20.f, 0.01f);
    r = zui::css::resolve_length("2lh", 20.f, 16.f);
    ASSERT_TRUE(r.valid);
    EXPECT_NEAR(r.px, 40.f, 0.01f);

    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "font-units.html")) << error;
    zui::layout::Box* ch = scene.box("ch");
    zui::layout::Box* ex = scene.box("ex");
    zui::layout::Box* lh = scene.box("lh");
    ASSERT_NE(ch, nullptr);
    ASSERT_NE(ex, nullptr);
    ASSERT_NE(lh, nullptr);
    // Without a measured face the engine still resolves via the 0.5em / used-lh path; widths
    // must be positive and lh height must be 2 × used line-height (30px → 60).
    EXPECT_GT(ch->geo.width, 1.f);
    EXPECT_GT(ex->geo.width, 1.f);
    EXPECT_NEAR(lh->geo.height, 60.f, 1.f);
}

// B1: expanded inherit allowlist copies table/fragmentation/text extras to descendants.
TEST(DifftestSelfCheck, InheritAllowlistExtras) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "inherit-extra.html")) << error;
    auto prop = [&](const char* id, const char* key) -> std::string {
        zui::html::Node* n = zui::html::find(scene.doc, id);
        EXPECT_NE(n, nullptr) << id;
        if (n == nullptr || n->computed == nullptr) {
            return {};
        }
        const auto it = n->computed->find(key);
        return it == n->computed->end() ? std::string() : std::string(it->second);
    };
    EXPECT_EQ(prop("child", "border-collapse"), "collapse");
    EXPECT_EQ(prop("grand", "border-collapse"), "collapse");
    EXPECT_EQ(prop("grand", "orphans"), "3");
    EXPECT_EQ(prop("grand", "widows"), "4");
    EXPECT_FALSE(prop("grand", "caret-color").empty());
    EXPECT_NE(prop("grand", "quotes").find("«"), std::string::npos);
}

// B2: word-break:break-all wraps Latin runs that normal keeps on one overflowing line.
TEST(DifftestSelfCheck, WordBreakAllWrapsLatin) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "word-break.html")) << error;
    zui::layout::Box* normal = scene.box("normal");
    zui::layout::Box* brk = scene.box("breakall");
    ASSERT_NE(normal, nullptr);
    ASSERT_NE(brk, nullptr);
    EXPECT_EQ(normal->style().word_break, zui::layout::WordBreak::Normal);
    EXPECT_EQ(brk->style().word_break, zui::layout::WordBreak::BreakAll);
    EXPECT_GT(brk->geo.height, normal->geo.height + 0.5f)
        << "break-all should wrap the Latin run onto extra lines";
}

// B1: content: url() on ::before builds a replaced box with intrinsic image size.
TEST(DifftestSelfCheck, ContentUrlImage) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "content-url.html")) << error;

    auto* pic = zui::html::find(scene.doc, "pic");
    ASSERT_NE(pic, nullptr);
    ASSERT_NE(pic->pseudo_node[0], nullptr);
    EXPECT_TRUE(pic->pseudo_node[0]->attrs.count("src") != 0);
    EXPECT_FALSE(pic->pseudo_node[0]->attrs.at("src").empty());

    zui::layout::Box* before = nullptr;
    zui::layout::Box* host = scene.box("pic");
    ASSERT_NE(host, nullptr);
    for (zui::layout::Box* ch : host->children) {
        if (ch != nullptr && ch->generated == zui::html::PseudoElement::Before) {
            before = ch;
            break;
        }
    }
    ASSERT_NE(before, nullptr);
    EXPECT_TRUE(before->is_replaced);
    EXPECT_FALSE(before->replaced_src.empty());
    EXPECT_GT(before->geo.width, 1.f);
    EXPECT_GT(before->geo.height, 1.f);

    zui::paint::DisplayList list;
    zui::paint::Painter painter;
    painter.set_retained(false);
    painter.paint(*scene.layout->root(), list, 800, 600);
    bool saw_image = false;
    for (const auto& it : list.items()) {
        if (it.op == zui::paint::DisplayOp::DrawImage &&
            it.text.find("green4") != std::string::npos) {
            saw_image = true;
            break;
        }
    }
    EXPECT_TRUE(saw_image) << "content:url() should emit DrawImage for the pseudo box";
}

// Cascade 5 §6.1: origin+importance layers. zui UA sheet marks #ua-important-* with !important;
// author normal / author !important must lose. Author !important still beats author normal.
TEST(DifftestSelfCheck, CascadeImportantOriginLayers) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "cascade-important.html")) << error;

    auto width_of = [&](const char* id) -> float {
        const zui::layout::Box* b = scene.box(id);
        EXPECT_NE(b, nullptr) << id;
        return b != nullptr ? b->geo.width : -1.f;
    };
    EXPECT_FLOAT_EQ(width_of("ua-important-probe"), 120.f);
    EXPECT_FLOAT_EQ(width_of("ua-important-vs-author-important"), 120.f);
    EXPECT_FLOAT_EQ(width_of("author-important-wins"), 120.f);
}

// LAYOUT.md section 6: `display` and `position` changes are reattaches, and `flush_style` never
// marks them - the box keeps its identity and re-interns its style inside the sync. The host still
// has to miss, or the moved box keeps the flow it was in.
TEST(DifftestSelfCheck, PositionChangeReinternsTheBox) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error)) << error;
    zui::layout::Box* label_before_box = scene.box("label");
    ASSERT_NE(label_before_box, nullptr);
    const zui::layout::LayoutResult* first_before = Scene::result(scene.box("first"));
    const zui::layout::LayoutResult* label_before = Scene::result(label_before_box);
    zui::html::Node* body = zui::html::find_tag(scene.doc.root.get(), "body");
    ASSERT_NE(first_before, nullptr);
    ASSERT_NE(label_before, nullptr);
    ASSERT_NE(body, nullptr);
    const float label_world_y_before = label_before_box->geo.world_y;
    const float label_inline_before = label_before->geometry.border_box_inline;

    ASSERT_TRUE(zui::html::add_class(scene.doc, "label", "absolute"));
    scene.app_frame();

    zui::layout::Box* label = scene.box("label");
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label, label_before_box)
        << "a position change replaced the box instead of restyling it";
    const zui::layout::LayoutResult* label_after = Scene::result(label);
    ASSERT_NE(label_after, nullptr);
    // A value read from the box the frame published, not the result pointer: results are freed and
    // re-allocated every frame and the allocator can hand the same address back (see
    // `published_records_current`). Out of flow the box shrink-to-fits, so the inline size is the
    // thing that proves it was laid out again under the new style.
    EXPECT_NE(label_after->geometry.border_box_inline, label_inline_before)
        << "the absolutely positioned box was never laid out";
    EXPECT_LT(label->geo.world_y, label_world_y_before)
        << "the box is still in flow: its style was not re-interned after the reattach";
    EXPECT_EQ(Scene::result(scene.box("first")), first_before)
        << "a box the change cannot move was laid out again";
}

// The studio's `on_open` shape, and LAYOUT.md section 6 with it: a fragment that brings a fixed
// mask, plus a text change on a node the page already has, in the same frame. The text change
// drops those boxes' slots and makes the results above it stale; the fragment drops its host. What
// neither can move keeps its result, and the mask still has to reach the collector.
TEST(DifftestSelfCheck, MaskFragmentAndTextChangeInOneFrameStayLocal) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error)) << error;
    zui::html::Node* body = zui::html::find_tag(scene.doc.root.get(), "body");
    zui::html::Node* label = zui::html::find(scene.doc, "label");
    ASSERT_NE(body, nullptr);
    ASSERT_NE(label, nullptr);
    const zui::layout::LayoutResult* first_before = Scene::result(scene.box("first"));
    const zui::layout::LayoutResult* tall_before = Scene::result(scene.box("tall"));
    zui::layout::Box* label_box = scene.box("label");
    ASSERT_NE(first_before, nullptr);
    ASSERT_NE(tall_before, nullptr);
    ASSERT_NE(label_box, nullptr);
    zui::layout::Box* label_text = nullptr;
    for (zui::layout::Box* child : label_box->children) {
        if (child != nullptr && child->role == zui::layout::BoxRole::Text) {
            label_text = child;
            break;
        }
    }
    ASSERT_NE(label_text, nullptr) << "the label has no text box to change";
    const zui::layout::LayoutResult* text_before = Scene::result(label_text);
    ASSERT_NE(text_before, nullptr);
    const float text_width_before = text_before->geometry.border_box_inline;

    ASSERT_TRUE(zui::html::attach_fragment(scene.doc, body, fixture("fragment-mask.html"), error))
        << error;
    // `App::set(id, "text", ...)`: write the node, sync its text boxes, mark them Layout.
    label->text = "Dialog open.";
    scene.layout->sync_text(label);
    scene.layout->mark_needs_layout(label);
    scene.app_frame({body});

    // The result *pointer* is not the signal: results are freed and re-allocated every frame and
    // the allocator can hand the same address back, which made this check flake about one run in
    // fifteen (FontArrivalKeepsContentVersion documents the same lesson). What has to hold is that
    // the text box's published result carries the new text's width, and that the label's published
    // result records that text box under its *current* content version - a result that answered
    // from the cache still names the pre-change one.
    const zui::layout::LayoutResult* text_after = Scene::result(label_text);
    ASSERT_NE(text_after, nullptr);
    EXPECT_NE(text_after->geometry.border_box_inline, text_width_before)
        << "the text change did not reach the text box's layout";
    EXPECT_TRUE(published_records_current(label_box, label_text))
        << "the node whose text changed answered from its cache";
    zui::layout::Box* mask = scene.box("mask");
    ASSERT_NE(mask, nullptr) << "the fragment produced no box";
    const zui::layout::LayoutResult* mask_result = Scene::result(mask);
    ASSERT_NE(mask_result, nullptr)
        << "the fixed mask was never asked: the collector did not run in the frame that also "
           "changed text";
    EXPECT_GT(mask_result->geometry.border_box_block, 0.0f) << "the fixed mask has no geometry";
    EXPECT_GT(mask_result->geometry.border_box_inline, 0.0f) << "the fixed mask has no geometry";
    EXPECT_EQ(Scene::result(scene.box("first")), first_before)
        << "a box the changes cannot move was laid out again";
    EXPECT_EQ(Scene::result(scene.box("tall")), tall_before)
        << "a box the changes cannot move was laid out again";
}
