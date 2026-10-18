// Focus, editing, select popups, tab order, hover, and window close.
// L6 architecture self-checks (TESTPLAN section 12).
// Shared scene harness: selfcheck_scene.h.

#include "selfcheck_scene.h"

#include <filesystem>
#include <fstream>
#include <iostream>

using namespace difftest_selfcheck;

// HOST_ARCHITECTURE PR-15: <select> opens a fixed option list; choosing commits value + input/change;
// Escape / outside click dismisses without change.
TEST(DifftestSelfCheck, SelectPopupDefaultBehavior) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "select-popup.html")) << error;

    zui::html::Node* group = zui::html::find(scene.doc, "group");
    ASSERT_NE(group, nullptr);
    EXPECT_EQ(group->value, "family");
    EXPECT_EQ(group->text, "家人");
    EXPECT_FALSE(zui::html::select_is_open(group));

    struct Call {
        std::string type;
        std::string name;
        std::string source;
        std::string value;
    };
    std::vector<Call> calls;
    zui::html::Node* hovered = nullptr;
    zui::html::Node* focused = nullptr;
    zui::html::Node* scroll_drag = nullptr;
    zui::input::InteractionHost host;
    host.document = &scene.doc;
    host.layout = scene.layout.get();
    host.metrics = &scene.metrics;
    host.hovered = &hovered;
    host.focused = &focused;
    host.scroll_drag = &scroll_drag;
    host.restyle = [&scene] { scene.frame(); };
    host.mark_geometry = [&scene] { scene.frame(); };
    host.dispatch = [&calls](zui::Event& e) {
        Call call;
        call.type = e.type();
        call.name = e.name();
        call.source = e.source();
        call.value = e.value();
        calls.push_back(std::move(call));
    };
    zui::input::Interaction interaction(host);

    const auto press_id = [&](const char* id, float dx, float dy) {
        zui::layout::Box* box = scene.box(id);
        ASSERT_NE(box, nullptr) << id;
        interaction.pointer_button("mousePressed", box->geo.world_x + dx, box->geo.world_y + dy,
                                   "left", 1);
        interaction.pointer_button("mouseReleased", box->geo.world_x + dx, box->geo.world_y + dy,
                                   "left", 1);
        scene.app_frame({group});
    };

    // The control's closed height, so the dismissal check below can prove the option boxes left
    // the flow instead of staying inside the control (they grew it to the whole list before the
    // fix: the contacts dialog's group field after the popup was dismissed).
    const float closed_height = scene.box("group")->geo.height;

    press_id("group", 8.0f, 8.0f);
    EXPECT_TRUE(zui::html::select_is_open(group));
    EXPECT_EQ(group->value, "family");

    // The open list is a contiguous menu: every row is as tall as the control and rows touch.
    {
        std::vector<zui::layout::Box*> rows;
        for (auto& ch : group->children) {
            if (ch && ch->tag == "option") {
                zui::layout::Box* box = scene.layout->principal_box(ch.get());
                ASSERT_NE(box, nullptr) << "an open option must have a box";
                rows.push_back(box);
            }
        }
        ASSERT_EQ(rows.size(), std::size_t{3});
        const float row_h = scene.box("group")->geo.height;
        for (std::size_t i = 0; i < rows.size(); ++i) {
            EXPECT_NEAR(rows[i]->geo.height, row_h, 0.02f) << "row " << i << " height";
            const float expected_top =
                rows.front()->geo.world_y + row_h * static_cast<float>(i);
            EXPECT_NEAR(rows[i]->geo.world_y, expected_top, 0.02f) << "row " << i << " top";
        }
    }

    // Hit the first option (work) via its laid-out box.
    zui::html::Node* work = nullptr;
    for (auto& ch : group->children) {
        if (ch && ch->tag == "option" && zui::html::option_value(ch.get()) == "work") {
            work = ch.get();
            break;
        }
    }
    ASSERT_NE(work, nullptr);
    zui::layout::Box* work_box = scene.layout->principal_box(work);
    ASSERT_NE(work_box, nullptr) << "open options must create boxes";
    calls.clear();
    interaction.pointer_button("mousePressed", work_box->geo.world_x + 4.0f,
                               work_box->geo.world_y + 4.0f, "left", 1);
    interaction.pointer_button("mouseReleased", work_box->geo.world_x + 4.0f,
                               work_box->geo.world_y + 4.0f, "left", 1);
    scene.app_frame({group});
    EXPECT_FALSE(zui::html::select_is_open(group));
    EXPECT_EQ(group->value, "work");
    EXPECT_EQ(group->text, "同事");
    ASSERT_GE(calls.size(), std::size_t{2});
    EXPECT_EQ(calls[calls.size() - 2].type, "input");
    EXPECT_EQ(calls[calls.size() - 1].type, "change");
    EXPECT_EQ(calls.back().value, "work");

    // Re-open, Escape dismisses without changing the value.
    press_id("group", 8.0f, 8.0f);
    EXPECT_TRUE(zui::html::select_is_open(group));
    EXPECT_TRUE(interaction.key("keyDown", "Escape"));
    scene.app_frame({group});
    EXPECT_FALSE(zui::html::select_is_open(group));
    EXPECT_EQ(group->value, "work");

    // Re-open, outside click dismisses without change.
    press_id("group", 8.0f, 8.0f);
    EXPECT_TRUE(zui::html::select_is_open(group));
    press_id("outside", 4.0f, 4.0f);
    EXPECT_FALSE(zui::html::select_is_open(group));
    EXPECT_EQ(group->value, "work");
    // A closed select keeps no option box, and the control is back to its closed height.
    for (auto& ch : group->children) {
        if (ch && ch->tag == "option") {
            EXPECT_EQ(scene.layout->principal_box(ch.get()), nullptr)
                << "a closed select's option must not have a box";
        }
    }
    EXPECT_NEAR(scene.box("group")->geo.height, closed_height, 0.02f);

    // A dialog's overflow:auto must not swallow the menu. The last option sits below #clip.
    zui::html::Node* clipped = zui::html::find(scene.doc, "clipped");
    ASSERT_NE(clipped, nullptr);
    press_id("clipped", 8.0f, 8.0f);
    scene.app_frame({clipped});
    EXPECT_TRUE(zui::html::select_is_open(clipped));
    zui::html::Node* last = nullptr;
    for (auto& ch : clipped->children) {
        if (ch && ch->tag == "option" && zui::html::option_value(ch.get()) == "c") {
            last = ch.get();
            break;
        }
    }
    ASSERT_NE(last, nullptr);
    zui::layout::Box* last_box = scene.layout->principal_box(last);
    zui::layout::Box* clip_box = scene.box("clip");
    ASSERT_NE(last_box, nullptr);
    ASSERT_NE(clip_box, nullptr);
    EXPECT_GT(last_box->geo.world_y, clip_box->geo.world_y + clip_box->geo.height);
    interaction.pointer_button("mousePressed", last_box->geo.world_x + 4.0f,
                               last_box->geo.world_y + 4.0f, "left", 1);
    interaction.pointer_button("mouseReleased", last_box->geo.world_x + 4.0f,
                               last_box->geo.world_y + 4.0f, "left", 1);
    scene.app_frame({clipped});
    EXPECT_EQ(clipped->value, "c");
    EXPECT_EQ(clipped->text, "丙");
}

// EVENT.md sections 6 and 7: a keydown handler runs before the engine's default action for the
// key, the payload is the KeyboardEvent-style Key struct, and Enter inside a <textarea> inserts
// the same line break Chrome inserts - unless the handler cancelled it.
TEST(DifftestSelfCheck, KeydownDispatchAndTextareaEnter) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "events.html")) << error;
    zui::html::Node* draft = zui::html::find(scene.doc, "draft");
    zui::html::Node* line = zui::html::find(scene.doc, "line");
    ASSERT_NE(draft, nullptr);
    ASSERT_NE(line, nullptr);

    struct Firing {
        std::string type;
        std::string name;
        std::string source;
        std::string value;
        std::string scope;
        std::string key;
        bool ctrl = false;
        bool shift = false;
        bool composing = false;
        bool has_key = false;
        bool prevented = false;
    };
    std::vector<Firing> firings;
    bool cancel_enter = false;
    zui::html::Node* hovered = nullptr;
    zui::html::Node* focused = nullptr;
    zui::html::Node* scroll_drag = nullptr;
    zui::input::InteractionHost host;
    host.document = &scene.doc;
    host.layout = scene.layout.get();
    host.metrics = &scene.metrics;
    host.hovered = &hovered;
    host.focused = &focused;
    host.scroll_drag = &scroll_drag;
    host.dispatch = [&](zui::Event& e) {
        Firing firing;
        firing.type = e.type();
        firing.name = e.name();
        firing.source = e.source();
        firing.value = e.value();
        firing.scope = e.data("scope");
        if (const zui::Key* key = e.is_key()) {
            firing.has_key = true;
            firing.key = key->key;
            firing.ctrl = key->ctrl;
            firing.shift = key->shift;
            firing.composing = key->composing;
        }
        if (cancel_enter && firing.key == "Enter") {
            e.prevent_default();
        }
        firing.prevented = e.default_prevented();
        firings.push_back(std::move(firing));
    };
    zui::input::Interaction interaction(host);

    // Nothing is focused, so the walk starts at <body>: a global shortcut. The payload is the
    // key that fired, and a keydown firing carries no value, so a handler cannot read the
    // focused control's old text by accident (EVENT.md section 6.1).
    firings.clear();
    EXPECT_FALSE(interaction.key("keyDown", "s", zui::Key{"", true}));
    ASSERT_EQ(firings.size(), std::size_t{1});
    EXPECT_EQ(firings[0].type, "keydown");
    EXPECT_EQ(firings[0].name, "on_global");
    EXPECT_EQ(firings[0].source, "") << "<body> carries no id";
    EXPECT_EQ(firings[0].scope, "window") << "data() reads the body's own data-*";
    ASSERT_TRUE(firings[0].has_key);
    EXPECT_EQ(firings[0].key, "s");
    EXPECT_TRUE(firings[0].ctrl);
    EXPECT_EQ(firings[0].value, "");

    // The focused element owns the firing; the body's handler does not also run.
    focused = draft;
    firings.clear();
    EXPECT_FALSE(interaction.key("keyDown", "Enter"));
    ASSERT_EQ(firings.size(), std::size_t{1});
    EXPECT_EQ(firings[0].name, "on_draft");
    EXPECT_EQ(firings[0].source, "draft");
    ASSERT_TRUE(firings[0].has_key);
    EXPECT_EQ(firings[0].key, "Enter");
    EXPECT_FALSE(firings[0].ctrl);
    EXPECT_EQ(firings[0].value, "") << "still no value, though the textarea holds text";

    // prevent_default() is reported back to the caller, which is what keeps the newline out.
    cancel_enter = true;
    firings.clear();
    EXPECT_TRUE(interaction.key("keyDown", "Enter"));
    ASSERT_EQ(firings.size(), std::size_t{1});
    EXPECT_TRUE(firings[0].prevented);
    cancel_enter = false;

    // The <textarea> default: insert at the caret. A single-line <input> has no default action,
    // which is the declared deviation from implicit form submission (EVENT.md section 10).
    draft->value = "ab";
    draft->caret = 1;
    EXPECT_TRUE(zui::input::insert_line_break(*draft));
    EXPECT_EQ(draft->value, "a\nb");
    EXPECT_EQ(draft->text, "a\nb");
    EXPECT_EQ(draft->caret, 2);
    line->value = "ab";
    EXPECT_FALSE(zui::input::insert_line_break(*line));
    EXPECT_EQ(line->value, "ab");
    draft->caret = 99;
    EXPECT_TRUE(zui::input::insert_line_break(*draft));
    EXPECT_EQ(draft->value, "a\nb\n") << "the caret is clamped to the end of the text";
}

