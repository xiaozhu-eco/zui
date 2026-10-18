// Debug-only diagnostics for the App: DOM / box dumps, the ZUI_DEBUG_FLOW report and the
// string helpers they print with. `Diagnostics` owns the dump serial; the flow it reads is the
// document and the engine it holds.

#ifndef ZUI_SOURCE_APP_DIAGNOSTICS_H
#define ZUI_SOURCE_APP_DIAGNOSTICS_H

#include "html/document.h"
#include "layout/engine.h"
#include "log/log.h"

#include <cstddef>
#include <fstream>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace zui {
namespace app_detail {

class Diagnostics {
public:
    Diagnostics(html::Document& document, layout::LayoutEngine& layouter)
        : document_(document), layouter_(layouter) {}

    static std::string preview_text(std::string_view text, size_t max_cp = 24) {
        std::string out;
        size_t cps = 0;
        for (size_t i = 0; i < text.size() && cps < max_cp;) {
            unsigned char c = static_cast<unsigned char>(text[i]);
            size_t n = 1;
            if (c >= 0xF0)
                n = 4;
            else if (c >= 0xE0)
                n = 3;
            else if (c >= 0xC0)
                n = 2;
            if (i + n > text.size())
                n = 1;
            out.append(text.substr(i, n));
            i += n;
            ++cps;
        }
        if (out.size() < text.size())
            out += "...";
        for (char& ch : out) {
            if (ch == '\n' || ch == '\r' || ch == '\t')
                ch = ' ';
        }
        return out;
    }

    int count_matching_id(html::Node* node, std::string_view id) const {
        if (node == nullptr) {
            return 0;
        }
        int n = (node->id == id) ? 1 : 0;
        for (const auto& ch : node->children) {
            n += count_matching_id(ch.get(), id);
        }
        return n;
    }

    int count_ch_buttons(html::Node* node) const {
        if (node == nullptr) {
            return 0;
        }
        int n = 0;
        if (node->tag == "button" && html::has_class_token(node->class_name, "ch")) {
            ++n;
        }
        for (const auto& ch : node->children) {
            n += count_ch_buttons(ch.get());
        }
        return n;
    }

    void dump_dom(std::ostream& out, html::Node* node, int depth, int index) const {
        if (node == nullptr)
            return;
        for (int i = 0; i < depth; ++i)
            out << "  ";
        out << "[" << index << "] <" << node->tag << ">";
        if (!node->id.empty())
            out << " id=" << node->id;
        if (!node->class_name.empty())
            out << " class=\"" << node->class_name << "\"";
        out << " visible=" << (node->visible ? 1 : 0);
        if (node->attrs.count("hidden"))
            out << " hidden";
        if (!node->text.empty())
            out << " text=\"" << preview_text(node->text) << "\"";
        out << " children=" << node->children.size() << "\n";
        int i = 0;
        for (auto& ch : node->children) {
            dump_dom(out, ch.get(), depth + 1, i++);
        }
    }

    void collect_titles(html::Node* node, std::vector<html::Node*>& titles) const {
        if (node == nullptr)
            return;
        if (node->tag == "h2" && html::has_class_token(node->class_name, "chapter-title")) {
            titles.push_back(node);
        }
        for (auto& ch : node->children)
            collect_titles(ch.get(), titles);
    }

