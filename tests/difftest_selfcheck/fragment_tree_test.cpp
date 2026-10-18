// Fragment insert/remove and anonymous wrap reparenting.
// L6 architecture self-checks (TESTPLAN section 12).
// Shared scene harness: selfcheck_scene.h.

#include "selfcheck_scene.h"

using namespace difftest_selfcheck;

// LAYOUT.md section 6: a structural change drops the minimal enclosing ancestor's slots, and the
// boxes it cannot move keep their results. A fragment attached with no other edit has to be laid
// out - and the host has to miss, or the new subtree is never asked at all.
TEST(DifftestSelfCheck, FragmentInsertLaysOutOnlyTheNewSubtree) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error)) << error;
    zui::html::Node* body = zui::html::find_tag(scene.doc.root.get(), "body");
    ASSERT_NE(body, nullptr);

    const zui::layout::LayoutResult* first_before = Scene::result(scene.box("first"));
    const zui::layout::LayoutResult* tall_before = Scene::result(scene.box("tall"));
    const zui::layout::LayoutResult* host_before = Scene::result(scene.layout->principal_box(body));
    ASSERT_NE(first_before, nullptr);
    ASSERT_NE(tall_before, nullptr);
    ASSERT_NE(host_before, nullptr);

    ASSERT_TRUE(zui::html::attach_fragment(scene.doc, body, fixture("fragment.html"), error))
        << error;
    scene.app_frame({body});

    zui::layout::Box* note = scene.box("note");
    ASSERT_NE(note, nullptr) << "the fragment produced no box";
    const zui::layout::LayoutResult* note_result = Scene::result(note);
    ASSERT_NE(note_result, nullptr) << "the new fragment was never asked";
    EXPECT_GT(note_result->geometry.border_box_block, 0.0f)
        << "the new fragment has no geometry: the host hit and never asked it";
    // "The host was laid out again" is not a pointer comparison (see `published_records_current`):
    // the host's published result has to name the new subtree under its current key.
    EXPECT_TRUE(published_records_current(scene.layout->principal_box(body), note))
        << "the host answered from its cache, so the new subtree was never asked";
    EXPECT_EQ(Scene::result(scene.box("first")), first_before)
        << "a box the fragment cannot move was laid out again";
    EXPECT_EQ(Scene::result(scene.box("tall")), tall_before)
        << "a box the fragment cannot move was laid out again";
}

// LAYOUT.md section 6 / section 4: a fixed-position box is only asked by `OutOfFlowAsk::collect`,
// which runs inside the compute of the box that establishes its containing block. A structural
// change that misses the host has to reach that collector, or the mask has no geometry while
// every other box answers from the cache.
TEST(DifftestSelfCheck, FragmentWithFixedMaskIsCollected) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error)) << error;
    zui::html::Node* body = zui::html::find_tag(scene.doc.root.get(), "body");
    ASSERT_NE(body, nullptr);
    const zui::layout::LayoutResult* first_before = Scene::result(scene.box("first"));
    ASSERT_NE(first_before, nullptr);

    ASSERT_TRUE(zui::html::attach_fragment(scene.doc, body, fixture("fragment-mask.html"), error))
        << error;
    scene.app_frame({body});

    zui::layout::Box* mask = scene.box("mask");
    ASSERT_NE(mask, nullptr) << "the fragment produced no box";
    const zui::layout::LayoutResult* mask_result = Scene::result(mask);
    ASSERT_NE(mask_result, nullptr)
        << "the fixed mask was never asked: the collector did not run this frame";
    EXPECT_GT(mask_result->geometry.border_box_block, 0.0f) << "the fixed mask has no geometry";
    EXPECT_GT(mask_result->geometry.border_box_inline, 0.0f) << "the fixed mask has no geometry";
    EXPECT_EQ(Scene::result(scene.box("first")), first_before)
        << "a box the mask cannot move was laid out again";
}