// HOST_ARCHITECTURE PR-5b: editable selection via Document::selection (drag, double-click word,
// Ctrl+A, Copy/Cut range-or-whole, Paste replaces range-or-whole). Clamp stays on text.size().
TEST(DifftestSelfCheck, EditableSelectionViaDocumentSelection) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "events.html")) << error;
    zui::html::Node* draft = zui::html::find(scene.doc, "draft");
    ASSERT_NE(draft, nullptr);
    draft->value = "hello world";
    draft->text = draft->value;
    draft->caret = static_cast<int>(draft->value.size());
    scene.layout->sync_text(draft);
    scene.layout->mark_needs_layout(draft);
    scene.frame();

    zui::layout::Box* box = scene.box("draft");
    ASSERT_NE(box, nullptr);
    ASSERT_NE(box->style_ptr, nullptr);

    std::string clipboard;
    zui::html::Node* hovered = nullptr;
    zui::html::Node* focused = nullptr;
    zui::html::Node* scroll_drag = nullptr;
    zui::input::InteractionHost host;
    host.document = &scene.doc;
    host.layout = scene.layout.get();
    host.metrics = &scene.metrics;
    host.hovered = &hovered;
    host.focused = &focused;
    host.scroll_drag = &scroll_drag;
    host.set_clipboard_text = [&](std::string_view text) {
        clipboard.assign(text);
        return true;
    };
    host.clipboard_text = [&] { return zui::ClipboardText{zui::ClipboardStatus::Ok, clipboard}; };
    zui::input::Interaction interaction(host);

    const float content_x = box->geo.world_x + box->geo.border.left + box->geo.padding.left;
    const float content_y = box->geo.world_y + box->geo.border.top + box->geo.padding.top + 8.f;
    auto x_at = [&](std::size_t index) {
        const float w = scene.metrics
                            .measure(std::string_view(draft->value).substr(0, index),
                                     box->style().font_size, box->style().font_family)
                            .width;
        return content_x + w;
    };

    // Single click: collapsed selection at caret; caret == focus.
    interaction.pointer_button("mousePressed", x_at(1), content_y, "left", 1);
    interaction.pointer_button("mouseReleased", x_at(1), content_y, "left", 1);
    ASSERT_EQ(focused, draft);
    EXPECT_EQ(draft->caret, 1);
    EXPECT_EQ(scene.doc.selection.node, draft);
    EXPECT_EQ(scene.doc.selection.start, std::size_t{1});
    EXPECT_EQ(scene.doc.selection.end, std::size_t{1});
    EXPECT_TRUE(zui::html::selection_empty(scene.doc.selection));

    // Drag select [1,5) ("ello"); ordered projection; caret stays at focus end.
    interaction.pointer_button("mousePressed", x_at(1), content_y, "left", 1);
    interaction.pointer_button("mouseMoved", x_at(5), content_y, "left", 1);
    interaction.pointer_button("mouseReleased", x_at(5), content_y, "left", 1);
    EXPECT_EQ(scene.doc.selection.node, draft);
    EXPECT_EQ(scene.doc.selection.start, std::size_t{1});
    EXPECT_EQ(scene.doc.selection.end, std::size_t{5});
    EXPECT_EQ(draft->caret, 5);
    EXPECT_EQ(draft->text, draft->value);

    // Copy reads the selected substring. With an empty range there is nothing to take: the
    // clipboard keeps what it held, as Chrome leaves it.
    clipboard.clear();
    EXPECT_FALSE(interaction.key("keyDown", "c", zui::Key{"", true}));
    EXPECT_EQ(clipboard, "ello");
    zui::html::set_selection(scene.doc, draft, 3, 3);
    draft->caret = 3;
    clipboard = "keep";
    EXPECT_FALSE(interaction.key("keyDown", "c", zui::Key{"", true}));
    EXPECT_EQ(clipboard, "keep");

    // Restore a range, Cut deletes it and collapses caret to the start.
    draft->text = draft->value;
    zui::html::set_selection(scene.doc, draft, 1, 5);
    draft->caret = 5;
    clipboard.clear();
    EXPECT_FALSE(interaction.key("keyDown", "x", zui::Key{"", true}));
    EXPECT_EQ(clipboard, "ello");
    EXPECT_EQ(draft->value, "h world");
    EXPECT_EQ(draft->text, draft->value);
    EXPECT_EQ(draft->caret, 1);
    EXPECT_EQ(scene.doc.selection.start, std::size_t{1});
    EXPECT_EQ(scene.doc.selection.end, std::size_t{1});

    // An empty-range Cut takes nothing: the value and the clipboard stay put.
    clipboard = "keep";
    EXPECT_FALSE(interaction.key("keyDown", "x", zui::Key{"", true}));
    EXPECT_EQ(clipboard, "keep");
    EXPECT_EQ(draft->value, "h world");

    // Paste with a range replaces that range; an empty range inserts at the caret, keeping
    // the text on both sides of it.
    draft->value = "hello world";
    draft->text = draft->value;
    zui::html::set_selection(scene.doc, draft, 6, 11);
    draft->caret = 11;
    clipboard = "zui";
    EXPECT_FALSE(interaction.key("keyDown", "v", zui::Key{"", true}));
    EXPECT_EQ(draft->value, "hello zui");
    EXPECT_EQ(draft->caret, 9);
    EXPECT_EQ(scene.doc.selection.start, std::size_t{9});
    EXPECT_EQ(scene.doc.selection.end, std::size_t{9});

    draft->value = "hello world";
    draft->text = draft->value;
    zui::html::set_selection(scene.doc, draft, 5, 5);
    draft->caret = 5;
    clipboard = "ALL";
    EXPECT_FALSE(interaction.key("keyDown", "v", zui::Key{"", true}));
    EXPECT_EQ(draft->value, "helloALL world");
    EXPECT_EQ(draft->text, draft->value);
    EXPECT_EQ(draft->caret, 8);
    EXPECT_EQ(scene.doc.selection.start, std::size_t{8});
    EXPECT_EQ(scene.doc.selection.end, std::size_t{8});

    // Ctrl+A selects [0, text.size()); caret at focus end.
    draft->value = "hello world";
    draft->text = draft->value;
    EXPECT_FALSE(interaction.key("keyDown", "a", zui::Key{"", true}));
    EXPECT_EQ(scene.doc.selection.node, draft);
    EXPECT_EQ(scene.doc.selection.start, std::size_t{0});
    EXPECT_EQ(scene.doc.selection.end, draft->text.size());
    EXPECT_EQ(draft->caret, static_cast<int>(draft->text.size()));

    // Double-click selects the ASCII word under the caret.
    draft->value = "hello world";
    draft->text = draft->value;
    scene.layout->sync_text(draft);
    scene.layout->mark_needs_layout(draft);
    scene.frame();
    box = scene.box("draft");
    ASSERT_NE(box, nullptr);
    interaction.pointer_button("mousePressed", x_at(1), content_y, "left", 2);
    interaction.pointer_button("mouseReleased", x_at(1), content_y, "left", 2);
    EXPECT_EQ(scene.doc.selection.start, std::size_t{0});
    EXPECT_EQ(scene.doc.selection.end, std::size_t{5});
    EXPECT_EQ(draft->caret, 5);

    // set_selection still clamps only by text.size() even when value is shorter (no bifurcate).
    draft->value = "hi";
    draft->text = "hello";
    zui::html::set_selection(scene.doc, draft, 0, 99);
    EXPECT_EQ(scene.doc.selection.end, std::size_t{5}) << "clamp uses text.size(), not value";
    draft->text = draft->value;
    zui::html::set_selection(scene.doc, draft, 0, 99);
    EXPECT_EQ(scene.doc.selection.end, std::size_t{2});

    // set(value) keeps a caret that sits in the shared prefix. insert writes at that caret.
    draft->value = "hello";
    draft->text = draft->value;
    draft->caret = 2;
    zui::html::set_selection(scene.doc, draft, 2, 2);
    ASSERT_TRUE(zui::html::set_field(scene.doc, "draft", "value", "hello!"));
    EXPECT_EQ(draft->value, "hello!");
    EXPECT_EQ(draft->caret, 2);
    EXPECT_EQ(scene.doc.selection.start, std::size_t{2});
    ASSERT_TRUE(zui::html::set_field(scene.doc, "draft", "insert", "X"));
    EXPECT_EQ(draft->value, "heXllo!");
    EXPECT_EQ(draft->caret, 3);
    ASSERT_TRUE(zui::html::set_field(scene.doc, "draft", "select", "1,4"));
    EXPECT_EQ(scene.doc.selection.start, std::size_t{1});
    EXPECT_EQ(scene.doc.selection.end, std::size_t{4});
    EXPECT_EQ(draft->caret, 4);
}

