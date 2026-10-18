// Engine-side driver for the zui differential test harness.
//
// Reads the same case JSON the browser driver reads (TESTPLAN section 3) and executes the same
// `steps` against the engine, writing one trace JSON per `trace` step. `capture` does not paint:
// a frame image has to be the presented framebuffer, and that readback is not implemented.
// The step vocabulary and the action DSL are deliberately closed sets: `eval` may only use the
// subset both ends implement, and that subset is enforced here at parse time.
//
// Usage:
//   zui_difftest --case <case.json> --out <dir> [--props <props.json>] [--env <env.json>]
//                [--page <page.html>] [--quiet]

#include "fs_util.h"
#include "png_encode.h"
#include "trace_export.h"

#include "backends/frame_probe.h"
#include "paint/display_list.h"

#include "css/engine/engine.h"
#include "css/transition/transition.h"
#include "html/document.h"
#include "input/interaction.h"
#include "layout/engine.h"
#include "layout/scrollbar_h.h"
#include "layout/scrollbar_v.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <system_error>
#include <vector>

namespace {

using Json = nlohmann::ordered_json;

struct Options {
    std::string case_path;
    std::string out_dir;
    std::string repo_root;
    std::string props_path;
    std::string env_path;
    std::string page_override;
    bool quiet = false;
    // L0 probe mode (TESTPLAN section 8): measure the two environment parameters the whole
    // comparison rests on, without running a case.
    bool probe = false;
    std::string probe_text = "Hamburgefonstiv";
    std::string probe_spaces_text = "Hamburge fonstiv a b c d e";
    std::string probe_kern_text = "AVAVAVAVAVAVA";
    float probe_font_px = 16;
    std::string probe_family = "sans-serif";
    /// LAYOUT.md section 9: the scrollbar thickness is a platform parameter. The driver takes it
    /// from the environment file so the comparison pins the browser's measured value.
    float scrollbar_thickness = zui::layout::PlatformMetrics{}.scrollbar_thickness;
};

std::string parent_dir(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

/// Trailing 3-digit step index so artifacts sort and read in execution order.
std::string step_name(std::size_t index, const std::string& op) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "step-%03zu-", index);
    return std::string(buf) + op;
}

bool parse_args(int argc, char** argv, Options& opt) {
    opt.repo_root = ZUI_SOURCE_DIR;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto need = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << "missing value for " << name << "\n";
                return nullptr;
            }
            return argv[++i];
        };
        if (arg == "--case") {
            const char* v = need("--case");
            if (v == nullptr) {
                return false;
            }
            opt.case_path = v;
        } else if (arg == "--out") {
            const char* v = need("--out");
            if (v == nullptr) {
                return false;
            }
            opt.out_dir = v;
        } else if (arg == "--repo") {
            const char* v = need("--repo");
            if (v == nullptr) {
                return false;
            }
            opt.repo_root = v;
        } else if (arg == "--props") {
            const char* v = need("--props");
            if (v == nullptr) {
                return false;
            }
            opt.props_path = v;
        } else if (arg == "--env") {
            const char* v = need("--env");
            if (v == nullptr) {
                return false;
            }
            opt.env_path = v;
        } else if (arg == "--page") {
            const char* v = need("--page");
            if (v == nullptr) {
                return false;
            }
            opt.page_override = v;
        } else if (arg == "--quiet") {
            opt.quiet = true;
        } else if (arg == "--probe") {
            opt.probe = true;
        } else if (arg == "--probe-text") {
            const char* v = need("--probe-text");
            if (v == nullptr) {
                return false;
            }
            opt.probe_text = v;
        } else if (arg == "--probe-spaces-text") {
            const char* v = need("--probe-spaces-text");
            if (v == nullptr) {
                return false;
            }
            opt.probe_spaces_text = v;
        } else if (arg == "--probe-kern-text") {
            const char* v = need("--probe-kern-text");
            if (v == nullptr) {
                return false;
            }
            opt.probe_kern_text = v;
        } else if (arg == "--probe-font-px") {
            const char* v = need("--probe-font-px");
            if (v == nullptr) {
                return false;
            }
            opt.probe_font_px = std::strtof(v, nullptr);
        } else if (arg == "--probe-family") {
            const char* v = need("--probe-family");
            if (v == nullptr) {
                return false;
            }
            opt.probe_family = v;
        } else if (arg == "--scrollbar-thickness") {
            const char* v = need("--scrollbar-thickness");
            if (v == nullptr) {
                return false;
            }
            opt.scrollbar_thickness = std::strtof(v, nullptr);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "zui_difftest --case <case.json> --out <dir> [--repo <root>]\n"
                         "             [--props props.json] [--env env.json] [--page page.html]\n";
            return false;
        } else {
            std::cerr << "unknown option: " << arg << "\n";
            return false;
        }
    }
    if (!opt.probe && (opt.case_path.empty() || opt.out_dir.empty())) {
        std::cerr << "--case and --out are required (or --probe)\n";
        return false;
    }
    return true;
}

zui::PointerAccuracy pointer_accuracy(const std::string& value) {
    if (value == "none") {
        return zui::PointerAccuracy::None;
    }
    if (value == "fine") {
        return zui::PointerAccuracy::Fine;
    }
    return zui::PointerAccuracy::Coarse;
}

/// Facts measured from the pinned headless browser. Desktop `HostMediaFacts` defaults
/// (hover + fine) are a different host and must not replay a browser golden.
zui::HostMediaFacts capture_browser_media() {
    zui::HostMediaFacts facts;
    facts.hover = false;
    facts.any_hover = false;
    facts.pointer = zui::PointerAccuracy::Coarse;
    facts.any_pointer = zui::PointerAccuracy::Coarse;
    facts.reduced_motion = false;
    facts.color_scheme = zui::ColorSchemePreference::Light;
    return facts;
}