// LAYOUT.md section 6 / section 7: removing a subtree unregisters it whole, and the boxes that
// survived keep their results. Nothing a slot or a pin still holds may name a box the tree has
// forgotten - the frame-end pin walk dereferences both.
TEST(DifftestSelfCheck, FragmentRemoveUnregistersTheWholeSubtree) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error)) << error;
    zui::html::Node* body = zui::html::find_tag(scene.doc.root.get(), "body");
    ASSERT_NE(body, nullptr);
    // Taken before the insert: the insert itself is what the neighbouring cases measure, and a
    // box the removal cannot move has to keep *this* result.
    const zui::layout::LayoutResult* first_before = Scene::result(scene.box("first"));
    ASSERT_NE(first_before, nullptr);
    ASSERT_TRUE(zui::html::attach_fragment(scene.doc, body, fixture("fragment.html"), error))
        << error;
    scene.app_frame({body});

    zui::html::Node* note_node = zui::html::find(scene.doc, "note");
    zui::layout::Box* note = scene.box("note");
    ASSERT_NE(note_node, nullptr);
    ASSERT_NE(note, nullptr);

    std::vector<zui::html::Node*> removed_nodes{note_node};
    for (const auto& child : note_node->children) {
        removed_nodes.push_back(child.get());
    }
    std::unordered_map<const zui::layout::Box*, bool> removed_boxes;
    collect_boxes(note, removed_boxes);

    scene.layout->detach_node(note_node);
    for (zui::html::Node* node : removed_nodes) {
        EXPECT_EQ(scene.layout->principal_box(node), nullptr)
            << "a removed node still has a box: only the node handed to detach was forgotten";
    }
    ASSERT_TRUE(zui::html::remove_tree(scene.doc, "note"));
    scene.layout->mark_needs_layout(body);
    scene.app_frame();

    EXPECT_EQ(Scene::result(scene.box("first")), first_before)
        << "removing the fragment laid out a box it cannot move";

    // The addresses are only compared, never dereferenced: a hit path that still names a removed
    // box is the bug, and the box itself may already be released.
    std::unordered_set<const zui::layout::Box*> gone;
    for (const auto& entry : removed_boxes) {
        gone.insert(entry.first);
    }
    struct Sweep {
        const std::unordered_set<const zui::layout::Box*>& gone;
        bool clean = true;
        void check(const zui::layout::LayoutResult& result) {
            for (const zui::layout::ChildRequest& request : result.used_requests) {
                if (request.child != nullptr && gone.count(request.child) != 0) {
                    clean = false;
                }
            }
            for (const zui::layout::ChildPlace& place : result.child_places) {
                if (place.box != nullptr && gone.count(place.box) != 0) {
                    clean = false;
                }
            }
        }
        void visit(const zui::layout::Box* box) {
            if (box == nullptr || !clean) {
                return;
            }
            if (box->cache != nullptr) {
                box->cache->for_results(
                    [this](const zui::layout::LayoutResult& result) { check(result); });
                if (const zui::layout::LayoutResult* pin = box->cache->pinned_final()) {
                    check(*pin);
                }
            }
            for (const zui::layout::Box* child : box->children) {
                visit(child);
            }
        }
    } sweep{gone};
    sweep.visit(scene.layout->root());
    EXPECT_TRUE(sweep.clean)
        << "a slot or a pin still names a box the tree removed; the frame-end pin walk would "
           "dereference it";

    // The release happens after the layout wrote its new pins, so a frame that follows must not
    // touch what was released.
    scene.layout->release_detached_boxes();
    scene.app_frame();
    EXPECT_EQ(Scene::result(scene.box("first")), first_before)
        << "the frame after the release laid out a box it cannot move";
}

// Gap-doc §C P0 / box_tree reparent: mixed inline+block builds an anonymous wrapper around
// #keep; toggling #keep to display:block forgets that wrapper. `reparent_box` must unlink #keep
// from the old anon and set parent to the host before `forget_subtree_boxes`, or the kept box is
// both live and queued in detached_ (UAF on release).
TEST(DifftestSelfCheck, AnonWrapReparentKeepsParentInvariant) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "box-reparent.html")) << error;

    zui::html::Node* host_node = zui::html::find(scene.doc, "host");
    zui::layout::Box* host = scene.box("host");
    zui::layout::Box* keep = scene.box("keep");
    zui::layout::Box* block = scene.box("block");
    ASSERT_NE(host_node, nullptr);
    ASSERT_NE(host, nullptr);
    ASSERT_NE(keep, nullptr);
    ASSERT_NE(block, nullptr);
    ASSERT_NE(keep->parent, nullptr);
    EXPECT_EQ(keep->parent->role, zui::layout::BoxRole::Anonymous)
        << "mixed inline+block did not wrap #keep in an anonymous block";
    EXPECT_EQ(keep->parent->parent, host);
    zui::layout::Box* anon_before = keep->parent;

    auto assert_parent_children = [](const zui::layout::Box* root) {
        struct Walk {
            bool ok = true;
            std::unordered_set<const zui::layout::Box*> seen;
            void visit(const zui::layout::Box* box) {
                if (box == nullptr || !ok) {
                    return;
                }
                if (!seen.insert(box).second) {
                    ok = false;
                    return;
                }
                for (const zui::layout::Box* child : box->children) {
                    if (child == nullptr || child->parent != box) {
                        ok = false;
                        return;
                    }
                    visit(child);
                }
            }
        } walk;
        walk.visit(root);
        return walk.ok;
    };
    EXPECT_TRUE(assert_parent_children(scene.layout->root()))
        << "parent/children invariant already broken before the rebuild";

    ASSERT_TRUE(zui::html::add_class(scene.doc, "keep", "as-block"));
    scene.app_frame({host_node});

    zui::layout::Box* keep_after = scene.box("keep");
    ASSERT_NE(keep_after, nullptr);
    EXPECT_EQ(keep_after, keep) << "display change replaced the kept box instead of reparenting it";
    EXPECT_EQ(keep_after->parent, host)
        << "kept box was not reparented onto the host after the anonymous wrapper went away";
    EXPECT_TRUE(std::find(host->children.begin(), host->children.end(), keep_after) !=
                host->children.end())
        << "host children do not list the kept box";
    EXPECT_TRUE(assert_parent_children(scene.layout->root()))
        << "parent/children invariant broken after anonymous-wrapper rebuild";

    // Old anon must not still claim the kept box; releasing detached must not free a live box.
    if (anon_before != nullptr) {
        EXPECT_TRUE(std::find(anon_before->children.begin(), anon_before->children.end(), keep) ==
                    anon_before->children.end())
            << "old anonymous parent still lists the kept box after reparent";
    }
    scene.layout->release_detached_boxes();
    EXPECT_EQ(scene.box("keep"), keep) << "release_detached freed a box that is still live";
    EXPECT_EQ(scene.box("block"), block);
    EXPECT_TRUE(assert_parent_children(scene.layout->root()))
        << "parent/children invariant broken after release_detached";
}