// HOST_ARCHITECTURE PR-10: focus/blur fire on pointer, Tab, and Interaction::focus/blur;
// value() is empty; IME enable tracks editable focus.
TEST(DifftestSelfCheck, FocusBlurEventsAndApi) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "events.html")) << error;
    zui::html::Node* draft = zui::html::find(scene.doc, "draft");
    zui::html::Node* line = zui::html::find(scene.doc, "line");
    zui::html::Node* pick = zui::html::find(scene.doc, "pick");
    ASSERT_NE(draft, nullptr);
    ASSERT_NE(line, nullptr);
    ASSERT_NE(pick, nullptr);
    draft->attrs["onfocus"] = "on_draft_focus";
    draft->attrs["onblur"] = "on_draft_blur";
    line->attrs["onfocus"] = "on_line_focus";
    line->attrs["onblur"] = "on_line_blur";
    pick->attrs["onfocus"] = "on_pick_focus";
    pick->attrs["onblur"] = "on_pick_blur";

    struct Call {
        std::string type;
        std::string name;
        std::string source;
        std::string value;
    };
    std::vector<Call> calls;
    bool ime_enabled = false;
    int ime_rect_calls = 0;
    zui::html::Node* hovered = nullptr;
    zui::html::Node* focused = nullptr;
    zui::html::Node* scroll_drag = nullptr;
    zui::input::InteractionHost host;
    host.document = &scene.doc;
    host.layout = scene.layout.get();
    host.metrics = &scene.metrics;
    host.hovered = &hovered;
    host.focused = &focused;
    host.scroll_drag = &scroll_drag;
    host.set_ime = [&](bool enabled) { ime_enabled = enabled; };
    host.set_ime_rect = [&](float, float, float, float) { ++ime_rect_calls; };
    host.dispatch = [&](zui::Event& e) {
        calls.push_back(Call{std::string(e.type()), std::string(e.name()), std::string(e.source()),
                             std::string(e.value())});
    };
    zui::input::Interaction interaction(host);

    auto press = [&](const char* id) {
        zui::layout::Box* box = scene.box(id);
        ASSERT_NE(box, nullptr);
        const float x = box->geo.world_x + box->geo.width * 0.5f;
        const float y = box->geo.world_y + box->geo.height * 0.5f;
        interaction.pointer_button("mousePressed", x, y, "left", 1);
        interaction.pointer_button("mouseReleased", x, y, "left", 1);
    };

    press("draft");
    ASSERT_EQ(focused, draft);
    EXPECT_TRUE(draft->focused);
    EXPECT_TRUE(ime_enabled);
    EXPECT_GE(ime_rect_calls, 1);
    ASSERT_FALSE(calls.empty());
    EXPECT_EQ(calls.back().type, "focus");
    EXPECT_EQ(calls.back().name, "on_draft_focus");
    EXPECT_EQ(calls.back().source, "draft");
    EXPECT_EQ(calls.back().value, "");

    calls.clear();
    ime_rect_calls = 0;
    press("line");
    ASSERT_EQ(focused, line);
    ASSERT_EQ(calls.size(), std::size_t{2});
    EXPECT_EQ(calls[0].type, "blur");
    EXPECT_EQ(calls[0].name, "on_draft_blur");
    EXPECT_EQ(calls[0].source, "draft");
    EXPECT_EQ(calls[1].type, "focus");
    EXPECT_EQ(calls[1].name, "on_line_focus");
    EXPECT_EQ(calls[1].source, "line");
    EXPECT_TRUE(ime_enabled);

    calls.clear();
    EXPECT_FALSE(interaction.key("keyDown", "Tab"));
    ASSERT_EQ(focused, pick) << "Tab order follows document focusables";
    ASSERT_GE(calls.size(), std::size_t{3});
    EXPECT_EQ(calls[0].type, "keydown");
    EXPECT_EQ(calls[calls.size() - 2].type, "blur");
    EXPECT_EQ(calls[calls.size() - 2].source, "line");
    EXPECT_EQ(calls.back().type, "focus");
    EXPECT_EQ(calls.back().source, "pick");
    EXPECT_TRUE(pick->focus_visible);
    EXPECT_FALSE(ime_enabled) << "button focus disables IME";

    calls.clear();
    ASSERT_TRUE(interaction.focus(draft, true));
    ASSERT_EQ(focused, draft);
    ASSERT_EQ(calls.size(), std::size_t{2});
    EXPECT_EQ(calls[0].type, "blur");
    EXPECT_EQ(calls[0].source, "pick");
    EXPECT_EQ(calls[1].type, "focus");
    EXPECT_EQ(calls[1].source, "draft");
    EXPECT_TRUE(ime_enabled);

    calls.clear();
    ASSERT_TRUE(interaction.blur());
    EXPECT_EQ(focused, nullptr);
    ASSERT_EQ(calls.size(), std::size_t{1});
    EXPECT_EQ(calls[0].type, "blur");
    EXPECT_EQ(calls[0].source, "draft");
    EXPECT_FALSE(ime_enabled);
    EXPECT_FALSE(interaction.blur());
}

// Editable change-on-blur: dirty since focus → change before blur; checkbox untouched.
// HTML keeps a text control's `selectionStart`/`selectionEnd` across a focus move, so a toolbar
// button - or a command the palette opens after two focus hops - still reads the range. A
// subtree that goes away takes its selection with it.
TEST(DifftestSelfCheck, EditableSelectionSurvivesTheFocusMoves) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "events.html")) << error;
    zui::html::Node* draft = zui::html::find(scene.doc, "draft");
    zui::html::Node* pick = zui::html::find(scene.doc, "pick");
    zui::html::Node* line = zui::html::find(scene.doc, "line");
    ASSERT_NE(draft, nullptr);
    ASSERT_NE(pick, nullptr);
    ASSERT_NE(line, nullptr);

    zui::html::Node* hovered = nullptr;
    zui::html::Node* focused = nullptr;
    zui::html::Node* scroll_drag = nullptr;
    zui::input::InteractionHost host;
    host.document = &scene.doc;
    host.layout = scene.layout.get();
    host.metrics = &scene.metrics;
    host.hovered = &hovered;
    host.focused = &focused;
    host.scroll_drag = &scroll_drag;
    zui::input::Interaction interaction(host);

    ASSERT_TRUE(interaction.focus(draft, true));
    draft->value = "hello world";
    draft->text = draft->value;
    zui::html::set_selection(scene.doc, draft, 6, 11);
    // Two hops, the way the palette is reached: the status button, then its field.
    ASSERT_TRUE(interaction.focus(pick, false));
    EXPECT_EQ(scene.doc.selection.node, draft) << "a focus move dropped the selection";
    EXPECT_EQ(scene.doc.selection.start, std::size_t{6});
    EXPECT_EQ(scene.doc.selection.end, std::size_t{11});
    ASSERT_TRUE(interaction.focus(line, true));
    EXPECT_EQ(scene.doc.selection.node, draft);
    EXPECT_EQ(scene.doc.selection.start, std::size_t{6});

    interaction.forget_subtree(draft);
    EXPECT_EQ(scene.doc.selection.node, nullptr) << "the dead node kept the selection";
}

TEST(DifftestSelfCheck, EditableChangeOnBlur) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "events.html")) << error;
    zui::html::Node* draft = zui::html::find(scene.doc, "draft");
    zui::html::Node* line = zui::html::find(scene.doc, "line");
    zui::html::Node* flag = zui::html::find(scene.doc, "flag");
    ASSERT_NE(draft, nullptr);
    ASSERT_NE(line, nullptr);
    ASSERT_NE(flag, nullptr);
    draft->attrs["onchange"] = "on_draft_change";
    draft->attrs["oninput"] = "on_draft_input";
    draft->attrs["onblur"] = "on_draft_blur";
    line->attrs["onchange"] = "on_line_change";
    line->attrs["onblur"] = "on_line_blur";
    flag->attrs["onchange"] = "on_flag_change";

    struct Call {
        std::string type;
        std::string name;
        std::string source;
        std::string value;
    };
    std::vector<Call> calls;
    zui::html::Node* hovered = nullptr;
    zui::html::Node* focused = nullptr;
    zui::html::Node* scroll_drag = nullptr;
    zui::input::InteractionHost host;
    host.document = &scene.doc;
    host.layout = scene.layout.get();
    host.metrics = &scene.metrics;
    host.hovered = &hovered;
    host.focused = &focused;
    host.scroll_drag = &scroll_drag;
    host.dispatch = [&](zui::Event& e) {
        calls.push_back(Call{std::string(e.type()), std::string(e.name()), std::string(e.source()),
                             std::string(e.value())});
    };
    zui::input::Interaction interaction(host);

    ASSERT_TRUE(interaction.focus(draft, true));
    calls.clear();
    draft->value = "changed";
    draft->text = draft->value;
    (void)interaction.fire(draft, "input");
    ASSERT_TRUE(interaction.focus(line, true));
    ASSERT_GE(calls.size(), std::size_t{3});
    EXPECT_EQ(calls[0].type, "input");
    EXPECT_EQ(calls[0].source, "draft");
    EXPECT_EQ(calls[1].type, "change");
    EXPECT_EQ(calls[1].name, "on_draft_change");
    EXPECT_EQ(calls[1].source, "draft");
    EXPECT_EQ(calls[1].value, "changed");
    EXPECT_EQ(calls[2].type, "blur");
    EXPECT_EQ(calls[2].source, "draft");

    // No edit → no change on blur.
    calls.clear();
    ASSERT_TRUE(interaction.blur());
    ASSERT_EQ(calls.size(), std::size_t{1});
    EXPECT_EQ(calls[0].type, "blur");
    EXPECT_EQ(calls[0].source, "line");

    // Checkbox still fires change on click only (not via editable dirty path).
    calls.clear();
    zui::layout::Box* flag_box = scene.box("flag");
    ASSERT_NE(flag_box, nullptr);
    const float fx = flag_box->geo.world_x + flag_box->geo.width * 0.5f;
    const float fy = flag_box->geo.world_y + flag_box->geo.height * 0.5f;
    interaction.pointer_button("mousePressed", fx, fy, "left", 1);
    interaction.pointer_button("mouseReleased", fx, fy, "left", 1);
    bool saw_flag_change = false;
    for (const Call& c : calls) {
        if (c.type == "change" && c.source == "flag") {
            saw_flag_change = true;
        }
    }
    EXPECT_TRUE(saw_flag_change);
}

