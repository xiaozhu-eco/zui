// List markers: order, counters, advance, and invalidation.
// L6 architecture self-checks (TESTPLAN section 12).
// Shared scene harness: selfcheck_scene.h.

#include "selfcheck_scene.h"

using namespace difftest_selfcheck;

// ::marker M1: author content on a list-item builds a Marker box before ::before; a non-list-item
// ::marker rule does not generate a box.
TEST(DifftestSelfCheck, MarkerOrderBeforeBefore) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "marker-order.html")) << error;
    zui::html::Node* a = zui::html::find(scene.doc, "a");
    ASSERT_NE(a, nullptr);
    zui::html::Node* marker =
        a->pseudo_node[static_cast<int>(zui::html::PseudoElement::Marker)].get();
    zui::html::Node* before =
        a->pseudo_node[static_cast<int>(zui::html::PseudoElement::Before)].get();
    ASSERT_NE(marker, nullptr) << "list-item ::marker { content } should generate a node";
    ASSERT_NE(before, nullptr);
    EXPECT_EQ(marker->text, "M");
    EXPECT_EQ(before->text, "B");

    zui::layout::Box* host = scene.box("a");
    ASSERT_NE(host, nullptr);
    ASSERT_GE(host->children.size(), 2u);
    EXPECT_EQ(host->children[0]->generated, zui::html::PseudoElement::Marker);
    EXPECT_EQ(host->children[1]->generated, zui::html::PseudoElement::Before);

    zui::html::Node* notlist = zui::html::find(scene.doc, "notlist");
    ASSERT_NE(notlist, nullptr);
    EXPECT_EQ(notlist->pseudo_node[static_cast<int>(zui::html::PseudoElement::Marker)].get(),
              nullptr)
        << "non-list-item must not generate ::marker";
}

static const zui::html::Node* marker_of(const zui::html::Node* li) {
    if (li == nullptr) {
        return nullptr;
    }
    return li->pseudo_node[static_cast<int>(zui::html::PseudoElement::Marker)].get();
}

TEST(DifftestSelfCheck, MarkerListItemCounters) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "marker-counters.html")) << error;

    auto text = [](const char* id, Scene& s) -> std::string {
        const zui::html::Node* m = marker_of(zui::html::find(s.doc, id));
        return m != nullptr ? m->text : std::string("<missing>");
    };

    // Same ShareKey class (.row) on siblings must still get distinct numbers (cache hit path).
    EXPECT_EQ(text("a", scene), "1. ");
    EXPECT_EQ(text("b", scene), "2. ");

    // ol[start=5]: reset to 4, then increment → 5, 6.
    EXPECT_EQ(text("s1", scene), "5. ");
    EXPECT_EQ(text("s2", scene), "6. ");

    // li[value=9]: increment then set → 9; following item → 10.
    EXPECT_EQ(text("v1", scene), "1. ");
    EXPECT_EQ(text("v9", scene), "9. ");
    EXPECT_EQ(text("v10", scene), "10. ");

    // Nested ol resets; outer ul continues after the nested scope pops.
    EXPECT_EQ(text("i1", scene), "1. ");
    EXPECT_EQ(text("i2", scene), "2. ");
    EXPECT_EQ(text("n2", scene), "2. ");

    // Author content: counter() yields digits only; ". " comes from the string token.
    EXPECT_EQ(text("c1", scene), "1. ");
    EXPECT_EQ(text("c2", scene), "2. ");
}

// ::marker M2: synthesized decimal suffix is a trailing space; advance is font-independent.
TEST(DifftestSelfCheck, MarkerDecimalAdvanceDiff) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "marker-counters.html")) << error;
    const zui::html::Node* marker = marker_of(zui::html::find(scene.doc, "a"));
    ASSERT_NE(marker, nullptr);
    ASSERT_EQ(marker->text, "1. ");

    const float adv_marker = scene.metrics.measure(marker->text, 16.f, "sans-serif").width;
    const float adv_digits = scene.metrics.measure("1.", 16.f, "sans-serif").width;
    const float adv_space = scene.metrics.measure(" ", 16.f, "sans-serif").width;
    EXPECT_NEAR(adv_marker - adv_digits, adv_space, 0.02f)
        << "marker advance " << adv_marker << " digits " << adv_digits << " space " << adv_space;
}

