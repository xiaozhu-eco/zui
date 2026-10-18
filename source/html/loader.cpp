// The DOM loader: lexbor HTML -> `Node` tree, plus the `<link>` / `<style>` stylesheet walk and
// the `<template>` fragment attach. `DocumentLoader` owns the workflow; the conversion helpers
// are private to this translation unit.

#include "html/document.h"
#include "html/document_internal.h"
#include "perf/perf_budget.h"
#include "trace/trace.h"

#include <cstdlib>
#include <lexbor/dom/interface.h>
#include <lexbor/dom/interfaces/attr.h>
#include <lexbor/dom/interfaces/document.h>
#include <lexbor/dom/interfaces/element.h>
#include <lexbor/dom/interfaces/node.h>
#include <lexbor/html/interface.h>
#include <lexbor/html/interfaces/document.h>
#include <lexbor/html/interfaces/template_element.h>
#include <lexbor/html/parser.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace zui {
namespace html {

bool whitespace_only(std::string_view s) {
    for (char c : s) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return true;
}

namespace {

std::string to_string(const lxb_char_t* data, size_t len) {
    if (data == nullptr || len == 0) {
        return {};
    }
    return std::string(reinterpret_cast<const char*>(data), len);
}

std::string attr(lxb_dom_element_t* el, const char* name) {
    size_t len = 0;
    const lxb_char_t* v = lxb_dom_element_get_attribute(
        el, reinterpret_cast<const lxb_char_t*>(name), std::char_traits<char>::length(name), &len);
    return to_string(v, len);
}


void append_text(std::string& dst, std::string_view chunk) {
    if (chunk.empty()) {
        return;
    }
    // Preserve author spaces (needed for white-space:pre / pre-wrap / break-spaces). Pure
    // whitespace-only inter-element nodes are filtered before they reach here.
    if (!dst.empty() && !std::isspace(static_cast<unsigned char>(dst.back())) &&
        !std::isspace(static_cast<unsigned char>(chunk.front()))) {
        dst.push_back(' ');
    }
    dst.append(chunk.data(), chunk.size());
}

void convert_children(lxb_dom_node_t* first, Node* node, Document& doc);

std::unique_ptr<Node> convert_element(lxb_dom_element_t* el, Document& doc) {
    perf::note_html_node();
    auto node = std::make_unique<Node>();
    size_t tag_len = 0;
    const lxb_char_t* tag = lxb_dom_element_local_name(el, &tag_len);
    node->tag = to_string(tag, tag_len);
    node->id = attr(el, "id");
    node->class_name = attr(el, "class");
    node->value = attr(el, "value");
    node->disabled =
        lxb_dom_element_has_attribute(el, reinterpret_cast<const lxb_char_t*>("disabled"), 8);
    node->checked =
        lxb_dom_element_has_attribute(el, reinterpret_cast<const lxb_char_t*>("checked"), 7);
    node->visible =
        !lxb_dom_element_has_attribute(el, reinterpret_cast<const lxb_char_t*>("hidden"), 6);

    for (lxb_dom_attr_t* a = lxb_dom_element_first_attribute(el); a != nullptr;
         a = lxb_dom_element_next_attribute(a)) {
        size_t nlen = 0;
        size_t vlen = 0;
        const lxb_char_t* n = lxb_dom_attr_qualified_name(a, &nlen);
        const lxb_char_t* v = lxb_dom_attr_value(a, &vlen);
        node->attrs[to_string(n, nlen)] = to_string(v, vlen);
    }
    if (node->tag == "option") {
        node->default_selected = node->attrs.find("selected") != node->attrs.end();
    } else if (is_checkbox(node.get()) || is_radio(node.get())) {
        node->default_selected = node->attrs.find("checked") != node->attrs.end();
    }

    lxb_dom_node_t* first = lxb_dom_node_first_child(lxb_dom_interface_node(el));
    if (node->tag == "template") {
        auto* tmpl = lxb_html_interface_template(el);
        if (tmpl != nullptr && tmpl->content != nullptr) {
            first = lxb_dom_node_first_child(lxb_dom_interface_node(tmpl->content));
        }
    }
    convert_children(first, node.get(), doc);

    if (node->tag == "input" || node->tag == "textarea") {
        auto type = node->attrs.find("type");
        const bool checkbox =
            node->tag == "input" && type != node->attrs.end() && type->second == "checkbox";
        const bool radio =
            node->tag == "input" && type != node->attrs.end() && type->second == "radio";
        const bool range =
            node->tag == "input" && type != node->attrs.end() && type->second == "range";
        const bool file =
            node->tag == "input" && type != node->attrs.end() && type->second == "file";
        if (checkbox) {
            // EVENT.md freezes the checkbox payload as "true" / "false".
            node->value = node->checked ? "true" : "false";
            node->text.clear();
        } else if (radio) {
            // HTML: the value attribute is the submission value and does not
            // follow checked. A missing attribute defaults to "on".
            if (node->attrs.find("value") == node->attrs.end()) {
                node->value = "on";
            }
            node->text.clear();
        } else if (range) {
            // Keep numeric value for paint; do not mirror into text (no glyph run).
            // Missing value → paint uses the HTML midpoint of min/max.
            node->text.clear();
            node->caret = 0;
        } else if (file) {
            // Filename lives in value; painter draws the button + label (no text run).
            node->text.clear();
            node->caret = 0;
        } else {
            if (node->value.empty() && !node->text.empty()) {
                node->value = node->text;
            }
            if (node->text.empty() && !node->value.empty()) {
                node->text = node->value;
            }
            node->caret = static_cast<int>(node->value.size());
        }
    }
    if (node->tag == "progress") {
        // HTML: missing value is indeterminate; paint treats empty as 0 fill.
        if (node->attrs.find("value") == node->attrs.end()) {
            node->value.clear();
        }
        node->text.clear();
    }
    if (node->tag == "select") {
        select_init(node.get());
    }
    // Declarative `<dialog open>` shows the dialog *non-modally*: it keeps `position: absolute`
    // from the UA sheet and paints no `::backdrop` (HTML: the top layer, `:modal` and the backdrop
    // all belong to `showModal()`). The pinned browser measures exactly that - `dialog#dlg` comes
    // back `position: absolute` and `ua-dialog-modal`'s golden has no dim layer.

    if (!node->id.empty()) {
        doc.ids[node->id] = node.get();
    }
    return node;
}

std::string trim_copy(std::string_view chunk) {
    size_t begin = 0;
    size_t end = chunk.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(chunk[begin]))) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(chunk[end - 1]))) {
        --end;
    }
    return std::string(chunk.substr(begin, end - begin));
}