// HOST §4.4: positive tabindex ascending, then 0/natural; tabindex=-1 skipped.
TEST(DifftestSelfCheck, TabIndexOrder) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "tabindex.html")) << error;
    zui::html::Node* b0 = zui::html::find(scene.doc, "b0");
    zui::html::Node* b1 = zui::html::find(scene.doc, "b1");
    zui::html::Node* b2 = zui::html::find(scene.doc, "b2");
    zui::html::Node* bneg = zui::html::find(scene.doc, "bneg");
    zui::html::Node* d0 = zui::html::find(scene.doc, "d0");
    ASSERT_NE(b0, nullptr);
    ASSERT_NE(b1, nullptr);
    ASSERT_NE(b2, nullptr);
    ASSERT_NE(bneg, nullptr);
    ASSERT_NE(d0, nullptr);

    zui::html::Node* hovered = nullptr;
    zui::html::Node* focused = nullptr;
    zui::html::Node* scroll_drag = nullptr;
    zui::input::InteractionHost host;
    host.document = &scene.doc;
    host.layout = scene.layout.get();
    host.metrics = &scene.metrics;
    host.hovered = &hovered;
    host.focused = &focused;
    host.scroll_drag = &scroll_drag;
    host.dispatch = [](zui::Event&) {};
    zui::input::Interaction interaction(host);

    ASSERT_TRUE(interaction.focus(b1, true));
    EXPECT_FALSE(interaction.key("keyDown", "Tab"));
    EXPECT_EQ(focused, b2);
    EXPECT_FALSE(interaction.key("keyDown", "Tab"));
    EXPECT_EQ(focused, b0);
    EXPECT_FALSE(interaction.key("keyDown", "Tab"));
    EXPECT_EQ(focused, d0);
    EXPECT_FALSE(interaction.key("keyDown", "Tab"));
    EXPECT_EQ(focused, b1) << "wrap; tabindex=-1 never entered";

    // Programmatic focus still accepts tabindex=-1.
    ASSERT_TRUE(interaction.focus(bneg, true));
    EXPECT_EQ(focused, bneg);
}

// `<input type=file>` sugar: pick_open host callback → value + input/change.
TEST(DifftestSelfCheck, FileInputPickOpenSugar) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "events.html")) << error;
    zui::html::Node* line = zui::html::find(scene.doc, "line");
    ASSERT_NE(line, nullptr);
    line->attrs["type"] = "file";
    line->attrs["accept"] = ".txt,.md,text/plain";
    line->attrs["multiple"] = "";
    line->attrs["onchange"] = "on_file";
    line->attrs["oninput"] = "on_file_input";
    line->value.clear();
    line->text.clear();

    struct Call {
        std::string type;
        std::string value;
    };
    std::vector<Call> calls;
    int pick_calls = 0;
    zui::FileDialogOptions last_opt;
    zui::html::Node* hovered = nullptr;
    zui::html::Node* focused = nullptr;
    zui::html::Node* scroll_drag = nullptr;
    zui::input::InteractionHost host;
    host.document = &scene.doc;
    host.layout = scene.layout.get();
    host.metrics = &scene.metrics;
    host.hovered = &hovered;
    host.focused = &focused;
    host.scroll_drag = &scroll_drag;
    host.pick_open = [&](const zui::FileDialogOptions& opt) {
        ++pick_calls;
        last_opt = opt;
        zui::PickPaths out;
        out.status = zui::PickStatus::Ok;
        out.paths = {"C:/books/a.txt", "D:/more/b.md"};
        return out;
    };
    host.dispatch = [&](zui::Event& e) {
        calls.push_back(Call{std::string(e.type()), std::string(e.value())});
    };
    zui::input::Interaction interaction(host);

    zui::layout::Box* box = scene.box("line");
    ASSERT_NE(box, nullptr);
    const float x = box->geo.world_x + box->geo.width * 0.5f;
    const float y = box->geo.world_y + box->geo.height * 0.5f;
    interaction.pointer_button("mousePressed", x, y, "left", 1);
    interaction.pointer_button("mouseReleased", x, y, "left", 1);

    EXPECT_EQ(pick_calls, 1);
    EXPECT_TRUE(last_opt.multi);
    ASSERT_FALSE(last_opt.filters.empty());
    EXPECT_NE(last_opt.filters[0].patterns.find("txt"), std::string::npos);
    EXPECT_EQ(line->value, "a.txt; b.md");
    bool saw_input = false;
    bool saw_change = false;
    for (const Call& c : calls) {
        if (c.type == "input" && c.value == "a.txt; b.md") {
            saw_input = true;
        }
        if (c.type == "change" && c.value == "a.txt; b.md") {
            saw_change = true;
        }
    }
    EXPECT_TRUE(saw_input);
    EXPECT_TRUE(saw_change);

    // Cancelled → no value change.
    host.pick_open = [&](const zui::FileDialogOptions&) {
        ++pick_calls;
        zui::PickPaths out;
        out.status = zui::PickStatus::Cancelled;
        return out;
    };
    const std::string before = line->value;
    interaction.pointer_button("mousePressed", x, y, "left", 1);
    interaction.pointer_button("mouseReleased", x, y, "left", 1);
    EXPECT_EQ(line->value, before);
}

// Hover A→B: leave on A (and wrap if leaving wrap), enter on B; keyup; dblclick when count>=2.
TEST(DifftestSelfCheck, HoverEnterLeaveKeyupAndDblclick) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "events.html")) << error;

    struct Call {
        std::string type;
        std::string name;
        std::string source;
        bool has_pointer = false;
        bool has_key = false;
        std::string key;
    };
    std::vector<Call> calls;
    zui::html::Node* hovered = nullptr;
    zui::html::Node* focused = nullptr;
    zui::html::Node* scroll_drag = nullptr;
    zui::input::InteractionHost host;
    host.document = &scene.doc;
    host.layout = scene.layout.get();
    host.metrics = &scene.metrics;
    host.hovered = &hovered;
    host.focused = &focused;
    host.scroll_drag = &scroll_drag;
    host.dispatch = [&calls](zui::Event& e) {
        Call call;
        call.type = e.type();
        call.name = e.name();
        call.source = e.source();
        call.has_pointer = e.is_pointer() != nullptr;
        if (const zui::Key* k = e.is_key()) {
            call.has_key = true;
            call.key = k->key;
        }
        calls.push_back(std::move(call));
    };
    zui::input::Interaction interaction(host);

    zui::layout::Box* a = scene.box("zone_a");
    zui::layout::Box* b = scene.box("zone_b");
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    const float ax = a->geo.world_x + 8.0f;
    const float ay = a->geo.world_y + 8.0f;
    const float bx = b->geo.world_x + 8.0f;
    const float by = b->geo.world_y + 8.0f;

    zui::Pointer mods;
    mods.x = ax;
    mods.y = ay;
    calls.clear();
    interaction.pointer_move(ax, ay, mods);
    ASSERT_GE(calls.size(), std::size_t{1});
    // Entering A from nowhere, in UI Events section 5.6 order: mouseover on A first, then the
    // enter pair outermost-first (wrap mouseenter, then A pointerenter+mouseenter). Wrap has no
    // pointerenter attribute. The engine used to fire the enter pair before mouseover.
    std::vector<std::string> enter_types;
    for (const Call& c : calls) {
        if (c.type.find("enter") != std::string::npos || c.type == "mouseover") {
            enter_types.push_back(c.type + ":" + c.source);
        }
    }
    ASSERT_EQ(enter_types.size(), std::size_t{4});
    EXPECT_EQ(enter_types[0], "mouseover:zone_a");
    EXPECT_EQ(enter_types[1], "mouseenter:zone_wrap");
    EXPECT_EQ(enter_types[2], "pointerenter:zone_a");
    EXPECT_EQ(enter_types[3], "mouseenter:zone_a");
    EXPECT_TRUE(
        std::any_of(calls.begin(), calls.end(), [](const Call& c) { return c.has_pointer; }));

    // A → B: leave A, enter B; wrap stays common so wrap does not leave/enter.
    calls.clear();
    mods.x = bx;
    mods.y = by;
    interaction.pointer_move(bx, by, mods);
    std::vector<std::string> seq;
    for (const Call& c : calls) {
        seq.push_back(c.type + ":" + c.source);
    }
    // Innermost leave first, then enter outermost-first under common (only B).
    auto find_seq = [&](std::string_view needle) -> int {
        for (std::size_t i = 0; i < seq.size(); ++i) {
            if (seq[i] == needle) {
                return static_cast<int>(i);
            }
        }
        return -1;
    };
    const int leave_pa = find_seq("pointerleave:zone_a");
    const int leave_ma = find_seq("mouseleave:zone_a");
    const int enter_mb = find_seq("mouseenter:zone_b");
    const int out_a = find_seq("mouseout:zone_a");
    // zone_b has no mouseover / pointerenter; over walks to wrap or none.
    EXPECT_GE(leave_pa, 0);
    EXPECT_GE(leave_ma, 0);
    EXPECT_GE(enter_mb, 0);
    EXPECT_LT(leave_pa, leave_ma);
    EXPECT_LT(leave_ma, enter_mb);
    EXPECT_EQ(find_seq("mouseleave:zone_wrap"), -1) << "common ancestor must not leave";
    EXPECT_EQ(find_seq("mouseenter:zone_wrap"), -1);
    // mouseout fires ahead of the leave pair (UI Events section 5.6), not after it.
    if (out_a >= 0) {
        EXPECT_LT(out_a, leave_pa);
    }

    // keyup: same target algorithm as keydown (focus, else body).
    zui::html::Node* keys = zui::html::find(scene.doc, "keys");
    ASSERT_NE(keys, nullptr);
    focused = keys;
    calls.clear();
    EXPECT_FALSE(interaction.key("keyUp", "a", zui::Key{"", false, false}));
    ASSERT_EQ(calls.size(), std::size_t{1});
    EXPECT_EQ(calls[0].type, "keyup");
    EXPECT_EQ(calls[0].name, "on_up");
    EXPECT_EQ(calls[0].source, "keys");
    ASSERT_TRUE(calls[0].has_key);
    EXPECT_EQ(calls[0].key, "a");

    // dblclick after click when count >= 2.
    zui::layout::Box* dbl = scene.box("dbl");
    ASSERT_NE(dbl, nullptr);
    calls.clear();
    interaction.pointer_button("mousePressed", dbl->geo.world_x + 4.0f, dbl->geo.world_y + 4.0f,
                               "left", 2);
    interaction.pointer_button("mouseReleased", dbl->geo.world_x + 4.0f,
                               dbl->geo.world_y + 4.0f, "left", 1);
    std::vector<std::string> click_seq;
    for (const Call& c : calls) {
        if (c.type == "click" || c.type == "dblclick") {
            click_seq.push_back(c.type);
        }
    }
    ASSERT_EQ(click_seq.size(), std::size_t{2});
    EXPECT_EQ(click_seq[0], "click");
    EXPECT_EQ(click_seq[1], "dblclick");
}