// ::marker M2: list-style-type class changes drop/rebuild marker text; inserting or removing a
// sibling renumbers the following items (text change -> reattach).
TEST(DifftestSelfCheck, MarkerInvalidatesOnClassAndSiblingEdit) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "marker-counters.html")) << error;

    auto text = [](const char* id, Scene& s) -> std::string {
        const zui::html::Node* m = marker_of(zui::html::find(s.doc, id));
        return m != nullptr ? m->text : std::string("<missing>");
    };

    zui::html::Node* ol = zui::html::find(scene.doc, "o");
    ASSERT_NE(ol, nullptr);
    EXPECT_EQ(text("a", scene), "1. ");
    EXPECT_EQ(text("b", scene), "2. ");

    ASSERT_TRUE(zui::html::add_class(scene.doc, "a", "as-none"));
    scene.app_frame({ol});
    EXPECT_EQ(marker_of(zui::html::find(scene.doc, "a")), nullptr)
        << "list-style-type:none must drop the default marker";
    EXPECT_EQ(text("b", scene), "2. ") << "sibling counter must keep its ordinal";

    ASSERT_TRUE(zui::html::remove_class(scene.doc, "a", "as-none"));
    scene.app_frame({ol});
    EXPECT_EQ(text("a", scene), "1. ");
    EXPECT_EQ(text("b", scene), "2. ");

    auto created = std::make_unique<zui::html::Node>();
    created->tag = "li";
    created->id = "pre";
    created->class_name = "row";
    created->attrs["class"] = "row";
    created->attrs["id"] = "pre";
    created->parent = ol;
    zui::html::Node* pre = created.get();
    ol->children.insert(ol->children.begin(), std::move(created));
    scene.doc.ids["pre"] = pre;
    scene.app_frame({ol});
    EXPECT_EQ(text("pre", scene), "1. ");
    EXPECT_EQ(text("a", scene), "2. ");
    EXPECT_EQ(text("b", scene), "3. ");

    scene.layout->detach_node(pre);
    ASSERT_TRUE(zui::html::remove_tree(scene.doc, "pre"));
    scene.app_frame({ol});
    EXPECT_EQ(text("a", scene), "1. ");
    EXPECT_EQ(text("b", scene), "2. ");
}

// ol[reversed] counts down. start is the first ordinal; without it the first ordinal is the
// item count. li[value] sets that item and the next one continues down. list-style-image
// replaces the type glyph with the same replaced box content:url() uses.
TEST(DifftestSelfCheck, MarkerReversedAndImage) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "marker-reversed.html")) << error;

    auto text = [](const char* id, Scene& s) -> std::string {
        const zui::html::Node* m = marker_of(zui::html::find(s.doc, id));
        return m != nullptr ? m->text : std::string("<missing>");
    };
    EXPECT_EQ(text("r1", scene), "3. ");
    EXPECT_EQ(text("r2", scene), "2. ");
    EXPECT_EQ(text("r3", scene), "1. ");
    EXPECT_EQ(text("s1", scene), "8. ");
    EXPECT_EQ(text("s2", scene), "7. ");
    EXPECT_EQ(text("v1", scene), "3. ");
    EXPECT_EQ(text("v4", scene), "4. ");
    EXPECT_EQ(text("v3", scene), "3. ");

    const zui::html::Node* pic = zui::html::find(scene.doc, "pic");
    ASSERT_NE(pic, nullptr);
    const zui::html::Node* marker = marker_of(pic);
    ASSERT_NE(marker, nullptr);
    EXPECT_TRUE(marker->text.empty());
    ASSERT_NE(marker->attrs.find("src"), marker->attrs.end());
    EXPECT_NE(marker->attrs.at("src").find("green4.png"), std::string::npos);

    zui::layout::Box* host = scene.box("pic");
    ASSERT_NE(host, nullptr);
    zui::layout::Box* marker_box = nullptr;
    for (zui::layout::Box* child : host->children) {
        if (child != nullptr && child->generated == zui::html::PseudoElement::Marker) {
            marker_box = child;
            break;
        }
    }
    ASSERT_NE(marker_box, nullptr);
    EXPECT_TRUE(marker_box->is_replaced);
    EXPECT_GT(marker_box->geo.width, 1.f);
    EXPECT_GT(marker_box->geo.height, 1.f);
}