void convert_children(lxb_dom_node_t* first, Node* node, Document& doc) {
    struct Piece {
        std::unique_ptr<Node> element;
        std::string text;
        bool is_text = false;
    };
    bool pending_space = false;
    bool saw_content = false;
    std::vector<Piece> ordered;
    for (lxb_dom_node_t* child = first; child != nullptr; child = lxb_dom_node_next(child)) {
        if (child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            auto ch = convert_element(lxb_dom_interface_element(child), doc);
            ch->parent = node;
            // HTML inter-element white space collapses to one space. Whether that space survives is
            // `display`'s call, not the tag's: `<div style="display:inline-block">` is inline level
            // and keeps it, a block-level sibling does not. `display` is not known yet here, so the
            // node is always emitted and the box tree drops the runs that generate no box (see
            // `BoxTree::wrap_inlines`). Deciding by tag name lost the space between inline-block
            // chips, which put every following box one space (~3.6px) too far inline.
            if (pending_space && saw_content) {
                Piece space;
                space.is_text = true;
                space.text = " ";
                ordered.push_back(std::move(space));
            }
            pending_space = false;
            saw_content = true;
            Piece piece;
            piece.element = std::move(ch);
            ordered.push_back(std::move(piece));
        } else if (child->type == LXB_DOM_NODE_TYPE_TEXT) {
            size_t len = 0;
            lxb_char_t* text = lxb_dom_node_text_content(child, &len);
            std::string chunk = to_string(text, len);
            if (whitespace_only(chunk)) {
                pending_space = true;
                node->whitespace_child = true;
                continue;
            }
            Piece piece;
            piece.is_text = true;
            // Keep leading/trailing spaces in mixed text nodes so CSS white-space:pre /
            // pre-wrap / break-spaces can see them. Pure whitespace nodes still collapse
            // through pending_space below (HTML inter-element whitespace).
            piece.text = std::move(chunk);
            if (pending_space && !ordered.empty() && !piece.text.empty()) {
                piece.text.insert(piece.text.begin(), ' ');
            }
            pending_space = false;
            saw_content = true;
            if (!piece.text.empty()) {
                ordered.push_back(std::move(piece));
            }
        }
    }
    // A trailing whitespace-only node is not a flex item and is not kept as a child, but
    // Chrome's Range from the first text node to that trailing node still covers the
    // elements in between. Remember it so the line trace can emit those rects.
    if (pending_space) {
        node->trailing_whitespace = true;
    }

    bool only_text = !ordered.empty();
    for (const Piece& piece : ordered) {
        if (!piece.is_text) {
            only_text = false;
            break;
        }
    }
    if (only_text) {
        for (const Piece& piece : ordered) {
            append_text(node->text, piece.text);
        }
        return;
    }
    for (Piece& piece : ordered) {
        if (piece.is_text) {
            auto text = std::make_unique<Node>();
            perf::note_html_node();
            text->parent = node;
            text->tag = "#text";
            text->text = std::move(piece.text);
            node->children.push_back(std::move(text));
        } else if (piece.element != nullptr) {
            node->children.push_back(std::move(piece.element));
        }
    }
}