zui::HostMediaFacts load_replay_host_media(const std::string& env_path) {
    zui::HostMediaFacts facts = capture_browser_media();
    std::string text;
    std::string error;
    if (env_path.empty() || !zdt::read_file(env_path, text, error)) {
        return facts;
    }
    Json env;
    try {
        env = Json::parse(text, nullptr, true, true);
    } catch (const std::exception&) {
        return facts;
    }
    const Json media = env.value("engine", Json::object()).value("host_media", Json::object());
    if (!media.is_object() || media.empty()) {
        return facts;
    }
    facts.hover = media.value("hover", facts.hover);
    facts.any_hover = media.value("any_hover", facts.any_hover);
    if (media.contains("pointer") && media["pointer"].is_string()) {
        facts.pointer = pointer_accuracy(media["pointer"].get<std::string>());
    }
    if (media.contains("any_pointer") && media["any_pointer"].is_string()) {
        facts.any_pointer = pointer_accuracy(media["any_pointer"].get<std::string>());
    }
    facts.reduced_motion = media.value("reduced_motion", facts.reduced_motion);
    const std::string scheme = media.value("color_scheme", std::string("light"));
    if (scheme == "dark") {
        facts.color_scheme = zui::ColorSchemePreference::Dark;
    } else if (scheme == "no-preference") {
        facts.color_scheme = zui::ColorSchemePreference::NoPreference;
    } else {
        facts.color_scheme = zui::ColorSchemePreference::Light;
    }
    return facts;
}

/// Drives one case: document, CSS engine, layout engine, transitions, virtual clock.
class EngineRunner {
public:
    EngineRunner(zui::html::Document& doc, zui::layout::FreeTypeTextMetrics& metrics,
                 const zdt::TraceOptions& trace_options,
                 const zui::layout::PlatformMetrics& platform)
        : doc_(doc),
          metrics_(metrics),
          layout_(metrics, platform),
          trace_options_(trace_options),
          platform_(platform),
          host_media_(capture_browser_media()) {}

    /// Per-step view of LAYOUT.md section 16's counters. The engine's counters live in
    /// LayoutContext and reset every frame, so the driver accumulates them across the frames
    /// one `steps[]` entry runs. That is what makes "a scroll step must not relayout" and
    /// "an animation frame must not mark Layout dirty" checkable from outside the engine.
    struct StepStats {
        int frames = 0;
        std::size_t dirty_marks = 0;
        std::size_t laid_out = 0;
        std::size_t cache_hits = 0;
        std::size_t evicted_measure_slots = 0;
        std::size_t container_requery_rounds = 0;
        std::size_t interned_styles = 0;
    };

    void begin_step() { step_ = StepStats{}; }
    const StepStats& step_stats() const { return step_; }

    /// A `steps[]` action mutated the DOM structure. The boxes are lowered on the next restyle
    /// frame, after the cascade has computed the new nodes' styles, and the host is marked
    /// Layout there (LAYOUT.md section 6: mark the smallest closed ancestor).
    void note_structure_change(zui::html::Node* node) {
        if (node == nullptr) {
            return;
        }
        for (zui::html::Node* queued : relower_queue_) {
            if (queued == node) {
                return;
            }
        }
        relower_queue_.push_back(node);
    }
    /// Case-level, not step-level: the frame that finally serves a cache hit no longer knows
    /// why an earlier frame reported `converged == false`, and the trace steps run no frame at
    /// all. The notes accumulate for the lifetime of the case.
    const std::vector<zdt::NonConvergenceNote>& non_convergence_notes() const { return notes_; }

    void set_viewport(float w, float h) {
        viewport_w_ = w;
        viewport_h_ = h;
    }

    double clock() const { return clock_; }

    /// Virtual clock in seconds. `freeze` / `advance` in a case are milliseconds, which is
    /// what the Web Animations API uses on the browser side (TESTPLAN section 5).
    void set_clock_ms(double ms) { clock_ = ms / 1000.0; }

    zui::css::LengthBasis length_basis(const zui::html::Node* node) const {
        zui::css::LengthBasis b;
        if (node == nullptr) {
            return b;
        }
        if (node->parent != nullptr) {
            if (zui::layout::Box* parent = layout_.principal_box(node->parent)) {
                b.inline_px = parent->geo.content_width;
                b.block_px = parent->geo.content_height;
                b.has_inline = true;
                b.has_block = true;
            }
        } else {
            b.inline_px = viewport_w_;
            b.block_px = viewport_h_;
            b.has_inline = true;
            b.has_block = true;
        }
        if (zui::layout::Box* box = layout_.principal_box(const_cast<zui::html::Node*>(node))) {
            b.used_width = box->geo.content_width;
            b.used_height = box->geo.content_height;
            b.has_used_width = true;
            b.has_used_height = true;
        }
        return b;
    }

    void frame(bool restyle) {
        motion_.set_length_basis([this](const zui::html::Node* n) { return length_basis(n); });
        if (restyle) {
            const zui::css::Engine::CascadeResult cr =
                css_.cascade(doc_, viewport_w_, viewport_h_, host_media_);
            motion_.sync(cr, clock_);
            // Boxes before layout: a node the cascade attached for the first time (eval.insert)
            // has no box yet, and a node whose display changed needs its subtree lowered again.
            // Lowering here, and not in the action, is what makes the new box carry its
            // computed style instead of an empty one.
            // Same host as App::flush_style: a node that just left `display: none` has no
            // box, so the parent is what grows the new child. Relowering the node itself no-ops.
            std::set<zui::html::Node*> reattach_hosts;
            for (zui::html::Node* node : cr.reattach) {
                reattach_hosts.insert(node->parent != nullptr ? node->parent : node);
            }
            for (zui::html::Node* host : reattach_hosts) {
                layout_.relower_subtree(host);
                layout_.mark_needs_layout(host);
            }
            for (zui::html::Node* host : relower_queue_) {
                layout_.relower_subtree(host);
                layout_.mark_needs_layout(host);
            }
            relower_queue_.clear();
            for (zui::html::Node* node : cr.layout) {
                layout_.mark_needs_layout(node);
            }
        }
        layout_.layout(doc_, zui::layout::Viewport{viewport_w_, viewport_h_});
        account_frame();
        // LAYOUT.md §10: publish query-container sizes then rematch `@container` (same fuse as App).
        for (int cq = 0; cq < 2; ++cq) {
            if (!layout_.frame_stats().container_size_changed) {
                break;
            }
            const zui::css::Engine::CascadeResult cq_cr =
                css_.cascade(doc_, viewport_w_, viewport_h_, host_media_);
            motion_.sync(cq_cr, clock_);
            for (zui::html::Node* node : cq_cr.layout) {
                layout_.mark_needs_layout(node);
            }
            for (zui::html::Node* node : cq_cr.reattach) {
                layout_.relower_subtree(node->parent != nullptr ? node->parent : node);
                layout_.mark_needs_layout(node->parent != nullptr ? node->parent : node);
            }
            layout_.layout(doc_, zui::layout::Viewport{viewport_w_, viewport_h_});
            account_frame();
        }
        motion_.resolve_pending();
        motion_.sample(clock_);
        // Length transitions write used overlays after the first layout; re-layout so geometry
        // matches the sampled width/height/margin/padding before sync_world / paint.
        if (motion_.needs_layout()) {
            for (zui::html::Node* n : motion_.layout_nodes()) {
                layout_.mark_needs_layout(n);
            }
            layout_.layout(doc_, zui::layout::Viewport{viewport_w_, viewport_h_});
        }
        layout_.sync_world();
    }

