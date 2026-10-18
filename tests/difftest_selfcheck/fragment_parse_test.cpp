// MARKUP: load_fragment uses the HTML fragment parsing algorithm with context element body.
// A full-document parse of fragment-context.html files <meta>, <title>, <link>, and <style>
// under <head>, so the body's children are only #frag-note. Body-context fragment parsing
// ignores <head> and inserts those elements into the fragment. Attaching under a <table>
// must still use body context: a stray <td> is ignored, not turned into a cell.

#include "selfcheck_scene.h"

#include <filesystem>
#include <string>

using namespace difftest_selfcheck;

namespace {

std::string child_tags(const zui::html::Node* node) {
    std::string out;
    if (node == nullptr) {
        return out;
    }
    for (const auto& child : node->children) {
        if (!out.empty()) {
            out.push_back(' ');
        }
        out += child->tag;
        if (!child->id.empty()) {
            out.push_back('#');
            out += child->id;
        }
    }
    return out;
}

const zui::html::Node* child_with_tag(const zui::html::Node* node, std::string_view tag) {
    if (node == nullptr) {
        return nullptr;
    }
    for (const auto& child : node->children) {
        if (child->tag == tag) {
            return child.get();
        }
    }
    return nullptr;
}

}  // namespace

TEST(FragmentParse, BodyContextKeepsHeadElements) {
    std::string error;
    zui::html::Document as_document;
    ASSERT_TRUE(zui::html::load_file(fixture("fragment-context.html"), as_document, error))
        << error;
    EXPECT_EQ(as_document.title, "Fragment title");
    zui::html::Node* doc_body = zui::html::find_tag(as_document.root.get(), "body");
    zui::html::Node* doc_meta = zui::html::find(as_document, "frag-meta");
    zui::html::Node* doc_note = zui::html::find(as_document, "frag-note");
    ASSERT_NE(doc_body, nullptr);
    ASSERT_NE(doc_meta, nullptr);
    ASSERT_NE(doc_note, nullptr);
    EXPECT_EQ(doc_note->parent, doc_body);
    bool meta_under_body = false;
    for (zui::html::Node* walk = doc_meta; walk != nullptr; walk = walk->parent) {
        if (walk == doc_body) {
            meta_under_body = true;
        }
    }
    EXPECT_FALSE(meta_under_body)
        << "a full document parse still files <meta> under <head>, not <body>";

    zui::html::Document host;
    ASSERT_TRUE(zui::html::load_file(fixture("fragment-host.html"), host, error)) << error;
    EXPECT_TRUE(host.title.empty());
    zui::html::Node* slot = zui::html::find(host, "slot");
    zui::html::Node* table = zui::html::find(host, "host-table");
    ASSERT_NE(slot, nullptr);
    ASSERT_NE(table, nullptr);
    const size_t sheets_before = host.stylesheets.size();

    ASSERT_TRUE(zui::html::attach_fragment(host, slot, fixture("fragment-context.html"), error))
        << error;

    EXPECT_TRUE(host.title.empty()) << "a fragment must not replace the host document title";
    EXPECT_EQ(zui::html::find(host, "host-table"), table);

    zui::html::Node* meta = zui::html::find(host, "frag-meta");
    zui::html::Node* note = zui::html::find(host, "frag-note");
    ASSERT_NE(meta, nullptr)
        << "body-context fragment parsing dropped <meta id=frag-meta>; children: "
        << child_tags(slot);
    ASSERT_NE(note, nullptr) << "children: " << child_tags(slot);
    EXPECT_EQ(meta->parent, slot) << "children: " << child_tags(slot);
    EXPECT_EQ(note->parent, slot) << "children: " << child_tags(slot);
    EXPECT_EQ(meta->tag, "meta");
    EXPECT_EQ(meta->attrs.at("content"), "body-context");
    EXPECT_EQ(note->tag, "div");
    EXPECT_EQ(note->text, "noted");
    std::string elements;
    for (const auto& child : slot->children) {
        if (child->tag == "#text") {
            EXPECT_EQ(child->text, " ")
                << "inter-element whitespace should collapse to one space, children: "
                << child_tags(slot);
            continue;
        }
        if (!elements.empty()) {
            elements.push_back(' ');
        }
        elements += child->tag;
        if (!child->id.empty()) {
            elements.push_back('#');
            elements += child->id;
        }
    }
    EXPECT_EQ(elements, "meta#frag-meta title link style div#frag-note")
        << "children: " << child_tags(slot);

    const zui::html::Node* title = child_with_tag(slot, "title");
    ASSERT_NE(title, nullptr);
    EXPECT_EQ(title->text, "Fragment title");
    const zui::html::Node* link = child_with_tag(slot, "link");
    ASSERT_NE(link, nullptr);
    EXPECT_EQ(link->attrs.at("href"), "fragment-context.css");
    EXPECT_EQ(child_with_tag(slot, "head"), nullptr);
    EXPECT_EQ(child_with_tag(slot, "body"), nullptr);

    ASSERT_GE(host.stylesheets.size(), sheets_before + 2);
    bool saw_link = false;
    bool saw_style = false;
    for (size_t i = sheets_before; i < host.stylesheets.size(); ++i) {
        const zui::html::Document::StylesheetSource& sheet = host.stylesheets[i];
        if (!sheet.href.empty()) {
            const std::filesystem::path href(sheet.href);
            if (href.filename() == "fragment-context.css") {
                saw_link = true;
                EXPECT_TRUE(href.is_absolute());
                EXPECT_NE(sheet.href, "fragment-context.css");
            }
        }
        if (sheet.text.find("17px") != std::string::npos) {
            saw_style = true;
        }
    }
    EXPECT_TRUE(saw_link) << "the fragment <link> href was not resolved next to the file";
    EXPECT_TRUE(saw_style) << "the fragment <style> text did not reach the stylesheet list";
}

TEST(FragmentParse, ContextStaysBodyUnderATable) {
    std::string error;
    zui::html::Document host;
    ASSERT_TRUE(zui::html::load_file(fixture("fragment-host.html"), host, error)) << error;
    zui::html::Node* table = zui::html::find(host, "host-table");
    ASSERT_NE(table, nullptr);

    ASSERT_TRUE(zui::html::attach_fragment(host, table, fixture("fragment-cell.html"), error))
        << error;

    EXPECT_EQ(zui::html::find(host, "stray-cell"), nullptr)
        << "a <td> in a body-context fragment is ignored; children: " << child_tags(table);
    EXPECT_EQ(child_with_tag(table, "td"), nullptr);
    EXPECT_EQ(child_with_tag(table, "tbody"), nullptr);
    EXPECT_EQ(child_with_tag(table, "tr"), nullptr);
}