// `:hover { transform }` moves the painted box. Hit-testing uses that Aff2, so the pointer at the
// original bottom edge would miss on the next probe and the control would flicker (zui_motion Play).
TEST(DifftestSelfCheck, HoverKeepsTransformedControlWhilePointerInLayoutBox) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "hover-transform.html")) << error;

    zui::html::Node* hovered = nullptr;
    zui::html::Node* focused = nullptr;
    zui::html::Node* scroll_drag = nullptr;
    zui::input::InteractionHost host;
    host.document = &scene.doc;
    host.layout = scene.layout.get();
    host.metrics = &scene.metrics;
    host.hovered = &hovered;
    host.focused = &focused;
    host.scroll_drag = &scroll_drag;
    host.restyle = [&scene] { scene.frame(); };
    zui::input::Interaction interaction(host);

    zui::html::Node* play = zui::html::find(scene.doc, "play");
    ASSERT_NE(play, nullptr);
    zui::layout::Box* box = scene.box("play");
    ASSERT_NE(box, nullptr);
    const float x = box->geo.world_x + box->geo.width * 0.5f;
    const float y = box->geo.world_y + box->geo.height - 2.f;

    zui::Pointer mods;
    mods.x = x;
    mods.y = y;
    interaction.pointer_move(x, y, mods);
    ASSERT_EQ(hovered, play);
    EXPECT_TRUE(play->hovered);

    scene.frame();
    box = scene.box("play");
    ASSERT_NE(box, nullptr);
    EXPECT_LT(box->geo.world_y + box->geo.height, y)
        << "the hover transform did not move the painted box off the pointer";

    mods.y = y - 0.5f;
    interaction.pointer_move(x, y - 0.5f, mods);
    EXPECT_EQ(hovered, play) << "hover dropped after the transform moved the painted box";
    EXPECT_TRUE(play->hovered);
}

// EVENT.md section 8: a window-level event is not a second mechanism. The user's close request
// is a `beforeunload` firing on <body>, and only a handler that calls prevent_default() keeps
// the frame alive - the app then answers the request itself and calls quit() when it is ready.
TEST(DifftestSelfCheck, WindowCloseIsDispatchedToBodyAndCancelable) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "events.html")) << error;
    zui::html::Node* body = zui::html::find_tag(scene.doc.root.get(), "body");
    ASSERT_NE(body, nullptr);

    struct Firing {
        std::string type;
        std::string name;
        std::string source;
        std::string value;
        std::string scope;
        bool has_key = false;
        bool prevented = false;
    };
    std::vector<Firing> firings;
    bool dirty = false;
    zui::input::InteractionHost host;
    host.document = &scene.doc;
    host.dispatch = [&](zui::Event& e) {
        Firing firing;
        firing.type = e.type();
        firing.name = e.name();
        firing.source = e.source();
        firing.value = e.value();
        firing.scope = e.data("scope");
        firing.has_key = e.is_key() != nullptr;
        if (dirty) {
            e.prevent_default();
        }
        firing.prevented = e.default_prevented();
        firings.push_back(std::move(firing));
    };
    zui::input::Interaction interaction(host);

    // A clean document: nobody cancels, so the loop is allowed to end.
    EXPECT_FALSE(interaction.fire_from(body, "beforeunload"));
    ASSERT_EQ(firings.size(), std::size_t{1});
    EXPECT_EQ(firings[0].type, "beforeunload");
    EXPECT_EQ(firings[0].name, "on_guard");
    EXPECT_EQ(firings[0].source, "") << "<body> carries no id";
    EXPECT_EQ(firings[0].scope, "window");
    EXPECT_EQ(firings[0].value, "") << "beforeunload carries no payload";
    EXPECT_FALSE(firings[0].has_key);

    // Unsaved work: the guard cancels, and that is the signal that keeps this frame in the loop.
    dirty = true;
    firings.clear();
    EXPECT_TRUE(interaction.fire_from(body, "beforeunload"));
    ASSERT_EQ(firings.size(), std::size_t{1});
    EXPECT_TRUE(firings[0].prevented);
}

// A click in a text control reads the line its y lands on - and a soft wrap is a line. The hit
// test used to give up whenever the value held no `\n` (a chat composer is one long soft-wrapped
// line), fell back to measuring the whole value as a single line, and put the caret back at the
// top of the text.
TEST(DifftestSelfCheck, TextareaClickReadsTheWrappedLine) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "events.html")) << error;
    zui::html::Node* draft = zui::html::find(scene.doc, "draft");
    ASSERT_NE(draft, nullptr);
    zui::html::Node* hovered = nullptr;
    zui::html::Node* focused = nullptr;
    zui::html::Node* scroll_drag = nullptr;
    zui::input::InteractionHost host;
    host.document = &scene.doc;
    host.layout = scene.layout.get();
    host.metrics = &scene.metrics;
    host.hovered = &hovered;
    host.focused = &focused;
    host.scroll_drag = &scroll_drag;
    host.restyle = [&scene] { scene.frame(); };
    zui::input::Interaction interaction(host);
    const auto click_at = [&](float x, float y) {
        interaction.pointer_button("mousePressed", x, y, "left", 1);
        interaction.pointer_button("mouseReleased", x, y, "left", 1);
    };

    // Lines broken by `\n`: the second line, one glyph in.
    draft->value = "alpha\nbravo\ncharlie";
    draft->text = draft->value;
    draft->caret = 0;
    scene.layout->sync_text(draft);
    scene.layout->mark_needs_layout(draft);
    scene.frame();
    zui::layout::Box* box = scene.box("draft");
    ASSERT_NE(box, nullptr);
    const float left = box->geo.world_x + box->geo.border.left + box->geo.padding.left;
    const float top = box->geo.world_y + box->geo.border.top + box->geo.padding.top;
    click_at(left + 4.f, top + 24.f);
    EXPECT_GE(draft->caret, 6) << draft->caret;
    EXPECT_LT(draft->caret, 11) << draft->caret;

    // One long line that soft-wraps into several runs: there is no `\n` to split on, so the run
    // table is the only thing that knows where the second visual line starts.
    std::string wrapped;
    for (int i = 0; i < 40; ++i) {
        wrapped += "word" + std::to_string(i) + " ";
    }
    draft->value = wrapped;
    draft->text = wrapped;
    draft->caret = 0;
    scene.layout->sync_text(draft);
    scene.layout->mark_needs_layout(draft);
    scene.frame();
    box = scene.box("draft");
    ASSERT_NE(box, nullptr);
    ASSERT_GE(box->geo.runs.size(), std::size_t{2});
    const zui::layout::TextRun& second = box->geo.runs[1];
    const std::size_t second_begin = wrapped.find(second.text);
    ASSERT_NE(second_begin, std::string::npos);
    click_at(box->geo.world_x + second.x + 2.f,
             box->geo.world_y + second.y + second.line_block * 0.5f);
    EXPECT_GE(draft->caret, second_begin) << draft->caret;
    EXPECT_LE(draft->caret, second_begin + second.text.size()) << draft->caret;
}

// The caret keys follow the laid-out lines too: the vertical arrows walk one line (a soft wrap
// counts as one) and stop at the far edge, and Home / End stay inside the caret's own line
// instead of jumping to the ends of the whole value.
TEST(DifftestSelfCheck, TextareaCaretKeysWalkTheLaidOutLines) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "events.html")) << error;
    zui::html::Node* draft = zui::html::find(scene.doc, "draft");
    ASSERT_NE(draft, nullptr);
    draft->value = "alpha\nbravo\ncharlie";
    draft->text = draft->value;
    draft->caret = 0;
    scene.layout->sync_text(draft);
    scene.layout->mark_needs_layout(draft);
    scene.frame();

    zui::html::Node* hovered = nullptr;
    zui::html::Node* focused = nullptr;
    zui::html::Node* scroll_drag = nullptr;
    zui::input::InteractionHost host;
    host.document = &scene.doc;
    host.layout = scene.layout.get();
    host.metrics = &scene.metrics;
    host.hovered = &hovered;
    host.focused = &focused;
    host.scroll_drag = &scroll_drag;
    host.restyle = [&scene] { scene.frame(); };
    zui::input::Interaction interaction(host);
    focused = draft;

    // ArrowDown from the line's start walks to the same column on the next line; ArrowUp comes
    // back; both stop at the far edge instead of running away.
    draft->caret = 6;
    zui::input::EditInput vertical;
    vertical.down = 1;
    // A caret-only edit answers false: `apply_editing`'s return tells whether the text changed.
    interaction.apply_editing(vertical);
    EXPECT_GE(draft->caret, 12) << draft->caret;
    EXPECT_LE(draft->caret, 19) << draft->caret;
    zui::input::EditInput back_up;
    back_up.up = 1;
    interaction.apply_editing(back_up);
    EXPECT_EQ(draft->caret, 6u);
    back_up.up = 1;
    interaction.apply_editing(back_up);
    EXPECT_EQ(draft->caret, 0u) << "Up on the first line lands on its start";
    vertical.down = 9;
    interaction.apply_editing(vertical);
    EXPECT_EQ(draft->caret, 19u) << "Down past the last line lands on its end";

    // Home / End move inside the caret's own line, not the whole value.
    draft->caret = 8;
    zui::input::EditInput home;
    home.home = 1;
    interaction.apply_editing(home);
    EXPECT_EQ(draft->caret, 6u);
    zui::input::EditInput to_end;
    to_end.end = 1;
    interaction.apply_editing(to_end);
    EXPECT_EQ(draft->caret, 11u);
}