    void load(const std::string& page_path, std::string& error) {
        if (!zui::html::load_file(page_path, doc_, error)) {
            return;
        }
        if (!css_.load_sheets(doc_, error)) {
            return;
        }
        frame(true);
        // TESTPLAN section 3: `load` is "load plus resource drain - the frame tasks of LAYOUT
        // section 15 run to completion". A replaced element needs that: LAYOUT section 4 says
        // the first observation still measures 300x150 and only bumps ContentVersion, so the
        // intrinsic size lands on a later frame. A real page reaches that state before the
        // browser stops, so the driver settles a few frames here instead of exactly one.
        for (int i = 0; i < 3; ++i) {
            frame(false);
        }
    }

    /// Resolves `#id`, `.class`, or `tag` to a node; empty means "window" (the root element).
    zui::html::Node* resolve(const std::string& target) {
        if (target.empty() || target == "window" || target == ":root") {
            return doc_.root.get();
        }
        if (target[0] == '#') {
            return zui::html::find(doc_, target.substr(1));
        }
        zui::html::Node* found = nullptr;
        const auto visit = [&](auto&& self, zui::html::Node* node) -> void {
            if (node == nullptr || found != nullptr) {
                return;
            }
            if (!node->tag.empty() && node->tag != "#text") {
                if (target[0] == '.') {
                    if (zui::html::has_class_token(node->class_name, target.substr(1))) {
                        found = node;
                        return;
                    }
                } else if (node->tag == target) {
                    found = node;
                    return;
                }
            }
            for (const auto& child : node->children) {
                self(self, child.get());
            }
        };
        visit(visit, doc_.root.get());
        return found;
    }

    bool apply_action(const Json& action, std::string& error) {
        const std::string what = action.value("do", std::string());
        const std::string target = action.value("target", std::string());
        zui::html::Node* node = resolve(target);
        if (what == "set_style") {
            if (node == nullptr) {
                error = "set_style: no node for '" + target + "'";
                return false;
            }
            std::string css_text;
            for (auto it = action["props"].begin(); it != action["props"].end(); ++it) {
                css_text += it.key() + ":" + it.value().get<std::string>() + ";";
            }
            node->attrs["style"] = css_text;
            return true;
        }
        if (what == "add_class") {
            return zui::html::add_class(doc_, target[0] == '#' ? target.substr(1) : target,
                                        action.value("name", std::string()));
        }
        if (what == "remove_class") {
            return zui::html::remove_class(doc_, target[0] == '#' ? target.substr(1) : target,
                                           action.value("name", std::string()));
        }
        if (what == "set_text") {
            if (node == nullptr) {
                error = "set_text: no node for '" + target + "'";
                return false;
            }
            node->text = action.value("text", std::string());
            node->value = node->text;
            layout_.sync_text(node);
            return true;
        }
        if (what == "select_text") {
            // Document-level selection (TESTPLAN section 3): the paint source is `node->text`, and
            // the browser side builds the same half-open range with a DOM Range. `end` defaults to
            // the whole text so a case can select an element without counting bytes.
            if (node == nullptr) {
                error = "select_text: no node for '" + target + "'";
                return false;
            }
            const std::size_t start =
                static_cast<std::size_t>(std::max(0, action.value("start", 0)));
            const std::size_t end = action.contains("end")
                                        ? static_cast<std::size_t>(std::max(0, action.value("end", 0)))
                                        : node->text.size();
            zui::html::set_selection(doc_, node, start, end);
            return true;
        }
        if (what == "set_checked") {
            if (node == nullptr) {
                error = "set_checked: no node for '" + target + "'";
                return false;
            }
            const bool on = action.value("value", true);
            std::string type;
            std::string group;
            if (const auto found = node->attrs.find("type"); found != node->attrs.end()) {
                type = found->second;
            }
            if (const auto found = node->attrs.find("name"); found != node->attrs.end()) {
                group = found->second;
            }
            const bool radio = type == "radio";
            if (on && radio && !group.empty()) {
                const auto clear = [&](auto&& self, zui::html::Node* current) -> void {
                    if (current == nullptr) {
                        return;
                    }
                    if (current != node) {
                        const auto found_type = current->attrs.find("type");
                        const auto found_name = current->attrs.find("name");
                        if (found_type != current->attrs.end() && found_type->second == "radio" &&
                            found_name != current->attrs.end() && found_name->second == group) {
                            current->checked = false;
                        }
                    }
                    for (const auto& child : current->children) {
                        self(self, child.get());
                    }
                };
                clear(clear, doc_.root.get());
            }
            node->checked = on;
            // Radio keeps its value attribute (the submission value). Checkbox stays
            // on the frozen "true" / "false" payload.
            if (!radio) {
                node->value = on ? "true" : "false";
            }
            return true;
        }
        if (what == "set_attr") {
            if (node == nullptr) {
                error = "set_attr: no node for '" + target + "'";
                return false;
            }
            node->attrs[action.value("name", std::string())] = action.value("value", std::string());
            return true;
        }
        if (what == "remove_attr") {
            // The boolean attributes the examples hide their overlays with (`hidden`) cannot be
            // revealed by set_attr("") - the selector still matches - so the vocabulary needs the
            // eraser half as well. Both drivers implement it; the `eval` step restyles after.
            if (node == nullptr) {
                error = "remove_attr: no node for '" + target + "'";
                return false;
            }
            node->attrs.erase(action.value("name", std::string()));
            return true;
        }
        if (what == "scroll") {
            if (node == nullptr) {
                error = "scroll: no node for '" + target + "'";
                return false;
            }
            node->scroll_x = static_cast<float>(action.value("x", 0.0));
            node->scroll_y = static_cast<float>(action.value("y", 0.0));
            return true;
        }
        if (what == "remove") {
            if (node == nullptr) {
                error = "remove: no node for '" + target + "'";
                return false;
            }
            zui::html::Node* parent_node = node->parent;
            layout_.detach_node(node);
            const bool removed = zui::html::remove_tree(doc_, node->id);
            // LAYOUT section 6: a box-tree mutation marks the smallest closed ancestor. Without
            // this the parent's cached result survives the removal and the next trace still
            // shows the old geometry.
            if (removed && parent_node != nullptr) {
                layout_.mark_needs_layout(parent_node);
            }
            return removed;
        }
        if (what == "insert") {
            if (node == nullptr) {
                error = "insert: no node for '" + target + "'";
                return false;
            }
            // Structured, not an HTML string: the vocabulary stays closed on both ends
            // (TESTPLAN section 3) and the two drivers cannot drift on parsing.
            auto created = std::make_unique<zui::html::Node>();
            created->tag = action.value("tag", std::string("div"));
            created->id = action.value("id", std::string());
            created->class_name = action.value("class", std::string());
            created->text = action.value("text", std::string());
            created->value = created->text;
            if (action.contains("props")) {
                std::string css_text;
                for (auto it = action["props"].begin(); it != action["props"].end(); ++it) {
                    css_text += it.key() + ":" + it.value().get<std::string>() + ";";
                }
                created->attrs["style"] = css_text;
            }
            created->parent = node;
            zui::html::Node* raw = created.get();
            const std::string before = action.value("before", std::string());
            if (!before.empty()) {
                zui::html::Node* anchor = resolve(before);
                if (anchor == nullptr || anchor->parent != node) {
                    error = "insert: 'before' is not a child of '" + target + "'";
                    return false;
                }
                auto& kids = node->children;
                auto at = std::find_if(kids.begin(), kids.end(),
                                       [&](const std::unique_ptr<zui::html::Node>& kid) {
                                           return kid.get() == anchor;
                                       });
                kids.insert(at, std::move(created));
            } else {
                node->children.push_back(std::move(created));
            }
            if (!raw->id.empty()) {
                doc_.ids[raw->id] = raw;
            }
            // The box is lowered on the next restyle frame, after the cascade (see
            // EngineRunner::frame): a box built here would carry an empty style.
            note_structure_change(node);
            return true;
        }
        error = "unsupported action '" + what + "'";
        return false;
    }