/// Collects the author stylesheets in **document order**. A `<style>` element and a
/// `<link rel="stylesheet">` both count, in `<head>` or in `<body>`: the previous walk only looked
/// at `<head>` children and only for `link`, so `<style>` blocks (the usual shape of a single-file
/// demo, and of a page that ships its CSS inline) styled nothing at all. The cascade applies the
/// list in order, which is why hrefs and inline text share one vector.
void collect_styles(lxb_dom_node_t* node, Document& doc) {
    for (; node != nullptr; node = lxb_dom_node_next(node)) {
        if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            lxb_dom_element_t* el = lxb_dom_interface_element(node);
            size_t tag_len = 0;
            const lxb_char_t* tag = lxb_dom_element_local_name(el, &tag_len);
            const std::string name = to_string(tag, tag_len);
            if (iequals(name, "style")) {
                size_t text_len = 0;
                lxb_char_t* text = lxb_dom_node_text_content(node, &text_len);
                if (text != nullptr && text_len != 0) {
                    Document::StylesheetSource sheet;
                    sheet.text.assign(reinterpret_cast<const char*>(text), text_len);
                    doc.stylesheets.push_back(std::move(sheet));
                }
                continue;  // A `<style>` element's children are the CSS text, not more markup.
            }
            if (iequals(name, "link") && iequals(attr(el, "rel"), "stylesheet")) {
                std::string href = attr(el, "href");
                if (!href.empty()) {
                    Document::StylesheetSource sheet;
                    sheet.href = std::move(href);
                    doc.stylesheets.push_back(std::move(sheet));
                }
            }
        }
        collect_styles(lxb_dom_node_first_child(node), doc);
    }
}

}  // namespace