// Shift+move grows a selection: the caret moves, the anchor stays, and the range is as live as
// one a drag made - typing replaces it. The engine had no such move at all: every caret key
// collapsed the selection it was supposed to extend.
TEST(DifftestSelfCheck, TextareaShiftArrowsGrowASelection) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "events.html")) << error;
    zui::html::Node* draft = zui::html::find(scene.doc, "draft");
    ASSERT_NE(draft, nullptr);
    draft->value = "alpha\nbravo\ncharlie";
    draft->text = draft->value;
    draft->caret = 0;
    scene.layout->sync_text(draft);
    scene.layout->mark_needs_layout(draft);
    scene.frame();

    zui::html::Node* hovered = nullptr;
    zui::html::Node* focused = nullptr;
    zui::html::Node* scroll_drag = nullptr;
    zui::input::InteractionHost host;
    host.document = &scene.doc;
    host.layout = scene.layout.get();
    host.metrics = &scene.metrics;
    host.hovered = &hovered;
    host.focused = &focused;
    host.scroll_drag = &scroll_drag;
    host.restyle = [&scene] { scene.frame(); };
    zui::input::Interaction interaction(host);
    focused = draft;

    auto expect_range = [&](std::size_t from, std::size_t to) {
        EXPECT_EQ(draft->caret, to);
        EXPECT_EQ(scene.doc.selection.node, draft);
        EXPECT_EQ(scene.doc.selection.start, from);
        EXPECT_EQ(scene.doc.selection.end, to);
    };
    zui::input::EditInput step;
    step.right = 1;
    step.extend = true;
    interaction.apply_editing(step);
    expect_range(0, 1);
    interaction.apply_editing(step);
    expect_range(0, 2);

    // Shift+Down walks a line with the anchor held; Shift+Up comes back to it and collapses.
    zui::input::EditInput down;
    down.down = 1;
    down.extend = true;
    interaction.apply_editing(down);
    EXPECT_TRUE(scene.doc.selection.start < scene.doc.selection.end);
    EXPECT_GE(draft->caret, 6u) << "the caret should be on the second line";
    zui::input::EditInput up;
    up.up = 1;
    up.extend = true;
    interaction.apply_editing(up);
    EXPECT_EQ(draft->caret, 2u);
    EXPECT_EQ(scene.doc.selection.start, 0u);
    EXPECT_EQ(scene.doc.selection.end, 2u);
    up.up = 1;
    interaction.apply_editing(up);
    EXPECT_EQ(draft->caret, 0u);
    EXPECT_TRUE(zui::html::selection_empty(scene.doc.selection))
        << "shift back onto the anchor collapses the range";

    // The range is live: typing replaces it, the way it replaces a dragged one.
    step.right = 1;
    step.extend = true;
    interaction.apply_editing(step);
    interaction.apply_editing(step);
    expect_range(0, 2);
    zui::input::EditInput typed;
    typed.text = "X";
    ASSERT_TRUE(interaction.apply_editing(typed));
    EXPECT_EQ(draft->value, "Xpha\nbravo\ncharlie");
    EXPECT_EQ(draft->caret, 1u);
    EXPECT_TRUE(zui::html::selection_empty(scene.doc.selection));

    // A plain move collapses whatever was selected.
    draft->caret = 0;
    zui::html::set_selection(scene.doc, draft, 0, 2);
    zui::input::EditInput plain;
    plain.right = 1;
    interaction.apply_editing(plain);
    EXPECT_EQ(draft->caret, 1u);
    EXPECT_TRUE(zui::html::selection_empty(scene.doc.selection));
}

// Shift+click extends a text control's selection: the caret moves to the click and the range's
// other end stays where it was. The press ignored the modifier and collapsed the range, so a
// shift+click that was meant to widen a selection threw it away - and a second one, back across
// the anchor, has to keep the anchor and extend the other way.
TEST(DifftestSelfCheck, EditableShiftClickExtendsTheSelection) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "events.html")) << error;
    zui::html::Node* draft = zui::html::find(scene.doc, "draft");
    ASSERT_NE(draft, nullptr);
    draft->value = "alpha bravo charlie";
    draft->text = draft->value;
    draft->caret = 0;
    scene.layout->sync_text(draft);
    scene.layout->mark_needs_layout(draft);
    scene.frame();

    zui::layout::Box* box = scene.box("draft");
    ASSERT_NE(box, nullptr);
    ASSERT_NE(box->style_ptr, nullptr);
    zui::html::Node* hovered = nullptr;
    zui::html::Node* focused = nullptr;
    zui::html::Node* scroll_drag = nullptr;
    zui::input::InteractionHost host;
    host.document = &scene.doc;
    host.layout = scene.layout.get();
    host.metrics = &scene.metrics;
    host.hovered = &hovered;
    host.focused = &focused;
    host.scroll_drag = &scroll_drag;
    host.restyle = [&scene] { scene.frame(); };
    zui::input::Interaction interaction(host);
    const float x0 = box->geo.world_x + box->geo.border.left + box->geo.padding.left;
    const float y = box->geo.world_y + box->geo.border.top + box->geo.padding.top + 8.f;
    const auto x_at = [&](std::size_t index) {
        const float w = scene.metrics
                            .measure(std::string_view(draft->value).substr(0, index),
                                     box->style().font_size, box->style().font_family)
                            .width;
        return x0 + w;
    };
    const auto click = [&](std::size_t index, bool shift) {
        zui::Pointer mods;
        mods.shift = shift;
        interaction.pointer_button("mousePressed", x_at(index), y, "left", 1, mods);
        interaction.pointer_button("mouseReleased", x_at(index), y, "left", 1, mods);
    };

    click(3, false);
    EXPECT_EQ(draft->caret, 3u);
    EXPECT_TRUE(zui::html::selection_empty(scene.doc.selection));

    click(11, true);
    EXPECT_EQ(draft->caret, 11u);
    EXPECT_EQ(scene.doc.selection.node, draft);
    EXPECT_EQ(scene.doc.selection.start, 3u);
    EXPECT_EQ(scene.doc.selection.end, 11u);

    // Back across the anchor: the anchor is the far end, so the range flips.
    click(1, true);
    EXPECT_EQ(draft->caret, 1u);
    EXPECT_EQ(scene.doc.selection.start, 1u);
    EXPECT_EQ(scene.doc.selection.end, 3u);

    // The range is live: typing replaces it.
    zui::input::EditInput typed;
    typed.text = "Z";
    ASSERT_TRUE(interaction.apply_editing(typed));
    EXPECT_EQ(draft->value, "aZha bravo charlie");
    EXPECT_TRUE(zui::html::selection_empty(scene.doc.selection));

    // A plain click still collapses.
    click(6, true);
    click(6, false);
    EXPECT_TRUE(zui::html::selection_empty(scene.doc.selection));
    EXPECT_EQ(draft->caret, 6u);
}

// A triple click takes the laid-out line under the pointer: the second click already took the
// word, and a browser's third one reads the line - the same range the writing view's third click
// takes off the paper.
TEST(DifftestSelfCheck, EditableTripleClickTakesTheLine) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "events.html")) << error;
    zui::html::Node* draft = zui::html::find(scene.doc, "draft");
    ASSERT_NE(draft, nullptr);
    draft->value = "alpha\nbravo\ncharlie";
    draft->text = draft->value;
    draft->caret = 0;
    scene.layout->sync_text(draft);
    scene.layout->mark_needs_layout(draft);
    scene.frame();

    zui::layout::Box* box = scene.box("draft");
    ASSERT_NE(box, nullptr);
    ASSERT_NE(box->style_ptr, nullptr);
    zui::html::Node* hovered = nullptr;
    zui::html::Node* focused = nullptr;
    zui::html::Node* scroll_drag = nullptr;
    zui::input::InteractionHost host;
    host.document = &scene.doc;
    host.layout = scene.layout.get();
    host.metrics = &scene.metrics;
    host.hovered = &hovered;
    host.focused = &focused;
    host.scroll_drag = &scroll_drag;
    host.restyle = [&scene] { scene.frame(); };
    zui::input::Interaction interaction(host);
    const float x = box->geo.world_x + box->geo.border.left + box->geo.padding.left + 4.f;
    const float y = box->geo.world_y + box->geo.border.top + box->geo.padding.top + 24.f;
    interaction.pointer_button("mousePressed", x, y, "left", 3);
    interaction.pointer_button("mouseReleased", x, y, "left", 3);
    EXPECT_EQ(scene.doc.selection.node, draft);
    EXPECT_EQ(scene.doc.selection.start, 6u) << "the second line starts at 6";
    EXPECT_EQ(scene.doc.selection.end, 11u) << "and ends before its `\\n`";
    EXPECT_EQ(draft->caret, 11);
}