    void dump_boxes(std::ostream& out, layout::Box* box, int depth) const {
        if (box == nullptr)
            return;
        for (int i = 0; i < depth; ++i)
            out << "  ";
        html::Node* n = box->node;
        out << "y=" << box->geo.world_y << " h=" << box->geo.height << " x=" << box->geo.world_x
            << " w=" << box->geo.width;
        if (n != nullptr) {
            out << " <" << n->tag << ">";
            if (!n->id.empty())
                out << "#" << n->id;
            if (!n->class_name.empty())
                out << "." << n->class_name;
            if (!n->text.empty())
                out << " \"" << preview_text(n->text) << "\"";
            else if (!box->text.empty())
                out << " boxtext=\"" << preview_text(box->text) << "\"";
        } else {
            out << " <anon>";
            if (!box->text.empty())
                out << " \"" << preview_text(box->text) << "\"";
        }
        out << " dirty=" << (box->layout_dirty ? 1 : 0) << " kids=" << box->children.size() << "\n";
        // Cap depth printing of huge para lists: for div#paras only print first/last 2
        if (n != nullptr && (n->id == "paras" || n->id == "paras_b") && box->children.size() > 6) {
            for (int i = 0; i < 2 && i < static_cast<int>(box->children.size()); ++i) {
                dump_boxes(out, box->children[static_cast<size_t>(i)], depth + 1);
            }
            for (int i = 0; i < depth + 1; ++i)
                out << "  ";
            out << "... (" << box->children.size() << " child boxes) ...\n";
            for (int i = std::max(2, static_cast<int>(box->children.size()) - 2);
                 i < static_cast<int>(box->children.size()); ++i) {
                dump_boxes(out, box->children[static_cast<size_t>(i)], depth + 1);
            }
            return;
        }
        for (layout::Box* ch : box->children)
            dump_boxes(out, ch, depth + 1);
    }

    static bool under_node(html::Node* n, html::Node* root) {
        for (; n != nullptr; n = n->parent)
            if (n == root)
                return true;
        return false;
    }

    /// The `ZUI_DEBUG_FLOW` report: chapter titles, the `#flow` DOM and its box tree.
    void dump_flow(const char* why, const std::string& path) {
        html::Document& document = document_;
        layout::LayoutEngine& layouter = layouter_;
        if (path.empty())
            return;
        html::Node* flow = html::find(document, "flow");
        html::Node* wrap = html::find(document, "flow_wrap");
        std::ofstream out(path, std::ios::app);
        if (!out)
            return;
        ++serial_;
        out << "\n===== dump #" << serial_ << " why=" << why
            << " scroll_y=" << (wrap ? wrap->scroll_y : -1.f) << " =====\n";

        // Smoking gun summary: every chapter-title in the document.
        std::vector<html::Node*> titles;
        collect_titles(document.root.get(), titles);
        out << "chapter-title count=" << titles.size() << "\n";
        for (size_t i = 0; i < titles.size(); ++i) {
            html::Node* title = titles[i];
            layout::Box* box = layouter.principal_box(title);
            out << "  title[" << i << "] text=\"" << preview_text(title->text, 40) << "\""
                << " visible=" << (title->visible ? 1 : 0)
                << " parent=" << (title->parent ? title->parent->id : std::string("-"));
            if (box) {
                out << " world_y=" << box->geo.world_y << " h=" << box->geo.height;
            } else {
                out << " box=null";
            }
            out << "\n";
        }

        out << "--- DOM #flow ---\n";
        if (flow == nullptr) {
            out << "(missing)\n";
        } else {
            out << "flow children=" << flow->children.size() << "\n";
            int i = 0;
            for (auto& ch : flow->children) {
                dump_dom(out, ch.get(), 1, i++);
            }
        }

        out << "--- BOX under #flow (or root if flow box missing) ---\n";
        layout::Box* flow_box = flow ? layouter.principal_box(flow) : nullptr;
        if (flow_box == nullptr) {
            out << "(no flow box; dumping root)\n";
            dump_boxes(out, layouter.root(), 0);
        } else {
            out << "flow scroll_h=" << flow_box->geo.scroll_height << " h=" << flow_box->geo.height
                << "\n";
            // Prefer wrap's scroll metrics when present.
            if (wrap) {
                if (layout::Box* wb = layouter.principal_box(wrap)) {
                    out << "flow_wrap scroll_y=" << wrap->scroll_y
                        << " scroll_h=" << wb->geo.scroll_height << " client_h=" << wb->geo.height
                        << "\n";
                }
            }
            dump_boxes(out, flow_box, 0);
        }
        out << "===== end dump #" << serial_ << " =====\n";
        out.flush();
        ZUI_INFO("[debug_flow] wrote #{} why={} titles={} -> {}", serial_, why, titles.size(),
                 path);
    }

private:
    html::Document& document_;
    layout::LayoutEngine& layouter_;
    int serial_ = 0;
};

}  // namespace app_detail
}  // namespace zui

#endif