bool DocumentLoader::load(std::string_view path, std::string& error) {
    Document& out = doc_;
    Document next;
    next.source_path.assign(path.begin(), path.end());

    std::ifstream in(next.source_path, std::ios::binary);
    if (!in) {
        error = "cannot open HTML file";
        return false;
    }
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xef &&
        static_cast<unsigned char>(bytes[1]) == 0xbb &&
        static_cast<unsigned char>(bytes[2]) == 0xbf) {
        bytes.erase(0, 3);
    }

    lxb_html_document_t* html = lxb_html_document_create();
    if (html == nullptr) {
        error = "lexbor document create failed";
        return false;
    }
    {
        trace::Zone zone("html.parse", trace::Category::Parse);
        // The scripting flag decides how `<noscript>` is parsed (HTML's "in body" insertion mode):
        // enabled, its content is one raw-text child that the browser does not render. The engine
        // never runs scripts, but every page it loads is compared against a browser that does, so
        // the flag is on - Chrome then reports the element itself as an ordinary inline box
        // (measured: `build/noscript_probe.html` and `example-zdb`'s welcome pane).
        lxb_html_document_scripting_set(html, true);
        const lxb_status_t status = lxb_html_document_parse(
            html, reinterpret_cast<const lxb_char_t*>(bytes.data()), bytes.size());
        if (status != LXB_STATUS_OK) {
            lxb_html_document_destroy(html);
            error = "HTML parse failed";
            return false;
        }
    }

    // Turning the lexbor DOM into the engine's own `html::Node` tree, title, and stylesheet list.
    // This is a separate zone from `html.parse`: the lexbor tree building and the engine's copy are
    // different costs, and only the second moves when the `<style>` / `<link>` walk changes.
    {
        trace::Zone zone("html.build", trace::Category::Parse);
        size_t title_len = 0;
        const lxb_char_t* title = lxb_html_document_title(html, &title_len);
        next.title = to_string(title, title_len);

        collect_styles(lxb_dom_interface_node(lxb_dom_document_element(&html->dom_document)), next);

        lxb_dom_element_t* root_el = lxb_dom_document_element(&html->dom_document);
        if (root_el == nullptr) {
            lxb_html_document_destroy(html);
            error = "HTML document has no document element";
            return false;
        }
        next.root = convert_element(root_el, next);
        bind_document_damage(next);
    }

    lxb_html_document_destroy(html);
    out = std::move(next);
    error.clear();
    return true;
}