// A selection drag that leaves the control keeps going: the pointer parks below the fold, and the
// scroller under it has to move while the button is down. Browsers scroll there (faster the further
// out the pointer is) and extend the selection over the text that arrives; the engine only
// extended on a pointer *move*, so a parked pointer selected nothing more.
TEST(DifftestSelfCheck, DragPastTheFoldKeepsScrollingAndSelecting) {
    std::string value;
    for (int i = 0; i < 60; ++i) {
        value += "line" + std::to_string(i) + "\n";
    }
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "zui-drag-scroll.html";
    {
        std::ofstream out(path);
        out << "<!DOCTYPE html><html><head><style>html,body{margin:0}"
               "#pane{height:150px;overflow-y:auto}"
               "#grow{display:block;width:300px;font-size:14px;line-height:20px}"
               "</style></head><body><div id=\"pane\"><textarea id=\"grow\"></textarea></div>"
               "</body></html>";
    }
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load_path(error, path.string())) << error;
    zui::html::Node* grow = zui::html::find(scene.doc, "grow");
    zui::html::Node* pane = zui::html::find(scene.doc, "pane");
    ASSERT_NE(grow, nullptr);
    ASSERT_NE(pane, nullptr);
    grow->value = value;
    grow->text = value;
    grow->caret = 0;
    scene.layout->sync_text(grow);
    scene.layout->mark_needs_layout(grow);
    scene.frame();

    zui::layout::Box* box = scene.layout->principal_box(grow);
    ASSERT_NE(box, nullptr);
    zui::html::Node* hovered = nullptr;
    zui::html::Node* focused = nullptr;
    zui::html::Node* scroll_drag = nullptr;
    zui::input::InteractionHost host;
    host.document = &scene.doc;
    host.layout = scene.layout.get();
    host.metrics = &scene.metrics;
    host.hovered = &hovered;
    host.focused = &focused;
    host.scroll_drag = &scroll_drag;
    host.restyle = [&scene] { scene.frame(); };
    zui::input::Interaction interaction(host);

    const float x = box->geo.world_x + 20.f;
    const float y0 = box->geo.world_y + 10.f;
    interaction.pointer_button("mousePressed", x, y0, "left", 1);
    const float below = box->geo.world_y + box->geo.height + 30.f;
    interaction.pointer_button("mouseMoved", x, below, "left", 1);
    const std::size_t at_start = static_cast<std::size_t>(std::max(grow->caret, 0));
    for (int i = 0; i < 20; ++i) {
        interaction.tick_drag_scroll();
        scene.frame();
    }
    EXPECT_GT(pane->scroll_y, 0.f) << "the pane never moved under the parked pointer";
    EXPECT_GT(grow->caret, static_cast<int>(at_start))
        << "the selection did not follow the text that arrived";
    EXPECT_EQ(scene.doc.selection.node, grow);
    EXPECT_TRUE(scene.doc.selection.start <= scene.doc.selection.end);
    interaction.pointer_button("mouseReleased", x, below, "left", 1);
    const float parked = pane->scroll_y;
    interaction.tick_drag_scroll();
    EXPECT_EQ(pane->scroll_y, parked) << "the scroll stops with the drag";
}

// A CJK run is one word. A browser segments `你好，世界` into two words at the fullwidth comma, and
// its double click takes the run between them; the ASCII-only rule made every ideograph a word of
// its own, so a double click in Chinese text took a single character. Ctrl+Arrow and the find
// word boundaries read the same rule.
TEST(DifftestSelfCheck, EditableDoubleClickTakesACjkRun) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "events.html")) << error;
    zui::html::Node* draft = zui::html::find(scene.doc, "draft");
    ASSERT_NE(draft, nullptr);
    draft->value = "say \xe4\xbd\xa0\xe5\xa5\xbd\xef\xbc\x8c\xe4\xb8\x96\xe7\x95\x8c!";  // "say 你好，世界!"
    draft->text = draft->value;
    draft->caret = 0;
    scene.layout->sync_text(draft);
    scene.layout->mark_needs_layout(draft);
    scene.frame();

    zui::layout::Box* box = scene.box("draft");
    ASSERT_NE(box, nullptr);
    ASSERT_NE(box->style_ptr, nullptr);
    zui::html::Node* hovered = nullptr;
    zui::html::Node* focused = nullptr;
    zui::html::Node* scroll_drag = nullptr;
    zui::input::InteractionHost host;
    host.document = &scene.doc;
    host.layout = scene.layout.get();
    host.metrics = &scene.metrics;
    host.hovered = &hovered;
    host.focused = &focused;
    host.scroll_drag = &scroll_drag;
    host.restyle = [&scene] { scene.frame(); };
    zui::input::Interaction interaction(host);
    const float x0 = box->geo.world_x + box->geo.border.left + box->geo.padding.left;
    const float y = box->geo.world_y + box->geo.border.top + box->geo.padding.top + 8.f;
    const auto x_at = [&](std::size_t index) {
        const float w = scene.metrics
                            .measure(std::string_view(draft->value).substr(0, index),
                                     box->style().font_size, box->style().font_family)
                            .width;
        return x0 + w;
    };
    const std::string value = draft->value;
    const std::size_t first = value.find("\xe4\xbd\xa0");   // 你
    const std::size_t comma = value.find("\xef\xbc\x8c");   // ，
    const std::size_t second = value.find("\xe4\xb8\x96");  // 世
    ASSERT_NE(first, std::string::npos);
    ASSERT_NE(comma, std::string::npos);
    ASSERT_NE(second, std::string::npos);

    // A double click inside 你好 takes the two ideographs, not one character.
    interaction.pointer_button("mousePressed", x_at(first + 3), y, "left", 2);
    interaction.pointer_button("mouseReleased", x_at(first + 3), y, "left", 2);
    EXPECT_EQ(scene.doc.selection.start, first);
    EXPECT_EQ(scene.doc.selection.end, comma) << "the fullwidth comma ends the word";

    // Ctrl+Right walks the same words.
    draft->caret = static_cast<int>(first + 3);
    zui::html::set_selection(scene.doc, draft, first + 3, first + 3);
    zui::input::EditInput word_right;
    word_right.right = 1;
    word_right.word = true;
    interaction.apply_editing(word_right);
    EXPECT_EQ(draft->caret, static_cast<int>(comma)) << "the comma is where the word ends";
    interaction.apply_editing(word_right);
    EXPECT_EQ(draft->caret, static_cast<int>(value.find('!')))
        << "the comma separates, and Ctrl+Right lands at the end of the next word";
}

// Ctrl with Home / End / the arrows: the whole value's ends, and whole words. Every browser reads
// the modifier that way, and the engine ignored it - Ctrl+Left moved one character and Ctrl+End
// stopped at the end of the caret's own line.
TEST(DifftestSelfCheck, EditableCtrlWordsAndWholeValueEdges) {
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load(error, "events.html")) << error;
    zui::html::Node* draft = zui::html::find(scene.doc, "draft");
    ASSERT_NE(draft, nullptr);
    draft->value = "alpha bravo charlie";
    draft->text = draft->value;
    draft->caret = 0;
    scene.layout->sync_text(draft);
    scene.layout->mark_needs_layout(draft);
    scene.frame();

    zui::html::Node* hovered = nullptr;
    zui::html::Node* focused = nullptr;
    zui::html::Node* scroll_drag = nullptr;
    zui::input::InteractionHost host;
    host.document = &scene.doc;
    host.layout = scene.layout.get();
    host.metrics = &scene.metrics;
    host.hovered = &hovered;
    host.focused = &focused;
    host.scroll_drag = &scroll_drag;
    host.restyle = [&scene] { scene.frame(); };
    zui::input::Interaction interaction(host);
    focused = draft;
    const auto type = [&](zui::input::EditInput step) {
        interaction.apply_editing(step);
        return draft->caret;
    };
    zui::input::EditInput word_right;
    word_right.right = 1;
    word_right.word = true;
    EXPECT_EQ(type(word_right), 5) << "the end of the word the caret sits in";
    EXPECT_EQ(type(word_right), 11) << "over the space and to the next word's end";
    zui::input::EditInput word_left;
    word_left.left = 1;
    word_left.word = true;
    EXPECT_EQ(type(word_left), 6) << "back to the word's start";

    zui::input::EditInput doc_end;
    doc_end.end = 1;
    doc_end.word = true;
    EXPECT_EQ(type(doc_end), draft->value.size()) << "Ctrl+End is the value's end";
    zui::input::EditInput doc_home;
    doc_home.home = 1;
    doc_home.word = true;
    EXPECT_EQ(type(doc_home), 0u) << "Ctrl+Home is the value's start";

    // Ctrl+Shift is the same move with the anchor held.
    zui::input::EditInput select_words;
    select_words.right = 2;
    select_words.word = true;
    select_words.extend = true;
    type(select_words);
    EXPECT_EQ(scene.doc.selection.start, 0u);
    EXPECT_EQ(scene.doc.selection.end, 11u);
    zui::input::EditInput whole;
    whole.end = 1;
    whole.word = true;
    whole.extend = true;
    type(whole);
    EXPECT_EQ(scene.doc.selection.end, draft->value.size());
}

// PageUp / PageDown in a focused text control. A control with its own scroll moves the caret a
// page of laid-out lines; one that shows all of its text at once lets the page it sits in move.
// The key did nothing at all before: the editable branch answered before the scroll default, and
// the edit frame had no page counts to answer with.
TEST(DifftestSelfCheck, TextareaPageKeysMoveTheCaretOrThePage) {
    std::string value;
    for (int i = 0; i < 40; ++i) {
        value += "l" + std::to_string(i) + "\n";
    }
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "zui-page-keys.html";
    {
        std::ofstream out(path);
        out << "<!DOCTYPE html><html><head><style>html,body{margin:0}"
               "#pane{height:120px;overflow-y:auto}"
               "#tall{display:block;width:300px;height:44px;font-size:14px;line-height:22px}"
               "#grow{display:block;width:300px;font-size:14px;line-height:22px}"
               "</style></head><body><div id=\"pane\"><textarea id=\"grow\"></textarea></div>"
               "<textarea id=\"tall\"></textarea></body></html>";
    }
    Scene scene;
    std::string error;
    ASSERT_TRUE(scene.load_path(error, path.string())) << error;
    zui::html::Node* tall = zui::html::find(scene.doc, "tall");
    zui::html::Node* grow = zui::html::find(scene.doc, "grow");
    zui::html::Node* pane = zui::html::find(scene.doc, "pane");
    ASSERT_NE(tall, nullptr);
    ASSERT_NE(grow, nullptr);
    ASSERT_NE(pane, nullptr);
    for (zui::html::Node* node : {tall, grow}) {
        node->value = value;
        node->text = value;
        node->caret = 0;
        scene.layout->sync_text(node);
        scene.layout->mark_needs_layout(node);
    }
    scene.frame();

    zui::html::Node* hovered = nullptr;
    zui::html::Node* focused = nullptr;
    zui::html::Node* scroll_drag = nullptr;
    zui::input::InteractionHost host;
    host.document = &scene.doc;
    host.layout = scene.layout.get();
    host.metrics = &scene.metrics;
    host.hovered = &hovered;
    host.focused = &focused;
    host.scroll_drag = &scroll_drag;
    host.restyle = [&scene] { scene.frame(); };
    zui::input::Interaction interaction(host);

    // The fixed-height field scrolls its own text: the caret walks down the lines it shows.
    zui::layout::Box* tall_box = scene.layout->principal_box(tall);
    ASSERT_NE(tall_box, nullptr);
    ASSERT_GT(zui::input::scroll_range(*tall_box).max_y, 0.01f)
        << "the fixture's #tall should overflow its own 44px";
    focused = tall;
    zui::input::EditInput page_down;
    page_down.page_down = 1;
    interaction.apply_editing(page_down);
    EXPECT_GE(tall->caret, 6) << "two 22px lines of a 44px port: " << tall->caret;
    EXPECT_LE(tall->caret, 12) << tall->caret;

    // The growing field inside the scroller: the page moves, the caret does not.
    zui::layout::Box* grow_box = scene.layout->principal_box(grow);
    ASSERT_NE(grow_box, nullptr);
    EXPECT_LT(zui::input::scroll_range(*grow_box).max_y, 0.01f)
        << "the growing textarea should show all of its text";
    focused = grow;
    grow->caret = 0;
    interaction.apply_editing(page_down);
    EXPECT_EQ(grow->caret, 0) << "the caret stays when the page does the moving";
    EXPECT_GT(pane->scroll_y, 0.f) << "the scroller should have moved a page";
}

