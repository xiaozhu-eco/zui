#include "input/detail.h"
#include "input/interaction.h"

#include "html/document.h"
#include "html/selection.h"

#include "layout/caret/caret_service.h"
#include "text/grapheme_break.h"
#include "layout/caret/caret_types.h"
#include "layout/point.h"
#include "layout/scrollbar_h.h"
#include "layout/scrollbar_metrics.h"
#include "layout/scrollbar_v.h"
#include "trace/trace.h"

#include <algorithm>
#include <limits>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace zui {
namespace input {

using namespace input_detail;

namespace input_detail {
void seed_content_editable(html::Node* node) {
    if (!is_content_editable(node)) {
        return;
    }
    html::refresh_edit_value(node);
    node->attrs["data-zui-edit"] = "1";
}

void apply_flatten_replace(html::Node* host, const std::string& previous, const std::string& next) {
    if (host == nullptr || previous == next) {
        return;
    }
    const std::string current = html::range_edit_text(host);
    if (current == next) {
        html::refresh_edit_value(host);
        return;
    }
    const std::string& from = current;
    std::size_t pre = 0;
    while (pre < from.size() && pre < next.size() && from[pre] == next[pre]) {
        ++pre;
    }
    std::size_t old_end = from.size();
    std::size_t new_end = next.size();
    while (old_end > pre && new_end > pre && from[old_end - 1] == next[new_end - 1]) {
        --old_end;
        --new_end;
    }
    if (pre < old_end) {
        html::delete_flatten_range(host, pre, old_end);
    }
    if (pre < new_end) {
        html::insert_text_at(host, pre, std::string_view(next).substr(pre, new_end - pre));
    }
    html::refresh_edit_value(host);
}

namespace {

std::uint8_t ce_child_style_bits(const html::Node* child, const html::Node*& text_out) {
    text_out = nullptr;
    if (child == nullptr) {
        return 0;
    }
    std::uint8_t style = 0;
    const html::Node* n = child;
    if (n->tag == "b") {
        style = static_cast<std::uint8_t>(style | 1);
        if (n->children.size() != 1) {
            return 0xff;
        }
        n = n->children[0].get();
    }
    if (n->tag == "i") {
        style = static_cast<std::uint8_t>(style | 2);
        if (n->children.size() != 1) {
            return 0xff;
        }
        n = n->children[0].get();
    }
    if (n->tag != "#text") {
        return 0xff;
    }
    text_out = n;
    return style;
}

bool ce_structure_matches(const html::Node* node, const std::vector<FormatRun>& runs,
                          const std::string& value) {
    if (node == nullptr) {
        return false;
    }
    std::vector<FormatRun> effective = runs;
    if (effective.empty() && !value.empty()) {
        effective.push_back(FormatRun{0, value.size(), 0});
    }
    if (node->children.size() != effective.size()) {
        return false;
    }
    for (std::size_t i = 0; i < effective.size(); ++i) {
        const FormatRun& run = effective[i];
        const html::Node* text_node = nullptr;
        const std::uint8_t style = ce_child_style_bits(node->children[i].get(), text_node);
        if (text_node == nullptr || style != run.style_bits) {
            return false;
        }
        if (run.end < run.start || run.end > value.size()) {
            return false;
        }
        const std::size_t len = run.end - run.start;
        if (text_node->text.size() != len) {
            return false;
        }
    }
    return true;
}

void ce_update_texts(html::Node* node, const std::vector<FormatRun>& runs, const std::string& value) {
    std::vector<FormatRun> effective = runs;
    if (effective.empty() && !value.empty()) {
        effective.push_back(FormatRun{0, value.size(), 0});
    }
    for (std::size_t i = 0; i < effective.size(); ++i) {
        const html::Node* text_c = nullptr;
        (void)ce_child_style_bits(node->children[i].get(), text_c);
        html::Node* text_node = const_cast<html::Node*>(text_c);
        if (text_node != nullptr) {
            text_node->text = value.substr(effective[i].start, effective[i].end - effective[i].start);
        }
    }
}

void ce_build_children(html::Node* node, const std::vector<FormatRun>& runs, const std::string& value) {
    node->children.clear();
    std::vector<FormatRun> effective = runs;
    if (effective.empty() && !value.empty()) {
        effective.push_back(FormatRun{0, value.size(), 0});
    }
    for (const FormatRun& run : effective) {
        if (run.end <= run.start || run.start >= value.size()) {
            continue;
        }
        const std::size_t end = std::min(run.end, value.size());
        const char mark = static_cast<char>(run.style_bits);
        html::Node* parent = node;
        std::vector<std::unique_ptr<html::Node>> owned;
        auto wrap = [&](const char* tag) {
            auto element = std::make_unique<html::Node>();
            element->tag = tag;
            element->parent = parent;
            html::Node* raw = element.get();
            if (parent == node) {
                owned.push_back(std::move(element));
            } else {
                parent->children.push_back(std::move(element));
            }
            parent = raw;
        };
        if ((mark & 1) != 0) {
            wrap("b");
        }
        if ((mark & 2) != 0) {
            wrap("i");
        }
        auto text_node = std::make_unique<html::Node>();
        text_node->tag = "#text";
        text_node->text = value.substr(run.start, end - run.start);
        text_node->parent = parent;
        if (parent == node) {
            owned.push_back(std::move(text_node));
        } else {
            parent->children.push_back(std::move(text_node));
        }
        for (auto& piece : owned) {
            node->children.push_back(std::move(piece));
        }
    }
}

}  // namespace

void sync_content_editable(html::Node* node, const std::vector<FormatRun>& runs) {
    if (!is_content_editable(node)) {
        return;
    }
    node->text = node->value;
    // Legacy mirror of format_runs onto node->format. CE authority is the DOM tree.
    std::string styles(node->value.size(), '\0');
    for (const FormatRun& run : runs) {
        const std::size_t begin = std::min(run.start, styles.size());
        const std::size_t end = std::min(run.end, styles.size());
        for (std::size_t i = begin; i < end; ++i) {
            styles[i] = static_cast<char>(run.style_bits);
        }
    }
    node->format = std::move(styles);

    if (ce_structure_matches(node, runs, node->value)) {
        ce_update_texts(node, runs, node->value);
        return;
    }
    ce_build_children(node, runs, node->value);
    html::note_dom_change(node);
}

void rebuild_content_editable(html::Node* node) {
    if (!is_content_editable(node)) {
        return;
    }
    // Legacy entry: derive runs from the projected format string, then sync.
    EditModel tmp;
    tmp.text = node->value;
    tmp.load_format_bytes(node->format);
    sync_content_editable(node, tmp.format_runs);
}

}  // namespace input_detail

}  // namespace input
}  // namespace zui