    zui::html::Document& doc() { return doc_; }
    zui::layout::LayoutEngine& layout() { return layout_; }
    zui::css::Engine& css() { return css_; }
    void set_scrollbar_thickness(float thickness) { painter_.set_scrollbar_thickness(thickness); }
    void set_host_media(const zui::HostMediaFacts& facts) { host_media_ = facts; }
    zui::paint::DisplayList display_list(int width, int height) {
        zui::paint::DisplayList list;
        if (layout_.root() != nullptr) {
            painter_.set_resource_base(doc_.source_path);
            painter_.set_selection(doc_.selection.node, doc_.selection.start, doc_.selection.end);
            painter_.paint(*layout_.root(), list, static_cast<float>(width),
                           static_cast<float>(height));
        }
        return list;
    }
    zui::layout::FreeTypeTextMetrics& metrics() { return metrics_; }
    const zdt::TraceOptions& trace_options() const { return trace_options_; }
    float viewport_w() const { return viewport_w_; }
    float viewport_h() const { return viewport_h_; }

private:
    void account_frame() {
        const zui::layout::LayoutContext::FrameStats& stats = layout_.frame_stats();
        ++step_.frames;
        step_.dirty_marks += stats.dirty_marks;
        step_.laid_out += stats.laid_out;
        step_.cache_hits += stats.cache_hits;
        step_.evicted_measure_slots += stats.evicted_measure_slots;
        step_.container_requery_rounds += stats.container_requery_rounds;
        step_.interned_styles = stats.interned_styles;
        for (const zui::layout::LayoutContext::NonConvergence& entry : layout_.non_convergence()) {
            if (entry.box == nullptr) {
                continue;
            }
            bool seen = false;
            for (const zdt::NonConvergenceNote& note : notes_) {
                if (note.box == entry.box) {
                    seen = true;
                    break;
                }
            }
            if (!seen) {
                notes_.push_back(zdt::NonConvergenceNote{entry.box, entry.reason});
            }
        }
    }

    zui::html::Document& doc_;
    zui::layout::FreeTypeTextMetrics& metrics_;
    zui::layout::LayoutEngine layout_;
    zui::css::Engine css_;
    zui::css::Transitions motion_;
    zui::paint::Painter painter_;
    zdt::TraceOptions trace_options_;
    zui::layout::PlatformMetrics platform_;
    float viewport_w_ = 800;
    float viewport_h_ = 600;
    zui::HostMediaFacts host_media_;
    double clock_ = 0;
    StepStats step_;
    std::vector<zdt::NonConvergenceNote> notes_;
    std::vector<zui::html::Node*> relower_queue_;
};

void set_scroll(zui::layout::Box* box, float x, float y, float thickness) {
    if (box == nullptr) {
        return;
    }
    // The scrollport is the client box. A visible bar on one axis shortens the other,
    // which is what `scrollHeight - clientHeight` uses. The border box alone left
    // example-style's scroll end 15px short (2102 vs 2117).
    float view_w = std::max(0.0f, box->geo.width - box->geo.border.horizontal());
    float view_h = std::max(0.0f, box->geo.height - box->geo.border.vertical());
    const zui::layout::ScrollbarH horizontal = zui::layout::horizontal_scrollbar(*box, thickness);
    const zui::layout::ScrollbarV vertical = zui::layout::vertical_scrollbar(*box, thickness);
    if (horizontal.visible) {
        view_h = std::max(0.0f, view_h - horizontal.track_h);
    }
    if (vertical.visible) {
        view_w = std::max(0.0f, view_w - vertical.track_w);
    }
    zui::input::set_scroll(*box, x, y, view_w, view_h);
}

/// `advance` / `freeze` both move the virtual clock; the difference is only how the case
/// describes it (TESTPLAN sections 3 and 5). The engine has no rAF, so `advance` is a no-op
/// beyond moving the clock for transitions.
const std::set<std::string> kUnsupportedOps = {};

void collect_checked(const zui::html::Document& doc, const zui::html::Node* node, Json& out) {
    if (node == nullptr) {
        return;
    }
    if (zui::html::is_checkbox(node) || zui::html::is_radio(node)) {
        out.push_back(Json{{"path", zui::input::dom_path(doc, node)}, {"checked", node->checked}});
    }
    for (const auto& child : node->children) {
        collect_checked(doc, child.get(), out);
    }
}

zui::html::Node* node_of_box(zui::layout::Box* hit) {
    for (zui::layout::Box* box = hit; box != nullptr; box = box->parent) {
        if (box->node == nullptr) {
            continue;
        }
        zui::html::Node* n = box->node;
        while (n != nullptr && n->tag.size() >= 2 && n->tag[0] == ':' && n->tag[1] == ':') {
            n = n->parent;
        }
        return n;
    }
    return nullptr;
}

