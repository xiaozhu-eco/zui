// Browser activation, form submit, contenteditable, IME policy, and SVG software raster.
// Drives Interaction / image::load / ImeComposer. Shared scene harness: selfcheck_scene.h.

#include "selfcheck_scene.h"

#include "backends/ime_composer.h"
#include "image/driver.h"
#include "image/image.h"

#include <filesystem>
#include <fstream>
#include <string>

using namespace difftest_selfcheck;

namespace {

namespace fs = std::filesystem;

struct Rig {
    Scene scene;
    std::vector<std::string> types;
    std::vector<std::string> names;
    std::string prevent_type;
    std::string prevent_name;
    int file_picks = 0;
    bool cancelled = false;
    std::string clip = "kept";
    int clip_writes = 0;
    std::vector<std::string> opened;
    zui::html::Node* hovered = nullptr;
    zui::html::Node* focused = nullptr;
    zui::html::Node* scroll_drag = nullptr;
    std::unique_ptr<zui::input::Interaction> interaction;

    bool open(const std::string& html) {
        const ::testing::TestInfo* info = ::testing::UnitTest::GetInstance()->current_test_info();
        const std::string tag = info != nullptr ? info->name() : "page";
        const fs::path path = fs::temp_directory_path() / ("zui-browser-" + tag + ".html");
        // A second open frees the previous document. Hover and focus still name those nodes
        // until the next pointer event walks them.
        hovered = nullptr;
        focused = nullptr;
        scroll_drag = nullptr;
        {
            std::ofstream out(path);
            out << html;
        }
        std::string error;
        if (!scene.load_path(error, path.string())) {
            ADD_FAILURE() << error;
            return false;
        }
        zui::input::InteractionHost host;
        host.document = &scene.doc;
        host.layout = scene.layout.get();
        host.metrics = &scene.metrics;
        host.hovered = &hovered;
        host.focused = &focused;
        host.scroll_drag = &scroll_drag;
        host.dispatch = [this](zui::Event& event) {
            types.emplace_back(event.type());
            names.emplace_back(event.name());
            if (!prevent_type.empty() && event.type() == prevent_type) {
                event.prevent_default();
                cancelled = true;
            }
            if (!prevent_name.empty() && event.name() == prevent_name) {
                event.prevent_default();
            }
        };
        host.pick_open = [this](const zui::FileDialogOptions&) {
            ++file_picks;
            zui::PickPaths out;
            out.status = zui::PickStatus::Cancelled;
            return out;
        };
        host.clipboard_text = [this] {
            zui::ClipboardText text;
            text.status = zui::ClipboardStatus::Ok;
            text.text = clip;
            return text;
        };
        host.set_clipboard_text = [this](std::string_view text) {
            ++clip_writes;
            clip.assign(text.begin(), text.end());
            return true;
        };
        host.open_uri = [this](std::string_view uri) {
            opened.emplace_back(uri);
            return true;
        };
        interaction = std::make_unique<zui::input::Interaction>(std::move(host));
        return true;
    }

    zui::html::Node* node(const char* id) { return zui::html::find(scene.doc, id); }

    void focus_id(const char* id) {
        zui::html::Node* target = node(id);
        ASSERT_NE(target, nullptr) << id;
        ASSERT_TRUE(interaction->focus(target));
    }

    void key(const char* name, bool ctrl = false) {
        zui::Key mods;
        mods.ctrl = ctrl;
        interaction->key("keyDown", name, mods);
    }

    void click_id(const char* id) {
        zui::layout::Box* box = scene.box(id);
        ASSERT_NE(box, nullptr) << id;
        const float x = box->geo.world_x + std::max(box->geo.width * 0.5f, 1.f);
        const float y = box->geo.world_y + std::max(box->geo.height * 0.5f, 1.f);
        interaction->pointer_button("mousePressed", x, y, "left", 1);
        interaction->pointer_button("mouseReleased", x, y, "left", 1);
    }