// A programmatic jump to a row the reader scrolled away from. The retained window culled the
// row's geometry ("no world coordinates, no glyph runs"), and `scroll_into_view` read its content
// position off that parked rectangle: an outline heading, a focus ring or a find hit that far away
// scrolled nowhere. The walk publishes every box again before reading the rectangle - the same
// one-frame cost a jump past the retained margin already pays.
TEST(DifftestSelfCheck, ScrollIntoViewReachesACulledTarget) {
    Scene scene;
    std::string html = "<!DOCTYPE html><html><head><style>"
                       "html,body{margin:0}"
                       "#spacer{height:1500px}"
                       "#far{height:100px}"
                       "</style></head><body><div id=\"spacer\"></div>"
                       "<div id=\"far\"><span id=\"deep\">row</span></div></body></html>";
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "zui-culled-jump.html";
    {
        std::ofstream out(path);
        out << html;
    }
    std::string error;
    ASSERT_TRUE(scene.load_path(error, path.string())) << error;
    // The target sits inside the culled block: the walk parks a whole subtree, so its descendants
    // are the boxes whose rectangles go with it.
    zui::html::Node* row = zui::html::find(scene.doc, "deep");
    ASSERT_NE(row, nullptr);
    zui::layout::Box* row_box = scene.layout->principal_box(row);
    ASSERT_NE(row_box, nullptr);
    // The row sits past the fold and outside the retained margin: the first map culled it.
    ASSERT_TRUE(row_box->geo.culled) << "the row must be culled for this case";

    zui::html::Node* scrolled = nullptr;
    ASSERT_TRUE(zui::input::scroll_into_view(*scene.layout, *row, zui::ScrollAlign::Start, 15.f,
                                             &scrolled))
        << "the jump to the culled row failed";
    ASSERT_NE(scrolled, nullptr);
    // The document scroller clamps at its own bottom; the point is that the jump read the row's
    // real rectangle (1500) instead of the parked one (which pinned it to 0).
    EXPECT_NEAR(scrolled->scroll_y, 1000.f, 2.f) << "the jump landed at " << scrolled->scroll_y;
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

// A press lands on the innermost hit, which for a control with an icon is the icon and not the
// control. The press still focuses the control around it - and a text selection made elsewhere
// stays put, which is what lets "select the text, then click a toolbar button" work at all. The
// press used to find no focus candidate at all, drop the focus, and clear the selection with it.
TEST(DifftestSelfCheck, PressOnAControlsChildFocusesTheControl) {
    Scene scene;
    std::string html = "<!DOCTYPE html><html><head><style>"
                       "html,body{margin:0}"
                       "#draft{width:200px;height:60px}"
                       "#tool{width:60px;height:24px}"
                       "</style></head><body>"
                       "<textarea id=\"draft\">hello world</textarea>"
                       "<button id=\"tool\"><span id=\"icon\">B</span></button>"
                       "<button id=\"off\" disabled><span id=\"off-icon\">X</span></button>"
                       "</body></html>";
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "zui-control-child-press.html";
    {
        std::ofstream out(path);
        out << html;
    }
    std::string error;
    ASSERT_TRUE(scene.load_path(error, path.string())) << error;
    zui::html::Node* draft = zui::html::find(scene.doc, "draft");
    zui::html::Node* tool = zui::html::find(scene.doc, "tool");
    ASSERT_NE(draft, nullptr);
    ASSERT_NE(tool, nullptr);
    draft->value = "hello world";
    draft->text = draft->value;
    draft->caret = 5;
    scene.layout->sync_text(draft);
    scene.layout->mark_needs_layout(draft);
    scene.frame();

    zui::html::Node* hovered = nullptr;
    zui::html::Node* focused = nullptr;
    zui::html::Node* scroll_drag = nullptr;
    zui::input::InteractionHost host;
    host.document = &scene.doc;
    host.layout = scene.layout.get();
    host.metrics = &scene.metrics;
    host.hovered = &hovered;
    host.focused = &focused;
    host.scroll_drag = &scroll_drag;
    host.restyle = [&scene] { scene.frame(); };
    zui::input::Interaction interaction(host);
    focused = draft;
    zui::html::set_selection(scene.doc, draft, 0, 5);

    const auto press_center = [&](const char* id) {
        zui::layout::Box* box = scene.box(id);
        EXPECT_NE(box, nullptr) << id;
        if (box == nullptr) {
            return;
        }
        interaction.pointer_button("mousePressed", box->geo.world_x + box->geo.width * 0.5f,
                                   box->geo.world_y + box->geo.height * 0.5f, "left", 1);
        interaction.pointer_button("mouseReleased", box->geo.world_x + box->geo.width * 0.5f,
                                   box->geo.world_y + box->geo.height * 0.5f, "left", 1);
    };
    // The icon inside the button: the button takes the focus, the range on the textarea stays.
    press_center("icon");
    EXPECT_EQ(focused, tool);
    EXPECT_EQ(scene.doc.selection.node, draft);
    EXPECT_EQ(scene.doc.selection.start, std::size_t{0});
    EXPECT_EQ(scene.doc.selection.end, std::size_t{5});

    // A disabled button takes nothing, and the press clears the range with the focus - there is
    // nowhere for it to go.
    press_center("off-icon");
    EXPECT_EQ(focused, nullptr);
    EXPECT_TRUE(zui::html::selection_empty(scene.doc.selection));
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

// A tall block whose far end was culled leaves boxes with no world rectangle: the cull publishes
// the line box it stops at ("the child keeps its own rectangle") and wipes what is below it. A
// later scroll used to slide that block - its direct children were published, so the pure-scroll
// shortcut ran - and the wiped inline boxes rode the sentinel: the text never painted. The walk
// now keeps its hands off any subtree with a parked box below it and maps it again.
TEST(DifftestSelfCheck, ScrollingOntoAWipedInlineBoxPublishesItAgain) {
    Scene scene;
    std::string html = "<!DOCTYPE html><html><head><style>"
                       "html,body{margin:0}"
                       "#box{height:200px;overflow:auto}"
                       "#far{height:1900px}"
                       "</style></head><body><div id=\"box\">"
                       "<div id=\"p\">top<div id=\"far\"></div>"
                       "<span id=\"e\"><strong id=\"d\">bold</strong></span></div>"
                       "</div></body></html>";
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "zui-wiped-inline.html";
    {
        std::ofstream out(path);
        out << html;
    }
    std::string error;
    ASSERT_TRUE(scene.load_path(error, path.string())) << error;
    zui::layout::Box* box = scene.box("box");
    zui::html::Node* deep = zui::html::find(scene.doc, "d");
    ASSERT_NE(box, nullptr);
    ASSERT_NE(deep, nullptr);
    zui::layout::Box* deep_box = scene.layout->principal_box(deep);
    ASSERT_NE(deep_box, nullptr);
    // The far end of the tall block is past the fold: the first map never published the span.
    EXPECT_TRUE(deep_box->geo.world_y <= -1.0e9f) << deep_box->geo.world_y;

    // Scroll the container to its bottom, the sequence `Document::set` runs (offset, layout,
    // world).
    ASSERT_TRUE(zui::input::set_scroll(*box, 0.f, 1.0e6f));
    scene.frame();
    scene.layout->sync_world();

    deep_box = scene.layout->principal_box(deep);
    ASSERT_NE(deep_box, nullptr);
    EXPECT_FALSE(deep_box->geo.culled) << "the span came into view still parked";
    EXPECT_GT(deep_box->geo.width, 0.f);
    // The strong's glyphs join the parent inline formatting context, so the shaped run is
    // published on that owner, not on the strong itself.
    const zui::layout::Box* run_box = deep_box;
    while (run_box != nullptr && run_box->geo.runs.empty() && run_box->inline_text_owned_by_parent) {
        run_box = run_box->parent;
    }
    ASSERT_NE(run_box, nullptr);
    EXPECT_FALSE(run_box->geo.culled);
    bool saw_bold_run = false;
    for (const auto& run : run_box->geo.runs) {
        if (run.text.find("bold") != std::string::npos) {
            saw_bold_run = true;
        }
    }
    EXPECT_TRUE(saw_bold_run) << "the wiped inline run was not published";
    zui::paint::DisplayList list;
    zui::paint::Painter painter;
    painter.set_retained(false);
    painter.paint(*scene.layout->root(), list, 800, 600);
    bool bold = false;
    for (const auto& item : list.items()) {
        if (item.op == zui::paint::DisplayOp::DrawText &&
            item.text.find("bold") != std::string::npos) {
            bold = true;
        }
    }
    EXPECT_TRUE(bold) << "the parked inline text never painted";
    std::error_code ec;
    std::filesystem::remove(path, ec);
}