Json readback_state(zui::html::Document& doc, zui::layout::LayoutEngine& layout,
                    zui::input::Interaction& interaction, const Json& step, const std::string& op,
                    zui::html::Node* target_node, float thickness, float viewport_w,
                    float viewport_h) {
    zui::html::Node* node = target_node != nullptr ? target_node : doc.root.get();
    zui::layout::Box* box = node != nullptr ? layout.principal_box(node) : nullptr;
    if (box == nullptr) {
        box = layout.root();
    }
    Json hits = Json::array();
    Json listed = step.value("hits", Json::array());
    if (op == "dispatch" && listed.empty() && step.contains("events")) {
        for (auto it = step["events"].rbegin(); it != step["events"].rend(); ++it) {
            const std::string type = it->value("type", std::string());
            if (type.rfind("mouse", 0) == 0 && it->contains("x")) {
                listed.push_back(Json{{"x", it->value("x", 0.0)}, {"y", it->value("y", 0.0)}});
                break;
            }
        }
    }
    for (const auto& hit : listed) {
        const float x = hit.value("x", 0.0f);
        const float y = hit.value("y", 0.0f);
        zui::html::Node* hit_node = node_of_box(layout.hit_test(x, y));
        hits.push_back(Json{{"x", x}, {"y", y}, {"path", zui::input::dom_path(doc, hit_node)}});
    }
    Json checked = Json::array();
    collect_checked(doc, doc.root.get(), checked);
    Json scrollers = Json::array();
    const auto collect_scrollers = [&](auto&& self, const zui::html::Node* cursor) -> void {
        if (cursor == nullptr) {
            return;
        }
        zui::layout::Box* scroller = layout.principal_box(const_cast<zui::html::Node*>(cursor));
        if (scroller != nullptr && scroller->style_ptr != nullptr && scroller->node != nullptr) {
            const zui::layout::OverflowMode ox = scroller->style().overflow_x;
            const zui::layout::OverflowMode oy = scroller->style().overflow_y;
            const bool scrolls =
                ox == zui::layout::OverflowMode::Auto || ox == zui::layout::OverflowMode::Scroll ||
                oy == zui::layout::OverflowMode::Auto || oy == zui::layout::OverflowMode::Scroll;
            if (scrolls) {
                scrollers.push_back(Json{{"path", zui::input::dom_path(doc, cursor)},
                                         {"x", zdt::quantize(scroller->node->scroll_x)},
                                         {"y", zdt::quantize(scroller->node->scroll_y)}});
            }
        }
        for (const auto& child : cursor->children) {
            self(self, child.get());
        }
    };
    collect_scrollers(collect_scrollers, doc.root.get());
    zui::html::Node* focus = interaction.focused();
    Json state = Json::object();
    state["schema"] = "zui.difftest.state/1";
    state["op"] = op;
    state["target"] = zui::input::dom_path(doc, node);
    if (box != nullptr && box->node != nullptr) {
        state["scroll"] =
            Json::array({zdt::quantize(box->node->scroll_x), zdt::quantize(box->node->scroll_y)});
        float client_w = 0;
        float client_h = 0;
        if (box == layout.root()) {
            zui::input::document_client_size(
                viewport_w, viewport_h, box->geo.scroll_width, box->geo.scroll_height, thickness,
                box->geo.border.horizontal(), box->geo.border.vertical(), client_w, client_h);
        } else {
            const zui::input::ClientRect client = zui::input::client_rect(*box, thickness);
            client_w = client.w;
            client_h = client.h;
        }
        state["client_box"] = Json::array({zdt::quantize(client_w), zdt::quantize(client_h)});
    } else {
        state["scroll"] = Json::array({0, 0});
        state["client_box"] = Json::array({0, 0});
    }
    state["hit"] = hits.empty() ? Json(nullptr) : hits.back()["path"];
    state["hits"] = std::move(hits);
    state["scrollers"] = std::move(scrollers);
    state["focus"] = focus != nullptr ? Json(zui::input::dom_path(doc, focus)) : Json(nullptr);
    state["checked"] = std::move(checked);
    return state;
}

