#ifndef ZUI_DIFFTEST_TRACE_EXPORT_H
#define ZUI_DIFFTEST_TRACE_EXPORT_H

// Engine-side trace export (LAYOUT.md section 16, trace export; TESTPLAN.md section 9).
//
// The observation channel must not change the layout path: everything here reads the tree
// after the frame has finished and never writes into LayoutResult, Placement or the key.
//
// JSON is nlohmann::ordered_json (vendored in third_party/nlohmann_json, wired in
// cmake/deps.cmake). Ordered rather than the default map-backed type, so a trace reads in
// DOM order and a raw file diff stays meaningful.

#include <nlohmann/json.hpp>

#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace zui {
namespace html {
struct Document;
}
namespace layout {
struct Box;
class LayoutEngine;
}
}  // namespace zui

namespace zdt {

using Json = nlohmann::ordered_json;

struct TraceOptions {
    /// Property list for the L1 dump. Values come from Node::computed, with `initial` used
    /// when the engine did not materialize the property, the same contract getComputedStyle
    /// has on the browser side.
    std::vector<std::string> style_props;
    std::map<std::string, std::string> initial;
    bool include_lines = true;
    bool include_breaks = false;
    bool include_advances = false;
    std::string engine = "zui";
    std::string engine_version;
};

struct TraceResult {
    Json trace;
    std::size_t elements = 0;
    std::size_t text_owners = 0;
    std::size_t line_boxes = 0;
};

/// One box a step saw report `converged == false`, with the source. A multi-frame step (the
/// driver settles several frames after `load`) has to carry these forward: the frame that
/// finally serves a cache hit no longer knows why an earlier frame gave up.
struct NonConvergenceNote {
    const zui::layout::Box* box = nullptr;
    std::string reason;
};

/// Builds the L2/L1 trace for a finished frame. Lengths are quantized to 1/64 px.
TraceResult export_trace(zui::html::Document& doc, zui::layout::LayoutEngine& layout,
                         float viewport_w, float viewport_h, const TraceOptions& options);

/// LAYOUT.md section 16 diagnostics for the frame that just finished: the boxes whose
/// `converged` is false together with the source, and the frame counters. This is
/// engine-only - the browser has no equivalent - so it is written beside the trace and
/// never inside it, and golden never carries it.
///
/// `extra_non_converged` carries boxes a multi-frame step saw in an earlier frame; the
/// diagnostics of the last frame alone would drop them.
Json export_diagnostics(zui::html::Document& doc, zui::layout::LayoutEngine& layout,
                        const std::string& step,
                        const std::vector<NonConvergenceNote>& extra_non_converged);

/// Length quantization shared with LAYOUT section 12 (1/64 px).
float quantize(float value);

/// CSS value normalization (the four rules in props.json). Identical on the browser side.
std::string normalize_style_value(std::string_view value);

/// FNV-1a 64 of the normalized `prop=value;` stream, as 16 lowercase hex digits.
std::string style_digest(const std::vector<std::pair<std::string, std::string>>& entries);

}  // namespace zdt

#endif