bool DocumentLoader::attach_fragment(Node* into, std::string_view path, std::string& error,
                                     const std::function<void(Node*)>& before_remove,
                                     bool replace_children) {
    Document& host = doc_;
    if (into == nullptr) {
        error = "fragment parent missing";
        return false;
    }
    // MARKUP: a fragment (a dialog) uses the HTML fragment parsing algorithm with context
    // element `body`. That is not a full document parse followed by taking the `<body>`
    // children: in "in body", `<head>` is ignored and `<meta>` / `<title>` / `<link>` /
    // `<style>` are inserted into the fragment, while a document parse files them under
    // `<head>`. The context stays `body` even when `into` is a table or another tag.
    Document frag;
    frag.source_path.assign(path.begin(), path.end());
    std::ifstream in(frag.source_path, std::ios::binary);
    if (!in) {
        error = "cannot open HTML file";
        return false;
    }
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xef &&
        static_cast<unsigned char>(bytes[1]) == 0xbb &&
        static_cast<unsigned char>(bytes[2]) == 0xbf) {
        bytes.erase(0, 3);
    }

    // Owns the lexbor parser and the fragment document. The document is original (created
    // with a null owner), so the fragment teardown does not free it; this does.
    struct FragmentSession {
        lxb_html_parser_t* parser = nullptr;
        lxb_html_document_t* document = nullptr;
        ~FragmentSession() { reset(); }
        void reset() {
            if (document != nullptr) {
                lxb_html_document_t* dying = document;
                document = nullptr;
                if (parser != nullptr && parser->tree != nullptr &&
                    parser->tree->document == dying) {
                    parser->tree->document = nullptr;
                }
                lxb_html_document_destroy(dying);
            }
            if (parser != nullptr) {
                lxb_html_parser_destroy(parser);
                parser = nullptr;
            }
        }
    } session;

    session.parser = lxb_html_parser_create();
    if (session.parser == nullptr || lxb_html_parser_init(session.parser) != LXB_STATUS_OK) {
        error = "lexbor parser create failed";
        return false;
    }
    // Same scripting flag as a full document load: `<noscript>` is raw text, matching Chrome.
    lxb_html_tree_scripting_set(session.parser->tree, true);

    lxb_dom_node_t* root = lxb_html_parse_fragment_by_tag_id(
        session.parser, nullptr, LXB_TAG_BODY, LXB_NS_HTML,
        reinterpret_cast<const lxb_char_t*>(bytes.data()), bytes.size());
    if (root == nullptr || root->owner_document == nullptr) {
        if (session.parser->tree != nullptr) {
            session.document = session.parser->tree->document;
        }
        error = "HTML fragment parse failed";
        return false;
    }
    session.document = lxb_html_interface_document(root->owner_document);

    // The fragment result is the children of the parser's `<html>` root. Lexbor does not
    // parent them under the context element; `innerHTML` moves these same children.
    collect_styles(root, frag);
    auto holder = std::make_unique<Node>();
    convert_children(lxb_dom_node_first_child(root), holder.get(), frag);
    session.reset();

    namespace fs = std::filesystem;
    const fs::path base = fs::path(frag.source_path).parent_path();
    for (const Document::StylesheetSource& sheet : frag.stylesheets) {
        if (sheet.href.empty() || sheet.href.find("://") != std::string::npos) {
            // Inline `<style>` text travels with the fragment as-is; a remote href is skipped.
            if (sheet.href.empty()) {
                host.stylesheets.push_back(sheet);
            }
            continue;
        }
        Document::StylesheetSource resolved = sheet;
        const fs::path file =
            fs::path(sheet.href).is_absolute() ? fs::path(sheet.href) : base / sheet.href;
        resolved.href = file.string();
        host.stylesheets.push_back(std::move(resolved));
    }
    note_dom_change(host);
    std::vector<std::unique_ptr<Node>> moved;
    moved.swap(holder->children);
    std::vector<std::string> ids;
    for (auto& ch : moved) {
        collect_ids(ch.get(), ids);
    }
    if (replace_children) {
        // `innerHTML = …`: everything the target already holds leaves before the fragment lands.
        // The id-collision pass below only takes nodes the new fragment names, which is why an
        // append leaves the old blocks a smaller re-render no longer names behind.
        while (!into->children.empty()) {
            Node* child = into->children.back().get();
            if (before_remove) {
                before_remove(child);
            }
            if (!remove_node(host, child)) {
                into->children.pop_back();
            }
        }
    }
    std::unordered_set<std::string> want(ids.begin(), ids.end());
    // Prefer the parent subtree over the id map: orphans that fell out of host.ids
    // still have to leave before the new fragment claims those names.
    std::vector<Node*> doomed;
    collect_descendants_with_ids(into, want, doomed);
    for (const std::string& id : ids) {
        if (Node* mapped = find(host, id)) {
            doomed.push_back(mapped);
        }
    }
    std::unordered_set<Node*> seen;
    // Remove deepest nodes first so erasing a parent does not invalidate a child pointer
    // we still plan to visit.
    std::sort(doomed.begin(), doomed.end(), [](Node* a, Node* b) {
        auto depth = [](Node* n) {
            int d = 0;
            for (; n != nullptr; n = n->parent) {
                ++d;
            }
            return d;
        };
        return depth(a) > depth(b);
    });
    for (Node* old : doomed) {
        if (old == nullptr || !seen.insert(old).second) {
            continue;
        }
        // Skip nodes already erased because an ancestor was removed.
        bool under_into = false;
        for (Node* walk = old; walk != nullptr; walk = walk->parent) {
            if (walk == into) {
                under_into = true;
                break;
            }
        }
        if (!under_into && find(host, old->id) != old) {
            continue;
        }
        if (before_remove) {
            before_remove(old);
        }
        remove_node(host, old);
    }
    for (auto& ch : moved) {
        ch->parent = into;
        adopt_ids(host, ch.get());
        into->children.push_back(std::move(ch));
    }
    error.clear();
    return true;
}

bool load_file(std::string_view path, Document& out, std::string& error) {
    return DocumentLoader(out).load(path, error);
}

bool attach_fragment(Document& host, Node* into, std::string_view path, std::string& error,
                     const std::function<void(Node*)>& before_remove, bool replace_children) {
    return DocumentLoader(host).attach_fragment(into, path, error, before_remove, replace_children);
}

}  // namespace html
}  // namespace zui