/// TESTPLAN section 6: both ends shape the same file. The browser loads it with @font-face,
/// the engine registers the same path for the family name here, before any chain is built.
/// Returns the list of registered faces so the summary can show what the run used.
std::vector<std::string> register_embedded_fonts(
    const std::string& repo_root, const std::string& env_path, std::string& error,
    std::vector<zui::backends::ProbeFontFace>& faces) {
    std::vector<std::string> registered;
    faces.clear();
    if (env_path.empty()) {
        return registered;
    }
    std::string text;
    if (!zdt::read_file(env_path, text, error)) {
        error.clear();  // an absent env file is not fatal: the case still runs, unregistered
        return registered;
    }
    Json env;
    try {
        env = Json::parse(text, nullptr, true, true);
    } catch (const std::exception& e) {
        error = std::string("env.json parse: ") + e.what();
        return registered;
    }
    const auto fonts = env.value("fonts", Json::object());
    for (const auto& entry : fonts.value("embedded", Json::array())) {
        const std::string family = entry.value("family", std::string());
        const std::string file = entry.value("file", std::string());
        if (family.empty() || file.empty()) {
            continue;
        }
        const std::string path =
            file[0] == '/' || file[0] == '\\' || file.find(":\\") != std::string::npos
                ? file
                : zdt::join_path(repo_root, file);
        zui::text::register_embedded_font(family, path);
        faces.push_back(zui::backends::ProbeFontFace{family, path});
        registered.push_back(family + " <- " + path);
    }
    return registered;
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!parse_args(argc, argv, opt)) {
        return 2;
    }
    std::vector<std::string> embedded_fonts;
    std::vector<zui::backends::ProbeFontFace> probe_fonts;
    zui::HostMediaFacts replay_media = capture_browser_media();
    {
        std::string font_error;
        const std::string env_path =
            opt.env_path.empty()
                ? zdt::join_path(zdt::join_path(opt.repo_root, "tests/difftest"), "env.json")
                : opt.env_path;
        embedded_fonts = register_embedded_fonts(opt.repo_root, env_path, font_error, probe_fonts);
        if (!font_error.empty()) {
            std::cerr << font_error << "\n";
            return 2;
        }
        replay_media = load_replay_host_media(env_path);
    }

    // ---- L0 probe ---------------------------------------------------------
    // Reads the two environment parameters that everything else depends on: the platform
    // scrollbar thickness and the advance of one probe string in one face. On the browser side
    // the same two numbers come from clientWidth and a Range rect.
    if (opt.probe) {
        Json out = Json::object();
        out["schema"] = "zui.difftest.probe/1";
        out["engine"] = "zui";
        out["warning"] = "probe page not loaded";
        if (!opt.page_override.empty()) {
            zui::html::Document doc;
            zui::layout::FreeTypeTextMetrics metrics;
            std::string error;
            if (zui::html::load_file(opt.page_override, doc, error)) {
                zui::css::Engine css;
                css.load_sheets(doc, error);
                css.cascade(doc, 800, 600);
                zui::layout::PlatformMetrics platform;
                platform.scrollbar_thickness = opt.scrollbar_thickness;
                zui::layout::LayoutEngine layout(metrics, platform);
                layout.layout(doc, zui::layout::Viewport{800, 600});
                float thickness = 0;
                if (layout.root() != nullptr) {
                    const zui::layout::Box& root = *layout.root();
                    const float padding_w =
                        std::max(0.0f, root.geo.width - root.geo.border.horizontal() -
                                           root.geo.padding.horizontal());
                    thickness = 800.0f - padding_w;
                }
                out["scrollbar_thickness_px"] = thickness;
                out["scrollbar_thickness_declared"] = platform.scrollbar_thickness;
                out["advance_px"] =
                    metrics.measure(opt.probe_text, opt.probe_font_px, opt.probe_family).width;
                out["advance_spaces_px"] =
                    metrics.measure(opt.probe_spaces_text, opt.probe_font_px, opt.probe_family)
                        .width;
                out["advance_kern_px"] =
                    metrics.measure(opt.probe_kern_text, opt.probe_font_px, opt.probe_family).width;
                out.erase("warning");
            } else {
                out["error"] = error;
            }
        }
        out["text"] = opt.probe_text;
        out["font_px"] = opt.probe_font_px;
        out["font_family"] = opt.probe_family;
        out["spaces_text"] = opt.probe_spaces_text;
        out["kern_text"] = opt.probe_kern_text;
        out["embedded_fonts"] = embedded_fonts;
        std::cout << out.dump(2) << "\n";
        return 0;
    }

    const std::string case_dir = parent_dir(opt.case_path);
    std::string error;
    std::string case_text;
    if (!zdt::read_file(opt.case_path, case_text, error)) {
        std::cerr << error << "\n";
        return 1;
    }
    Json case_json;
    try {
        case_json = Json::parse(case_text, nullptr, true, true);
    } catch (const std::exception& e) {
        std::cerr << "case.json parse: " << e.what() << "\n";
        return 1;
    }

    // L1 property list: the case may narrow it; props.json supplies the default and the
    // initial values so an unspecifed property lands on the same value Chrome reports.
    zdt::TraceOptions trace_options;
    trace_options.engine = "zui";
    trace_options.engine_version = "0.1.0";
    const std::string props_path =
        opt.props_path.empty()
            ? zdt::join_path(zdt::join_path(opt.repo_root, "tests/difftest"), "props.json")
            : opt.props_path;
    std::string props_text;
    if (zdt::read_file(props_path, props_text, error)) {
        try {
            const Json props = Json::parse(props_text, nullptr, true, true);
            for (const auto& prop : props.value("default_props", Json::array())) {
                trace_options.style_props.push_back(prop.get<std::string>());
            }
            if (props.contains("initial")) {
                for (auto it = props["initial"].begin(); it != props["initial"].end(); ++it) {
                    trace_options.initial[it.key()] = it.value().get<std::string>();
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "props.json parse: " << e.what() << "\n";
            return 1;
        }
    } else {
        std::cerr << "warning: " << error << "\n";
    }
    if (case_json.contains("style_props")) {
        trace_options.style_props.clear();
        for (const auto& prop : case_json["style_props"]) {
            trace_options.style_props.push_back(prop.get<std::string>());
        }
    }
    if (case_json.contains("text")) {
        trace_options.include_breaks = case_json["text"].value("breaks", false);
        trace_options.include_advances = case_json["text"].value("advances", false);
    }

    std::string page_path =
        !opt.page_override.empty()
            ? opt.page_override
            : zdt::join_path(case_dir, case_json.value("page", std::string("page.html")));

    // The engine opens a replaced element's `src` through image::load(), which is a plain fopen
    // against the process working directory, while `<link href>` is resolved against the
    // document. The example apps only work because the build copies their assets next to the
    // executable. Standing in the page's directory is the same contract, and without it every
    // relative <img> silently falls back to the 300x150 placeholder and the trace is meaningless.
    // Absolute paths are unaffected, and the driver only ever serves one page per process.
    {
        std::error_code fs_error;
        const std::filesystem::path absolute_page =
            std::filesystem::absolute(page_path, fs_error).lexically_normal();
        if (!fs_error) {
            // Resolve before entering the directory: a relative --case would otherwise be read
            // against the page's own directory after the chdir below.
            page_path = absolute_page.string();
            const std::filesystem::path page_dir = absolute_page.parent_path();
            std::filesystem::current_path(page_dir, fs_error);
            if (fs_error) {
                std::cerr << "warning: cannot enter " << page_dir.string() << "\n";
            } else if (!opt.quiet) {
                std::cout << "workdir " << page_dir.string() << "\n";
            }
        }
    }

    zui::html::Document doc;
    zui::layout::FreeTypeTextMetrics metrics;
    zui::layout::PlatformMetrics platform;
    platform.scrollbar_thickness = opt.scrollbar_thickness;
    EngineRunner runner(doc, metrics, trace_options, platform);
    runner.set_host_media(replay_media);
    runner.set_scrollbar_thickness(opt.scrollbar_thickness);
    zui::html::Node* hovered = nullptr;
    zui::html::Node* focused = nullptr;
    zui::html::Node* scroll_drag = nullptr;
    zui::input::InteractionHost input_host;
    input_host.document = &runner.doc();
    input_host.layout = &runner.layout();
    input_host.metrics = &runner.metrics();
    input_host.scrollbar_thickness = opt.scrollbar_thickness;
    input_host.hovered = &hovered;
    input_host.focused = &focused;
    input_host.scroll_drag = &scroll_drag;
    // The same gate the app uses: a pointer move only re-cascades when a loaded `:hover` rule
    // could match the chain that is entering or leaving hover.
    input_host.hover_styles = [&runner](const zui::html::Node& node) {
        return runner.css().hover_may_change_style(node);
    };
    zui::input::Interaction interaction(std::move(input_host));
    const Json viewport = case_json.value("viewport", Json::array({800, 600}));
    if (viewport.is_array() && viewport.size() == 2) {
        runner.set_viewport(viewport[0].get<float>(), viewport[1].get<float>());
    }

    Json summary = Json::object();
    summary["schema"] = "zui.difftest.engine-summary/1";
    summary["case"] = case_json.value("name", std::string("unnamed"));
    summary["embedded_fonts"] = embedded_fonts;
    Json steps_out = Json::array();
    Json artifacts = Json::array();
    Json unsupported = Json::array();
    Json errors = Json::array();
    int trace_count = 0;
    int capture_count = 0;

    std::size_t index = 0;
    for (const auto& step : case_json["steps"]) {
        const std::string op = step.value("op", std::string());
        const std::string name = step_name(index, op);
        ++index;
        runner.begin_step();
        if (kUnsupportedOps.count(op) != 0) {
            unsupported.push_back(Json{
                {"op", op}, {"step", name}, {"reason", "engine has no test-API equivalent yet"}});
            continue;
        }
        if (op == "load") {
            runner.load(page_path, error);
            if (!error.empty()) {
                errors.push_back(Json{{"step", name}, {"error", error}});
                break;
            }
        } else if (op == "resize") {
            runner.set_viewport(step.value("w", 800.0f), step.value("h", 600.0f));
            runner.frame(true);
        } else if (op == "eval") {
            bool ok = true;
            for (const auto& action : step["actions"]) {
                std::string action_error;
                if (!runner.apply_action(action, action_error)) {
                    errors.push_back(Json{{"step", name}, {"error", action_error}});
                    ok = false;
                    break;
                }
            }
            if (!ok) {
                break;
            }
            runner.frame(true);
            // A step that declares a readback reports the state its actions produced. Scroll
            // anchoring (TESTPLAN section 4) is asserted this way: the step that grows the
            // scroller has to show the offset absorbing the growth, and a `scroll` step cannot
            // carry that - it would set the offset it is supposed to observe.
            if (step.value("readback", false)) {
                zui::html::Node* readback_target =
                    runner.resolve(step.value("target", std::string("window")));
                const Json state = readback_state(
                    runner.doc(), runner.layout(), interaction, step, "eval", readback_target,
                    opt.scrollbar_thickness, runner.viewport_w(), runner.viewport_h());
                const std::string path = zdt::join_path(opt.out_dir, name + ".state.json");
                if (!zdt::write_file(path, state.dump(2), error)) {
                    errors.push_back(Json{{"step", name}, {"error", error}});
                    break;
                }
                artifacts.push_back(path);
            }
        } else if (op == "scroll") {
            const std::string target = step.value("target", std::string("window"));
            zui::html::Node* node = runner.resolve(target);
            if (node == nullptr) {
                errors.push_back(Json{{"step", name}, {"error", "scroll target not found"}});
                break;
            }
            zui::layout::Box* box = runner.layout().principal_box(node);
            if (box == nullptr) {
                box = runner.layout().root();
            }
            // Resolve content-visibility for the offset already on screen before applying this
            // step's offset. A browser does that on the frame after the previous scroll, and the
            // trace taken immediately after a scroll still shows those sizes. Doing it after
            // set_scroll would accept the new band and move the clamp the golden does not.
            if (runner.layout().uses_content_visibility()) {
                runner.layout().layout(runner.doc(), zui::layout::Viewport{runner.viewport_w(),
                                                                           runner.viewport_h()});
            }
            // Document / :root scroll uses the viewport client size as the scrollport (same as
            // keyboard page scroll in interaction.cpp). The root border box is the content size,
            // so using it as view_h clamps max_scroll to 0 on a tall page.
            if (box == runner.layout().root() &&
                (target.empty() || target == "window" || target == ":root" || target == "html")) {
                float view_w = 0.f;
                float view_h = 0.f;
                zui::input::document_client_size(
                    runner.viewport_w(), runner.viewport_h(), box->geo.scroll_width,
                    box->geo.scroll_height, opt.scrollbar_thickness, box->geo.border.horizontal(),
                    box->geo.border.vertical(), view_w, view_h);
                zui::input::set_scroll(*box, step.value("x", 0.0f), step.value("y", 0.0f), view_w,
                                       view_h);
            } else {
                set_scroll(box, step.value("x", 0.0f), step.value("y", 0.0f),
                           opt.scrollbar_thickness);
            }
            runner.layout().sync_world();
            {
                const Json state = readback_state(runner.doc(), runner.layout(), interaction, step,
                                                  "scroll", node, opt.scrollbar_thickness,
                                                  runner.viewport_w(), runner.viewport_h());
                const std::string path = zdt::join_path(opt.out_dir, name + ".state.json");
                if (!zdt::write_file(path, state.dump(2), error)) {
                    errors.push_back(Json{{"step", name}, {"error", error}});
                    break;
                }
                artifacts.push_back(path);
            }
        } else if (op == "scroll_into_view") {
            const std::string target = step.value("target", std::string());
            zui::html::Node* node = runner.resolve(target);
            if (node == nullptr) {
                errors.push_back(Json{{"step", name}, {"error", "scroll_into_view target not found"}});
                break;
            }
            const std::string block_name = step.value("block", std::string("start"));
            zui::ScrollAlign align = zui::ScrollAlign::Start;
            if (block_name == "center") {
                align = zui::ScrollAlign::Center;
            } else if (block_name == "end") {
                align = zui::ScrollAlign::End;
            } else if (block_name == "nearest") {
                align = zui::ScrollAlign::Nearest;
            } else if (block_name != "start") {
                errors.push_back(Json{{"step", name},
                                      {"error", "scroll_into_view block must be start, center, end, or nearest"}});
                break;
            }
            zui::input::scroll_into_view(runner.layout(), *node, align, opt.scrollbar_thickness,
                                         nullptr);
            runner.layout().sync_world();
            {
                const Json state = readback_state(runner.doc(), runner.layout(), interaction, step,
                                                  "scroll_into_view", node, opt.scrollbar_thickness,
                                                  runner.viewport_w(), runner.viewport_h());
                const std::string path = zdt::join_path(opt.out_dir, name + ".state.json");
                if (!zdt::write_file(path, state.dump(2), error)) {
                    errors.push_back(Json{{"step", name}, {"error", error}});
                    break;
                }
                artifacts.push_back(path);
            }
        } else if (op == "dispatch") {
            for (const auto& event : step.value("events", Json::array())) {
                const std::string type = event.value("type", std::string());
                const float x = event.value("x", 0.0f);
                const float y = event.value("y", 0.0f);
                if (type == "mouseWheel") {
                    interaction.wheel(x, y, event.value("deltaX", 0.0f),
                                      event.value("deltaY", 0.0f), event.value("deltaMode", 0));
                } else if (type == "keyDown" || type == "keyUp") {
                    interaction.key(type, event.value("key", std::string()));
                } else {
                    interaction.pointer_button(type, x, y,
                                               event.value("button", std::string("left")),
                                               event.value("count", 1));
                }
            }
            if (interaction.style_dirty()) {
                interaction.clear_style_dirty();
                runner.frame(true);
            } else {
                runner.layout().sync_world();
            }
            {
                const Json state = readback_state(
                    runner.doc(), runner.layout(), interaction, step, "dispatch",
                    runner.resolve(step.value("target", std::string("window"))),
                    opt.scrollbar_thickness, runner.viewport_w(), runner.viewport_h());
                const std::string path = zdt::join_path(opt.out_dir, name + ".state.json");
                if (!zdt::write_file(path, state.dump(2), error)) {
                    errors.push_back(Json{{"step", name}, {"error", error}});
                    break;
                }
                artifacts.push_back(path);
            }
        } else if (op == "freeze" || op == "advance") {
            const double ms = step.value("t", step.value("dt", 0.0));
            runner.set_clock_ms(ms);
            runner.frame(false);
        } else if (op == "trace") {
            const zdt::TraceResult result =
                zdt::export_trace(runner.doc(), runner.layout(), runner.viewport_w(),
                                  runner.viewport_h(), trace_options);
            const std::string path = zdt::join_path(opt.out_dir, name + ".trace.json");
            if (!zdt::write_file(path, result.trace.dump(2), error)) {
                errors.push_back(Json{{"step", name}, {"error", error}});
                break;
            }
            artifacts.push_back(path);
            ++trace_count;
            if (!opt.quiet) {
                std::cout << "trace " << name << " elements=" << result.elements
                          << " text_owners=" << result.text_owners << " lines=" << result.line_boxes
                          << "\n";
            }
        } else if (op == "capture") {
            // Interaction has already landed on the document through source/input/Interaction.
            // This step only paints that state and reads the GL framebuffer. It does not poll
            // the hidden context window.
            const int w = static_cast<int>(std::lround(runner.viewport_w()));
            const int h = static_cast<int>(std::lround(runner.viewport_h()));
            const zui::paint::DisplayList list = runner.display_list(w, h);
            std::vector<std::uint8_t> rgba;
            const char* which = std::getenv("ZUI_PROBE_RENDERER");
            const bool use_vk = which != nullptr && std::string(which) == "vk";
            const zui::backends::ProbeFrame frame =
                use_vk ? zui::backends::probe_read_frame_vk(list, w, h, probe_fonts, rgba)
                       : zui::backends::probe_read_frame(list, w, h, probe_fonts, rgba);
            if (frame == zui::backends::ProbeFrame::Unavailable) {
                unsupported.push_back(Json{{"op", "capture"},
                                           {"step", name},
                                           {"reason", "framebuffer readback is not implemented"}});
                if (!opt.quiet) {
                    std::cout << "capture " << name << " skipped: no framebuffer readback\n";
                }
            } else if (frame != zui::backends::ProbeFrame::Ok) {
                errors.push_back(Json{{"step", name}, {"error", "framebuffer readback failed"}});
                break;
            } else {
                std::string png;
                if (!zdt::encode_rgba_png(rgba.data(), w, h, png)) {
                    errors.push_back(Json{{"step", name}, {"error", "png encode failed"}});
                    break;
                }
                const std::string path = zdt::join_path(opt.out_dir, name + ".png");
                if (!zdt::write_file(path, png, error)) {
                    errors.push_back(Json{{"step", name}, {"error", error}});
                    break;
                }
                artifacts.push_back(path);
                ++capture_count;
                if (!opt.quiet) {
                    std::cout << "capture " << name << " " << w << "x" << h << "\n";
                }
            }
        } else {
            errors.push_back(Json{{"step", name}, {"error", "unknown op '" + op + "'"}});
            break;
        }
        // LAYOUT.md section 16 diagnostics ride beside the trace, never inside it: the browser
        // has no equivalent, and golden must stay a two-sided comparison.
        {
            const EngineRunner::StepStats& stats = runner.step_stats();
            Json diagnostics = zdt::export_diagnostics(runner.doc(), runner.layout(), name,
                                                       runner.non_convergence_notes());
            Json step_counters = Json::object();
            step_counters["frames"] = stats.frames;
            step_counters["dirty_marks"] = stats.dirty_marks;
            step_counters["laid_out"] = stats.laid_out;
            step_counters["cache_hits"] = stats.cache_hits;
            step_counters["evicted_measure_slots"] = stats.evicted_measure_slots;
            step_counters["container_requery_rounds"] = stats.container_requery_rounds;
            step_counters["interned_styles"] = stats.interned_styles;
            diagnostics["step"] = std::move(step_counters);
            const std::string diagnostics_path =
                zdt::join_path(opt.out_dir, name + ".diagnostics.json");
            if (!zdt::write_file(diagnostics_path, diagnostics.dump(2), error)) {
                errors.push_back(Json{{"step", name}, {"error", error}});
                break;
            }
            steps_out.push_back(Json{{"step", name},
                                     {"op", op},
                                     {"status", "ok"},
                                     {"frames", stats.frames},
                                     {"dirty_marks", stats.dirty_marks},
                                     {"converged", diagnostics["converged"]},
                                     {"non_converged", diagnostics["non_converged"].size()}});
        }
    }

    summary["steps"] = std::move(steps_out);
    summary["artifacts"] = std::move(artifacts);
    summary["unsupported"] = std::move(unsupported);
    summary["errors"] = std::move(errors);
    summary["traces"] = trace_count;
    summary["captures"] = capture_count;
    summary["status"] = errors.empty() ? "ok" : "error";

    if (!zdt::write_file(zdt::join_path(opt.out_dir, "engine-summary.json"), summary.dump(2),
                         error)) {
        std::cerr << error << "\n";
        return 1;
    }
    if (!opt.quiet) {
        std::cout << summary.dump(2) << "\n";
    }
    return errors.empty() ? 0 : 1;
}
