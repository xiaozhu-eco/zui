#include <gtest/gtest.h>

#include "css/engine/engine.h"
#include "html/document.h"
#include "layout/engine.h"

#include "log/log.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>

TEST(LayoutHello, Page) {
    zui::html::Document doc;
    std::string error;
    if (!zui::html::load_file(ZUI_SOURCE_DIR "/example/tests/hello/hello.html", doc, error)) {
        FAIL() << error << "\n";
    }
    if (doc.root == nullptr || doc.root->tag != "html") {
        FAIL() << "root is not html\n";
    }
    zui::css::Engine css;
    if (!css.load_sheets(doc, error)) {
        FAIL() << error << "\n";
    }
    css.cascade(doc);

    zui::layout::FreeTypeTextMetrics metrics;
    if (!metrics.ready()) {
        FAIL() << "FreeType did not load a system font\n";
    }
    zui::layout::LayoutEngine layout(metrics);
    const zui::layout::Viewport viewport{800.0f, 600.0f};
    layout.layout(doc, viewport);

    zui::layout::Box* html = layout.root();
    if (html == nullptr || std::fabs(html->geo.width - 800.0f) > 0.5f ||
        std::fabs(html->geo.height - 600.0f) > 0.5f) {
        FAIL() << "html box " << (html ? html->geo.width : 0) << "x"
               << (html ? html->geo.height : 0) << "\n";
    }
    zui::html::Node* title_n = zui::html::find(doc, "heading");
    zui::html::Node* save_n = zui::html::find(doc, "save");
    zui::layout::Box* title = layout.principal_box(title_n);
    zui::layout::Box* save = layout.principal_box(save_n);
    if (title == nullptr || save == nullptr) {
        FAIL() << "missing principal boxes\n";
    }
    if (save->geo.world_y <= title->geo.world_y) {
        FAIL() << "flex column did not stack: title.y=" << title->geo.world_y
               << " save.y=" << save->geo.world_y << "\n";
    }
    if (save->geo.height < 20.0f) {
        FAIL() << "button height too small: " << save->geo.height << "\n";
    }
    zui::layout::Box* cards = nullptr;
    zui::layout::Box* card = nullptr;
    std::function<void(zui::layout::Box*)> find_cards = [&](zui::layout::Box* b) {
        if (b->node != nullptr) {
            if (zui::html::has_class_token(b->node->class_name, "cards")) {
                cards = b;
            } else if (card == nullptr && zui::html::has_class_token(b->node->class_name, "card")) {
                card = b;
            }
        }
        for (zui::layout::Box* ch : b->children) {
            find_cards(ch);
        }
    };
    find_cards(html);
    if (cards == nullptr || card == nullptr) {
        FAIL() << "hello cards missing\n";
    }
    // A card is 72px tall: 16px padding twice plus a 40px stat line (28px text) over a 17px label.
    // Chrome reports the same 256,185,260.3,72 for this card at a 1100x720 client area.
    if (std::fabs(card->geo.height - 72.0f) > 2.0f ||
        std::fabs(cards->geo.height - card->geo.height) > 1.0f) {
        FAIL() << "auto row height: cards=" << cards->geo.width << "x" << cards->geo.height
               << " card=" << card->geo.width << "x" << card->geo.height << "\n";
    }
    zui::layout::Box* content = nullptr;
    float projects_base = -1;
    float stat_base = -1;
    std::function<void(zui::layout::Box*)> find_align = [&](zui::layout::Box* b) {
        if (b->node != nullptr && zui::html::has_class_token(b->node->class_name, "content")) {
            content = b;
        }
        if (b->parent != nullptr && b->parent->node != nullptr) {
            const float base = b->geo.world_y + b->geo.baseline;
            if (b->text == "Projects" &&
                zui::html::has_class_token(b->parent->node->class_name, "label")) {
                projects_base = base;
            }
            if (b->text == "12" && b->parent->node->id == "stat_projects") {
                stat_base = base;
            }
        }
        for (zui::layout::Box* ch : b->children) {
            find_align(ch);
        }
    };
    find_align(html);
    if (content == nullptr || content->geo.world_x + content->geo.width > viewport.width + 0.5f) {
        if (content == nullptr) {
            FAIL() << "content spills past the viewport\n";
        }
        FAIL() << "content spills past the viewport: right="
               << (content->geo.world_x + content->geo.width) << "\n";
    }
    if (projects_base < 0 || stat_base < 0 || std::fabs(projects_base - stat_base) > 1.0f) {
        FAIL() << "label baseline " << projects_base << " stat baseline " << stat_base << "\n";
    }
    float used_h = 0;
    for (const zui::layout::Box* child : content->children) {
        used_h = std::max(used_h, child->geo.y + child->geo.height + child->geo.margin.bottom);
    }
    const float view_h =
        content->geo.height - content->geo.border.vertical() - content->geo.padding.vertical();
    if (used_h <= view_h + 1.0f) {
        FAIL() << "content does not overflow: used=" << used_h << " view=" << view_h << "\n";
    }
    auto* nav_home = layout.principal_box(zui::html::find(doc, "nav_home"));
    auto* nav_projects = layout.principal_box(zui::html::find(doc, "nav_projects"));
    auto* nav_settings = layout.principal_box(zui::html::find(doc, "nav_settings"));
    auto* name = layout.principal_box(zui::html::find(doc, "name"));
    auto* inc = layout.principal_box(zui::html::find(doc, "inc"));
    if (nav_home == nullptr || nav_projects == nullptr || nav_settings == nullptr ||
        name == nullptr || inc == nullptr) {
        FAIL() << "missing nav or form boxes\n";
    }
    // .sidebar is width:200px content-box. Column stretch makes the three buttons share it.
    if (std::fabs(nav_home->geo.width - 200.0f) > 1.0f ||
        std::fabs(nav_projects->geo.width - nav_home->geo.width) > 0.5f ||
        std::fabs(nav_settings->geo.width - nav_home->geo.width) > 0.5f) {
        FAIL() << "sidebar buttons are not equal width: home=" << nav_home->geo.width
               << " projects=" << nav_projects->geo.width
               << " settings=" << nav_settings->geo.width << "\n";
    }
    // One line, 10px padding, 2px border. Wrapping "+ Project" at min-content width used to
    // stretch the whole form row to two lines.
    if (nav_home->geo.height > 46.0f || std::fabs(name->geo.height - nav_home->geo.height) > 1.0f ||
        std::fabs(save->geo.height - nav_home->geo.height) > 1.0f ||
        std::fabs(inc->geo.height - nav_home->geo.height) > 1.0f) {
        FAIL() << "form controls are not one line tall: nav=" << nav_home->geo.height
               << " name=" << name->geo.height << " save=" << save->geo.height
               << " inc=" << inc->geo.height << "\n";
    }
    ZUI_FILE_INFO("html={}x{}", html->geo.width, html->geo.height);
    ZUI_FILE_INFO("title world=({},{}) {}x{}", title->geo.world_x, title->geo.world_y,
                  title->geo.width, title->geo.height);
    ZUI_FILE_INFO("save world=({},{}) {}x{}", save->geo.world_x, save->geo.world_y,
                  save->geo.width, save->geo.height);
    ZUI_FILE_INFO("nav {}x{} name={}x{} inc={}x{}", nav_home->geo.width, nav_home->geo.height,
                  name->geo.width, name->geo.height, inc->geo.width, inc->geo.height);
    ZUI_FILE_INFO("cards world=({},{}) {}x{} card={}x{}", cards->geo.world_x, cards->geo.world_y,
                  cards->geo.width, cards->geo.height, card->geo.width, card->geo.height);
}
