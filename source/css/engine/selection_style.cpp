// Highlight pseudo-elements: ::selection and ::backdrop. Unlike the generated elements, neither
// gets a node (no box); the cascade stores a compact contribution and commit merges the parent
// highlight, fills paired defaults, and writes the compact node fields paint reads.

#include "css/engine/cascade_internal.h"
#include "css/shorthand/shorthand.h"
#include "css/values.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace zui {
namespace css {
namespace detail {
bool selection_prop(std::string_view name) { return name == "color" || name == "background-color"; }

bool is_css_wide_keyword(std::string_view value) {
    return value == "inherit" || value == "initial" || value == "unset" || value == "revert" ||
           value == "revert-layer";
}

/// Resolves one ::selection DeclSink into this node's cascaded contribution only (no parent
/// highlight merge, no currentColor fill). Does not copy inheritable colour from the element —
/// that would break paired defaults / A1. `originating` supplies var() / currentcolor context.
std::shared_ptr<const html::SelectionStyle>
finalize_selection_sink(DeclSink& sink, const StyleMap* originating, const MediaState& media) {
    if (!sink.matched()) {
        return nullptr;
    }
    CustomLookup lookup = [&](std::string_view name) -> std::optional<std::string> {
        if (originating == nullptr) {
            return std::nullopt;
        }
        auto it = originating->find(std::string(name));
        if (it == originating->end()) {
            return std::nullopt;
        }
        return it->second.str();
    };
    if (!sink.pending.empty()) {
        for (const PendingShort& p : sink.pending) {
            std::optional<std::string> sub = substitute(p.value, lookup, media);
            if (!sub) {
                continue;
            }
            std::vector<std::pair<std::string, std::string>> parts;
            if (!expand_shorthand_value(p.name.str(), *sub, parts)) {
                continue;
            }
            for (const auto& part : parts) {
                if (!selection_prop(part.first)) {
                    continue;
                }
                bool replace = true;
                auto existing = sink.win.find(part.first);
                if (existing != sink.win.end()) {
                    const Winner& slot = existing->second;
                    const DeclKey next{cascade_layer(p.origin, p.important), p.element_attached,
                                       p.layer, p.spec, p.order};
                    const DeclKey current{cascade_layer(slot.origin, slot.important),
                                          slot.element_attached, slot.layer, slot.spec, slot.order};
                    replace = declaration_wins(next, current);
                }
                if (!replace) {
                    continue;
                }
                Winner& slot = sink.win[part.first];
                slot.value = part.second;
                slot.spec = p.spec;
                slot.order = p.order;
                slot.origin = p.origin;
                slot.important = p.important;
                slot.layer = p.layer;
                slot.element_attached = p.element_attached;
            }
        }
    }

    auto resolve_value = [&](std::string value) -> std::optional<std::string> {
        if (is_css_wide_keyword(value)) {
            // inherit/unset → parent highlight at commit; initial/revert → no cascaded value.
            return std::nullopt;
        }
        if (contains_substitution(value)) {
            std::optional<std::string> sub = substitute(value, lookup, media);
            if (!sub) {
                return std::nullopt;
            }
            value = std::move(*sub);
            if (is_css_wide_keyword(value)) {
                return std::nullopt;
            }
        }
        return value;
    };

    auto is_currentcolor = [](std::string_view text) {
        if (text.size() != 12) {
            return false;
        }
        const char* expect = "currentcolor";
        for (size_t i = 0; i < 12; ++i) {
            if (std::tolower(static_cast<unsigned char>(text[i])) !=
                static_cast<unsigned char>(expect[i])) {
                return false;
            }
        }
        return true;
    };
    auto parse_with_current = [&](std::string_view text) -> std::optional<CssRgba> {
        if (is_currentcolor(text)) {
            if (originating == nullptr) {
                return std::nullopt;
            }
            const auto it = originating->find("color");
            if (it == originating->end()) {
                return CssRgba{0, 0, 0, 255};
            }
            return parse_css_color(it->second);
        }
        return parse_css_color(text);
    };

    html::SelectionStyle out;
    auto take = [&](const char* prop, bool fg) {
        const auto it = sink.win.find(prop);
        if (it == sink.win.end()) {
            return;
        }
        std::optional<std::string> value = resolve_value(it->second.value.str());
        if (!value) {
            return;
        }
        std::optional<CssRgba> rgba = parse_with_current(*value);
        if (!rgba) {
            return;
        }
        const bool author = it->second.origin == kOriginAuthor;
        if (fg) {
            out.fg_r = rgba->r;
            out.fg_g = rgba->g;
            out.fg_b = rgba->b;
            out.fg_a = rgba->a;
            out.flags |= html::SelectionStyle::kHasFg;
            if (author) {
                out.flags |= html::SelectionStyle::kAuthorFg;
            }
        } else {
            out.bg_r = rgba->r;
            out.bg_g = rgba->g;
            out.bg_b = rgba->b;
            out.bg_a = rgba->a;
            out.flags |= html::SelectionStyle::kHasBg;
            if (author) {
                out.flags |= html::SelectionStyle::kAuthorBg;
            }
        }
    };
    take("color", true);
    take("background-color", false);
    if (out.flags == 0) {
        return nullptr;
    }
    return std::make_shared<html::SelectionStyle>(out);
}

bool selection_equal(const html::SelectionStyle* a, const html::SelectionStyle& b) {
    if (a == nullptr) {
        return b.flags == 0;
    }
    return a->flags == b.flags && a->fg_r == b.fg_r && a->fg_g == b.fg_g && a->fg_b == b.fg_b &&
           a->fg_a == b.fg_a && a->bg_r == b.bg_r && a->bg_g == b.bg_g && a->bg_b == b.bg_b &&
           a->bg_a == b.bg_a;
}

CssRgba originating_current_color(const html::Node* node) {
    if (node == nullptr || node->computed == nullptr) {
        return CssRgba{0, 0, 0, 255};
    }
    const auto it = node->computed->find("color");
    if (it == node->computed->end()) {
        return CssRgba{0, 0, 0, 255};
    }
    if (std::optional<CssRgba> c = parse_css_color(it->second)) {
        return *c;
    }
    return CssRgba{0, 0, 0, 255};
}

/// Merges this node's cascaded ::selection contribution with the parent highlight, applies paired
/// defaults / currentColor fill, and writes `node->selection`. Paint-only dirty.
void commit_selection(html::Node* node,
                      const std::shared_ptr<const html::SelectionStyle>& contrib) {
    CascadeAcc share_clock(cascade_split_armed() ? &g_cascade_split.share_ms : nullptr);
    html::SelectionStyle merged;
    if (contrib != nullptr) {
        merged = *contrib;
    }
    const html::SelectionStyle* parent =
        node->parent != nullptr ? node->parent->selection.get() : nullptr;
    // Highlight inheritance (§3.5): missing bg ← parent highlight (incl. author_bg bit).
    if ((merged.flags & html::SelectionStyle::kHasBg) == 0 && parent != nullptr &&
        (parent->flags & html::SelectionStyle::kHasBg) != 0) {
        merged.bg_r = parent->bg_r;
        merged.bg_g = parent->bg_g;
        merged.bg_b = parent->bg_b;
        merged.bg_a = parent->bg_a;
        merged.flags |= html::SelectionStyle::kHasBg;
        if ((parent->flags & html::SelectionStyle::kAuthorBg) != 0) {
            merged.flags |= html::SelectionStyle::kAuthorBg;
        }
    }
    // Author fg may inherit. A parent's currentColor fill (has_fg without author_fg) must not
    // (A1) — children resolve their own originating colour instead.
    if ((merged.flags & html::SelectionStyle::kHasFg) == 0 && parent != nullptr &&
        (parent->flags & html::SelectionStyle::kAuthorFg) != 0) {
        merged.fg_r = parent->fg_r;
        merged.fg_g = parent->fg_g;
        merged.fg_b = parent->fg_b;
        merged.fg_a = parent->fg_a;
        merged.flags |= html::SelectionStyle::kHasFg | html::SelectionStyle::kAuthorFg;
    }

    const bool author_fg = (merged.flags & html::SelectionStyle::kAuthorFg) != 0;
    const bool author_bg = (merged.flags & html::SelectionStyle::kAuthorBg) != 0;
    if (!author_fg && !author_bg) {
        // Sentinel for the UA Highlight / HighlightText pair at paint time.
        if (node->selection != nullptr) {
            node->selection.reset();
            html::note_paint_change_at(node);
        }
        return;
    }
    // One authored side suppresses the UA pair (§3.3.1).
    if ((merged.flags & html::SelectionStyle::kHasFg) == 0) {
        const CssRgba fg = originating_current_color(node);
        merged.fg_r = fg.r;
        merged.fg_g = fg.g;
        merged.fg_b = fg.b;
        merged.fg_a = fg.a;
        merged.flags |= html::SelectionStyle::kHasFg;
    }
    if ((merged.flags & html::SelectionStyle::kHasBg) == 0) {
        merged.bg_r = 0;
        merged.bg_g = 0;
        merged.bg_b = 0;
        merged.bg_a = 0;
        merged.flags |= html::SelectionStyle::kHasBg;
    }

    if (selection_equal(node->selection.get(), merged)) {
        return;
    }
    node->selection = std::make_unique<html::SelectionStyle>(merged);
    html::note_paint_change_at(node);
}

/// `::backdrop` only paints for modal top-layer elements. Non-modal nodes drop any cascaded map.
void commit_backdrop(html::Node* node, const std::shared_ptr<const StyleMap>& map) {
    if (node == nullptr) {
        return;
    }
    std::shared_ptr<const StyleMap> next;
    if (node->modal && map != nullptr) {
        next = map;
    }
    if (node->backdrop == next) {
        return;
    }
    if (node->backdrop != nullptr && next != nullptr && *node->backdrop == *next) {
        node->backdrop = next;
        return;
    }
    node->backdrop = std::move(next);
    html::note_paint_change_at(node);
}
}  // namespace detail
}  // namespace css
}  // namespace zui