    bool saw(const char* type) const {
        return std::find(types.begin(), types.end(), type) != types.end();
    }
};

const char* kWired[] = {
    "click", "dblclick", "input", "change", "keydown", "keyup", "beforeunload", "scroll", "wheel",
    "drop", "focus", "blur", "contextmenu", "auxclick", "pointermove", "mousemove", "pointerdown",
    "pointerup", "pointercancel", "mouseenter", "mouseleave", "mouseover", "mouseout",
    "pointerenter", "pointerleave", "dragstart", "drag", "dragenter", "dragover", "dragleave",
    "dragend", "visibilitychange", "resize", "orientationchange",
};

bool has_tag(const zui::html::Node* node, const char* tag) {
    if (node == nullptr) {
        return false;
    }
    if (node->tag == tag) {
        return true;
    }
    for (const auto& child : node->children) {
        if (has_tag(child.get(), tag)) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST(DifftestSelfCheck, WiredKindsStillDispatch) {
    Rig rig;
    ASSERT_TRUE(rig.open("<!DOCTYPE html><html><body><div id=\"n\">n</div></body></html>"));
    zui::html::Node* node = rig.node("n");
    ASSERT_NE(node, nullptr);
    for (const char* kind : kWired) {
        node->attrs[std::string("on") + kind] = "on_kind";
        rig.types.clear();
        (void)rig.interaction->fire(node, kind);
        EXPECT_TRUE(rig.saw(kind)) << kind;
    }
}

TEST(DifftestSelfCheck, EnterAndSpaceActivateControls) {
    Rig rig;
    ASSERT_TRUE(rig.open(
        "<!DOCTYPE html><html><body style=\"height:4000px\">"
        "<button id=\"go\" onclick=\"on_go\" onkeydown=\"on_go_key\">Go</button>"
        "<input id=\"ib\" type=\"button\" onclick=\"on_ib\" value=\"B\">"
        "<form id=\"f\" onsubmit=\"on_submit\">"
        "<input id=\"is\" type=\"submit\" onclick=\"on_is\" value=\"S\">"
        "<input id=\"ir\" type=\"reset\" onclick=\"on_ir\" value=\"R\">"
        "<input id=\"name\" value=\"keep\">"
        "</form>"
        "<a id=\"lnk\" href=\"https://example.com/z\" onclick=\"on_lnk\">Link</a>"
        "</body></html>"));

    zui::layout::Box* root = rig.scene.layout->root();
    ASSERT_NE(root, nullptr);
    ASSERT_NE(root->node, nullptr);
    const float before = root->node->scroll_y;
    rig.key(" ");
    EXPECT_GT(root->node->scroll_y, before + 1.f);
    root->node->scroll_y = before;

    rig.focus_id("go");
    rig.key(" ");
    EXPECT_TRUE(rig.saw("click"));
    EXPECT_NEAR(root->node->scroll_y, before, 0.5f);
    rig.types.clear();
    rig.key("Enter");
    EXPECT_TRUE(rig.saw("click"));

    rig.types.clear();
    rig.focus_id("ib");
    rig.key(" ");
    rig.key("Enter");
    EXPECT_EQ(std::count(rig.types.begin(), rig.types.end(), "click"), 2);
    EXPECT_FALSE(rig.saw("submit"));

    rig.types.clear();
    rig.focus_id("is");
    rig.key("Enter");
    EXPECT_TRUE(rig.saw("click"));
    EXPECT_TRUE(rig.saw("submit"));

    zui::html::Node* name = rig.node("name");
    ASSERT_NE(name, nullptr);
    name->value = "changed";
    rig.types.clear();
    rig.focus_id("ir");
    rig.key(" ");
    EXPECT_TRUE(rig.saw("click"));
    EXPECT_EQ(name->value, "keep");

    rig.types.clear();
    rig.focus_id("lnk");
    rig.key("Enter");
    EXPECT_TRUE(rig.saw("click"));
    ASSERT_FALSE(rig.opened.empty());
    EXPECT_EQ(rig.opened.back(), "https://example.com/z");

    rig.types.clear();
    rig.opened.clear();
    rig.prevent_type = "keydown";
    rig.focus_id("go");
    rig.key("Enter");
    rig.key(" ");
    EXPECT_FALSE(rig.saw("click"));
    EXPECT_NEAR(root->node->scroll_y, before, 0.5f);
}

TEST(DifftestSelfCheck, LabelActivationFocusesAndClicks) {
    Rig rig;
    ASSERT_TRUE(rig.open(
        "<!DOCTYPE html><html><body>"
        "<label id=\"for-lab\" for=\"box\">Pick</label>"
        "<input id=\"box\" type=\"checkbox\">"
        "<label id=\"wrap\"><span id=\"cap\">Press</span>"
        "<button id=\"inner\" onclick=\"on_inner\">In</button></label>"
        "</body></html>"));
    rig.click_id("for-lab");
    zui::html::Node* box = rig.node("box");
    ASSERT_NE(box, nullptr);
    EXPECT_TRUE(box->checked);
    EXPECT_EQ(rig.focused, box);

    rig.click_id("cap");
    EXPECT_TRUE(rig.saw("click"));
    EXPECT_EQ(rig.focused, rig.node("inner"));
}

// A label's own click handler runs before the labeled control is activated. Album's
// 导入图片 is `<label for="file" onclick="on_import">`; retargeting first dropped that
// handler and the file input's picker stored only a filename.
TEST(DifftestSelfCheck, LabelClickHandlerRunsBeforeFileActivation) {
    Rig rig;
    const char* html =
        "<!DOCTYPE html><html><body>"
        "<label id=\"lab\" for=\"file\" onclick=\"on_import\">\xe5\xaf\xbc\xe5\x85\xa5</label>"
        "<input id=\"file\" type=\"file\">"
        "<label id=\"plain\" for=\"box\">Pick</label>"
        "<input id=\"box\" type=\"checkbox\">"
        "</body></html>";
    ASSERT_TRUE(rig.open(html));
    rig.prevent_name = "on_import";
    rig.click_id("lab");
    EXPECT_EQ(rig.file_picks, 0);
    EXPECT_NE(std::find(rig.names.begin(), rig.names.end(), "on_import"), rig.names.end());

    rig.click_id("plain");
    ASSERT_NE(rig.node("box"), nullptr);
    EXPECT_TRUE(rig.node("box")->checked);
    EXPECT_EQ(rig.file_picks, 0);

    ASSERT_TRUE(rig.open(html));
    rig.file_picks = 0;
    rig.names.clear();
    rig.prevent_name.clear();
    rig.click_id("lab");
    EXPECT_EQ(rig.file_picks, 1);
    EXPECT_NE(std::find(rig.names.begin(), rig.names.end(), "on_import"), rig.names.end());
}

TEST(DifftestSelfCheck, CopyCutPasteDispatchBeforeClipboard) {
    Rig rig;
    ASSERT_TRUE(rig.open(
        "<!DOCTYPE html><html><body>"
        "<input id=\"t\" value=\"hello\" oncopy=\"on_copy\" oncut=\"on_cut\" onpaste=\"on_paste\" "
        "onselect=\"on_select\">"
        "</body></html>"));
    zui::html::Node* field = rig.node("t");
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(field->value, "hello");
    rig.focus_id("t");
    rig.key("a", true);
    EXPECT_TRUE(rig.saw("select"));
    rig.types.clear();
    rig.key("c", true);
    EXPECT_TRUE(rig.saw("copy"));
    EXPECT_EQ(rig.clip, "hello");
    EXPECT_EQ(field->value, "hello");
    EXPECT_EQ(rig.clip_writes, 1);

    rig.key("x", true);
    EXPECT_TRUE(rig.saw("cut"));
    EXPECT_EQ(rig.clip, "hello");
    EXPECT_TRUE(field->value.empty());

    rig.clip = "z";
    rig.key("v", true);
    EXPECT_TRUE(rig.saw("paste"));
    EXPECT_EQ(field->value, "z");

    field->value = "hello";
    field->text = "hello";
    rig.clip = "kept";
    rig.clip_writes = 0;
    rig.prevent_type = "copy";
    rig.key("c", true);
    EXPECT_TRUE(rig.saw("copy"));
    EXPECT_EQ(rig.clip, "kept");
    EXPECT_EQ(rig.clip_writes, 0);
    EXPECT_EQ(field->value, "hello");

    rig.prevent_type = "cut";
    rig.key("x", true);
    EXPECT_EQ(field->value, "hello");
    EXPECT_EQ(rig.clip, "kept");

    rig.prevent_type = "paste";
    rig.clip = "nope";
    rig.key("v", true);
    EXPECT_EQ(field->value, "hello");
}

TEST(DifftestSelfCheck, PointerCaptureAndSelect) {
    Rig rig;
    ASSERT_TRUE(rig.open(
        "<!DOCTYPE html><html><body>"
        "<button id=\"cap\" ongotpointercapture=\"on_got\" onlostpointercapture=\"on_lost\">C</button>"
        "<input id=\"t\" value=\"abcdef\" onselect=\"on_select\">"
        "</body></html>"));
    rig.click_id("cap");
    EXPECT_TRUE(rig.saw("gotpointercapture"));
    EXPECT_TRUE(rig.saw("lostpointercapture"));

    rig.focus_id("t");
    rig.key("a", true);
    EXPECT_TRUE(rig.saw("select"));
    const zui::html::SelectionRange& sel = rig.scene.doc.selection;
    EXPECT_EQ(sel.node, rig.node("t"));
    EXPECT_EQ(sel.start, 0u);
    EXPECT_EQ(sel.end, std::string("abcdef").size());
}

TEST(DifftestSelfCheck, FormSubmitValidationAndCancel) {
    Rig rig;
    ASSERT_TRUE(rig.open(
        "<!DOCTYPE html><html><body>"
        "<form id=\"ok\" onsubmit=\"on_submit\">"
        "<input id=\"name\" value=\"ada\" onkeydown=\"on_key\">"
        "<button id=\"send\" type=\"submit\">Send</button>"
        "</form>"
        "<form id=\"need\" onsubmit=\"on_need\">"
        "<input id=\"req\" required oninvalid=\"on_invalid\">"
        "<input id=\"mail\" type=\"email\" value=\"not-an-email\" oninvalid=\"on_mail\">"
        "<button id=\"bad\" type=\"submit\">Bad</button>"
        "</form>"
        "</body></html>"));
    rig.focus_id("name");
    rig.key("Enter");
    EXPECT_TRUE(rig.saw("submit"));
    EXPECT_FALSE(rig.saw("invalid"));

    rig.types.clear();
    rig.focus_id("req");
    rig.key("Enter");
    EXPECT_TRUE(rig.saw("invalid"));
    EXPECT_FALSE(rig.saw("submit"));
    EXPECT_EQ(rig.node("req")->attrs["aria-invalid"], "true");
    EXPECT_EQ(rig.node("mail")->attrs["aria-invalid"], "true");

    rig.node("req")->value = "ada";
    rig.node("req")->attrs.erase("aria-invalid");
    rig.node("mail")->attrs.erase("aria-invalid");
    rig.types.clear();
    rig.focus_id("mail");
    rig.key("Enter");
    EXPECT_TRUE(rig.saw("invalid"));
    EXPECT_FALSE(rig.saw("submit"));
    EXPECT_EQ(rig.node("mail")->attrs["aria-invalid"], "true");

    rig.node("mail")->value = "ada@example.com";
    rig.types.clear();
    rig.prevent_type = "submit";
    rig.focus_id("req");
    rig.key("Enter");
    EXPECT_TRUE(rig.saw("submit"));
    EXPECT_TRUE(rig.cancelled);
}

TEST(DifftestSelfCheck, ContentEditableAndComposition) {
    Rig rig;
    ASSERT_TRUE(rig.open(
        "<!DOCTYPE html><html><body>"
        "<div id=\"edit\" contenteditable=\"true\" onbeforeinput=\"on_before\" oninput=\"on_input\" "
        "onselect=\"on_select\">Hi</div>"
        "<textarea id=\"area\" oncompositionstart=\"on_cs\" oncompositionupdate=\"on_cu\" "
        "oncompositionend=\"on_ce\"></textarea>"
        "</body></html>"));
    zui::html::Node* edit = rig.node("edit");
    ASSERT_NE(edit, nullptr);
    rig.focus_id("edit");
    edit->caret = 2;
    zui::input::EditInput typed;
    typed.text = "!";
    ASSERT_TRUE(rig.interaction->apply_editing(typed));
    EXPECT_EQ(edit->value, "Hi!");
    EXPECT_TRUE(rig.saw("beforeinput"));
    EXPECT_TRUE(rig.saw("input"));

    zui::input::EditInput back;
    back.backspace = 1;
    ASSERT_TRUE(rig.interaction->apply_editing(back));
    EXPECT_EQ(edit->value, "Hi");

    zui::input::EditInput line;
    line.enter = true;
    ASSERT_TRUE(rig.interaction->apply_editing(line));
    EXPECT_NE(edit->value.find('\n'), std::string::npos);

    edit->value = "Hi";
    edit->text = "Hi";
    edit->caret = 2;
    edit->format.clear();
    rig.key("a", true);
    EXPECT_TRUE(rig.saw("select"));
    const zui::html::SelectionRange& edited = rig.scene.doc.selection;
    EXPECT_EQ(edited.node, edit);
    EXPECT_EQ(edited.start, 0u);
    EXPECT_EQ(edited.end, std::string("Hi").size());
    rig.key("b", true);
    EXPECT_TRUE(has_tag(edit, "b"));
    rig.key("i", true);
    EXPECT_TRUE(has_tag(edit, "i"));
    EXPECT_EQ(edit->value, "Hi");

    const std::string kept = edit->value;
    rig.prevent_type = "beforeinput";
    zui::input::EditInput blocked;
    blocked.text = "Z";
    EXPECT_FALSE(rig.interaction->apply_editing(blocked));
    EXPECT_EQ(edit->value, kept);

    zui::input::EditInput composing;
    composing.enter = true;
    composing.composing = true;
    EXPECT_FALSE(rig.interaction->apply_editing(composing));
    EXPECT_EQ(edit->value, kept);

    rig.prevent_type.clear();
    rig.types.clear();
    rig.focus_id("area");
    rig.interaction->composition_event(1);
    rig.interaction->composition_event(2);
    rig.interaction->composition_event(3);
    EXPECT_TRUE(rig.saw("compositionstart"));
    EXPECT_TRUE(rig.saw("compositionupdate"));
    EXPECT_TRUE(rig.saw("compositionend"));

    rig.focus_id("area");
    zui::html::Node* area = rig.node("area");
    ASSERT_NE(area, nullptr);
    auto publish = [&](zui::backends::ImeComposer& composer, std::string echo) {
        zui::backends::TextInputFrame step;
        step.text = std::move(echo);
        composer.apply(step);
        zui::input::EditInput edit;
        edit.text = step.text;
        edit.composing = step.composing;
        edit.composition = step.composition;
        rig.interaction->apply_editing(edit);
        return step;
    };

    zui::backends::ImeComposer live;
    zui::backends::ImeSample sample;
    sample.comp = "n";
    EXPECT_EQ(live.observe(sample), zui::backends::ImeAction::Preedit);
    EXPECT_TRUE(live.composing());
    zui::backends::TextInputFrame frame = publish(live, "");
    EXPECT_EQ(frame.composition_phase, 1);
    EXPECT_TRUE(frame.text.empty());
    EXPECT_EQ(frame.composition, "n");
    EXPECT_EQ(area->value, "n");
    EXPECT_EQ(live.observe(sample), zui::backends::ImeAction::None);
    publish(live, "");
    EXPECT_EQ(area->value, "n");

    sample.comp = "ni";
    EXPECT_EQ(live.observe(sample), zui::backends::ImeAction::Preedit);
    frame = publish(live, "");
    EXPECT_EQ(frame.composition_phase, 2);
    EXPECT_EQ(frame.composition, "ni");
    EXPECT_EQ(area->value, "ni");

    sample.enter = true;
    EXPECT_EQ(live.observe(sample), zui::backends::ImeAction::Commit);
    EXPECT_FALSE(live.composing());
    zui::backends::KeyPress press;
    live.annotate(press);
    EXPECT_FALSE(press.composing);
    frame = publish(live, "\r");
    EXPECT_EQ(frame.composition_phase, 3);
    EXPECT_EQ(frame.text, "ni");
    EXPECT_EQ(frame.text.find('\r'), std::string::npos);
    EXPECT_EQ(frame.text.find('\n'), std::string::npos);
    EXPECT_TRUE(frame.composition.empty());
    EXPECT_EQ(area->value, "ni");
    EXPECT_EQ(area->value.find('\n'), std::string::npos);
    EXPECT_EQ(area->value.find('\r'), std::string::npos);

    sample.enter = false;
    EXPECT_EQ(live.observe(sample), zui::backends::ImeAction::None);
    frame = publish(live, "");
    EXPECT_TRUE(frame.text.empty());
    EXPECT_FALSE(frame.composing);
    EXPECT_EQ(area->value, "ni");

    area->value.clear();
    area->text.clear();
    area->caret = 0;
    zui::backends::ImeComposer committed;
    zui::backends::ImeSample result;
    result.comp = "ni";
    EXPECT_EQ(committed.observe(result), zui::backends::ImeAction::Preedit);
    publish(committed, "");
    EXPECT_EQ(area->value, "ni");
    result.result = "你";
    EXPECT_EQ(committed.observe(result), zui::backends::ImeAction::Commit);
    frame = publish(committed, "你");
    EXPECT_EQ(frame.text, "你");
    EXPECT_FALSE(frame.composing);
    EXPECT_EQ(area->value, "你");
    EXPECT_EQ(committed.observe(result), zui::backends::ImeAction::None);
    publish(committed, "");
    EXPECT_EQ(area->value, "你");
}

TEST(DifftestSelfCheck, SvgSoftwareRasterAndStillPngJpeg) {
    const fs::path svg = fs::temp_directory_path() / "zui-browser-raster.svg";
    {
        std::ofstream out(svg);
        out << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8\" height=\"8\">"
               "<rect width=\"8\" height=\"8\" fill=\"#ff0000\"/></svg>";
    }
    const zui::image::Bitmap* bitmap = zui::image::load(svg.string());
    ASSERT_NE(bitmap, nullptr);
    EXPECT_EQ(bitmap->width, 8);
    EXPECT_EQ(bitmap->height, 8);
    ASSERT_NE(bitmap->rgba, nullptr);
    bool painted = false;
    const std::size_t bytes =
        static_cast<std::size_t>(bitmap->width) * static_cast<std::size_t>(bitmap->height) * 4;
    for (std::size_t i = 0; i < bytes; i += 4) {
        if (bitmap->rgba[i + 3] != 0) {
            painted = true;
            break;
        }
    }
    EXPECT_TRUE(painted);

    const std::string png = std::string(ZUI_SOURCE_DIR) + "/tests/fixtures/dot.png";
    const zui::image::Bitmap* png_bitmap = zui::image::load(png);
    ASSERT_NE(png_bitmap, nullptr);
    EXPECT_GT(png_bitmap->width, 0);
    EXPECT_GT(png_bitmap->height, 0);

    zui::image::Raster src;
    src.width = 2;
    src.height = 2;
    src.rgba = {10, 20, 30, 255, 10, 20, 30, 255, 10, 20, 30, 255, 10, 20, 30, 255};
    std::vector<std::uint8_t> encoded;
    std::string error;
    const zui::image::Driver* jpeg = zui::image::DriverManager::instance().find("jpg");
    ASSERT_NE(jpeg, nullptr);
    ASSERT_TRUE(jpeg->encode(src, encoded, error)) << error;
    const fs::path jpeg_path = fs::temp_directory_path() / "zui-browser-raster.jpg";
    {
        std::ofstream out(jpeg_path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(encoded.data()),
                  static_cast<std::streamsize>(encoded.size()));
    }
    const zui::image::Bitmap* jpeg_bitmap = zui::image::load(jpeg_path.string());
    ASSERT_NE(jpeg_bitmap, nullptr);
    EXPECT_EQ(jpeg_bitmap->width, 2);
    EXPECT_EQ(jpeg_bitmap->height, 2);
}
