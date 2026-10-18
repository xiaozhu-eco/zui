// Declaration expansion cache, shorthand pending, and winner publish.

#include "ascii.h"
#include "css/engine/cascade_internal.h"
#include "css/shorthand/shorthand.h"
#include "css/values.h"

#include <lexbor/css/declaration.h>

#include "perf/perf_budget.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace zui {
namespace css {
namespace detail {

namespace {

/// `std::string` growth under the debug heap was the first-cascade expand cost.
struct StackBuf {
    char data[384];
    std::size_t n = 0;
    bool overflow = false;
    std::string heap;
};

lxb_status_t stack_append(const lxb_char_t* data, size_t len, void* ctx) {
    auto* buf = static_cast<StackBuf*>(ctx);
    if (!buf->overflow && buf->n + len <= sizeof(buf->data)) {
        if (len != 0 && data != nullptr) {
            std::memcpy(buf->data + buf->n, data, len);
            buf->n += len;
        }
        return LXB_STATUS_OK;
    }
    if (!buf->overflow) {
        buf->overflow = true;
        buf->heap.assign(buf->data, buf->n);
    }
    if (len != 0 && data != nullptr) {
        buf->heap.append(reinterpret_cast<const char*>(data), len);
    }
    return LXB_STATUS_OK;
}


}  // namespace

/// One declaration's longhands, expanded once per cascade. The same UA rule is applied to every
/// miss; serializing it again per node was the apply cost.
struct CachedDecl {
    bool deferred = false;
    std::uint8_t fin_flags = 0;
    ::zui::Atom defer_name;
    ::zui::Atom defer_value;
    std::vector<std::pair<::zui::Atom, ::zui::Atom>> parts;
};

void note_fin_flags(CachedDecl& exp) {
    auto note = [&](std::string_view name, std::string_view value) {
        if (is_custom_property(name)) {
            exp.fin_flags = static_cast<std::uint8_t>(exp.fin_flags | DeclSink::kFinCustom);
        }
        if (name.find("inline") != std::string_view::npos ||
            name.find("-block") != std::string_view::npos) {
            exp.fin_flags = static_cast<std::uint8_t>(exp.fin_flags | DeclSink::kFinLogical);
        }
        if (value == "inherit" || value == "initial" || value == "unset") {
            exp.fin_flags = static_cast<std::uint8_t>(exp.fin_flags | DeclSink::kFinWide);
        }
        if (value == "revert" || value == "revert-layer" || value == "revert-rule") {
            exp.fin_flags = static_cast<std::uint8_t>(exp.fin_flags | DeclSink::kFinRevert);
        }
        if (contains_substitution(value)) {
            exp.fin_flags = static_cast<std::uint8_t>(exp.fin_flags | DeclSink::kFinSub);
        }
    };
    if (exp.deferred) {
        note(exp.defer_name, exp.defer_value);
        return;
    }
    for (const auto& part : exp.parts) {
        note(part.first, part.second);
    }
}

void adopt_parts(CachedDecl& exp, std::vector<std::pair<std::string, std::string>>& raw) {
    exp.parts.clear();
    exp.parts.reserve(raw.size());
    for (auto& part : raw) {
        exp.parts.emplace_back(::zui::Atom(std::move(part.first)), ::zui::Atom(std::move(part.second)));
    }
}

/// Open-addressed cache. Slots are indexed through `slot_data` so the hit path never calls
/// `vector::operator[]`. Entries live until a stylesheet is destroyed: declaration pointers are
/// stable across cascades of the same sheets, and rebuilding the longhands on every cascade was
/// the apply cost. Style attributes pass `cache=false` and never enter this table.
struct DeclExpCache {
    struct Slot {
        const lxb_css_rule_declaration_t* key = nullptr;
        std::uint32_t index = 0;
    };
    std::vector<const lxb_css_rule_declaration_t*> keys;
    std::vector<CachedDecl> values;
    std::vector<Slot> slots;
    Slot* slot_data = nullptr;
    CachedDecl* value_data = nullptr;
    std::uint32_t mask = 0;
    std::size_t live = 0;

    void clear() {
        keys.clear();
        values.clear();
        slots.clear();
        slot_data = nullptr;
        value_data = nullptr;
        mask = 0;
        live = 0;
    }

    void rehash(std::size_t nslots) {
        std::vector<Slot> next(nslots);
        Slot* s = next.data();
        const std::uint32_t m = static_cast<std::uint32_t>(nslots - 1);
        const std::size_t n = keys.size();
        const lxb_css_rule_declaration_t* const* ks = keys.data();
        for (std::size_t k = 0; k < n; ++k) {
            const lxb_css_rule_declaration_t* key = ks[k];
            std::uintptr_t h = reinterpret_cast<std::uintptr_t>(key);
            h >>= 4;
            h *= static_cast<std::uintptr_t>(0x9E3779B1u);
            h ^= h >> 16;
            std::uint32_t i = static_cast<std::uint32_t>(h) & m;
            while (s[i].key != nullptr) {
                i = (i + 1) & m;
            }
            s[i].key = key;
            s[i].index = static_cast<std::uint32_t>(k);
        }
        slots.swap(next);
        slot_data = slots.data();
        mask = m;
        value_data = values.data();
    }

    CachedDecl& insert(const lxb_css_rule_declaration_t* key, CachedDecl exp) {
        if (slots.empty() || (live + 1) * 10 >= slots.size() * 7) {
            rehash(slots.empty() ? 64 : slots.size() * 2);
        }
        values.push_back(std::move(exp));
        value_data = values.data();
        const std::uint32_t index = static_cast<std::uint32_t>(values.size() - 1);
        keys.push_back(key);
        Slot* s = slot_data;
        const std::uint32_t m = mask;
        std::uintptr_t h = reinterpret_cast<std::uintptr_t>(key);
        h >>= 4;
        h *= static_cast<std::uintptr_t>(0x9E3779B1u);
        h ^= h >> 16;
        std::uint32_t i = static_cast<std::uint32_t>(h) & m;
        while (s[i].key != nullptr) {
            i = (i + 1) & m;
        }
        s[i].key = key;
        s[i].index = index;
        ++live;
        return value_data[index];
    }
};

DeclExpCache g_decl_exp;
bool g_record_decl_history = true;

void reset_decl_expansion(bool record_history) {
    g_record_decl_history = record_history;
}

void clear_decl_expansion() {
    g_decl_exp.clear();
}

std::string trim_copy(std::string text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.erase(text.begin());
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.pop_back();
    }
    return text;
}

/// Style attributes are parsed into a temporary stylesheet that is destroyed before the next
/// element. Lexbor reuses the declaration pointer, so caching those expansions makes every later
/// element with the same property read the first element's longhands (open `<option>` rows all
/// landed on the first row's `top`).
CachedDecl g_ephemeral_decl;

const CachedDecl& expansion_for(const lxb_css_rule_declaration_t* decl, std::string_view source,
                                bool cache) {
    if (cache) {
        DeclExpCache::Slot* s = g_decl_exp.slot_data;
        if (s != nullptr) {
            std::uintptr_t h = reinterpret_cast<std::uintptr_t>(decl);
            h >>= 4;
            h *= static_cast<std::uintptr_t>(0x9E3779B1u);
            h ^= h >> 16;
            const std::uint32_t m = g_decl_exp.mask;
            std::uint32_t i = static_cast<std::uint32_t>(h) & m;
            for (;;) {
                const lxb_css_rule_declaration_t* k = s[i].key;
                if (k == decl) {
                    return g_decl_exp.value_data[s[i].index];
                }
                if (k == nullptr) {
                    break;
                }
                i = (i + 1) & m;
            }
        }
    }
    CachedDecl exp;
    const std::string_view name = canonical_name(decl);
    std::string defer_name;
    std::string defer_value;
    std::vector<std::pair<std::string, std::string>> raw;
    bool placed = false;
    if (name.empty()) {
        // Leave `parts` empty so the caller skips the declaration.
    } else if (deferred_shorthand(decl, defer_name, defer_value)) {
        exp.deferred = true;
        exp.defer_name = std::move(defer_name);
        exp.defer_value = std::move(defer_value);
    } else if (name == "flex" && decl->offset.value_end > decl->offset.value_begin &&
               decl->offset.value_end <= source.size()) {
        // Lexbor keeps a `flex` shorthand whose basis is `calc()` / `min()` / `max()` /
        // `clamp()`, but the basis struct can only hold a length or a percentage, so the
        // expansion below would publish a unitless number. The stylesheet span is the
        // specified value; expand that instead. The parse can also leave the whole declaration
        // undefined (`LXB_CSS_PROPERTY__UNDEF`) with the span intact - `flex: 1 1 calc(100% -
        // 64px)` is one - so the name, not the property id, is the test that finds it.
        std::string specified(source.data() + decl->offset.value_begin,
                              decl->offset.value_end - decl->offset.value_begin);
        specified = trim_copy(std::move(specified));
        std::string folded = specified;
        for (char& c : folded) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        const bool math = folded.find("calc(") != std::string::npos ||
                          folded.find("min(") != std::string::npos ||
                          folded.find("max(") != std::string::npos ||
                          folded.find("clamp(") != std::string::npos;
        if (!(math && expand_flex_text(specified, raw))) {
            if (!expand_shorthand(decl, raw)) {
                std::string value = serialize_value(decl);
                raw.emplace_back(std::string(name), std::move(value));
            }
        }
    } else if (expand_shorthand(decl, raw)) {
        // Longhands came from the parsed shorthand. No value serialization.
    } else if (name == "content" && decl->offset.value_end > decl->offset.value_begin &&
               decl->offset.value_end <= source.size()) {
        // Lexbor's serializer drops `attr(name, "fallback")` down to `attr(name)`.
        std::string value(source.data() + decl->offset.value_begin,
                          decl->offset.value_end - decl->offset.value_begin);
        value = trim_copy(std::move(value));
        exp.parts.emplace_back(::zui::Atom(name), ::zui::Atom(std::move(value)));
        placed = true;
    } else if (decl->type == LXB_CSS_PROPERTY__CUSTOM && decl->u.custom != nullptr &&
               decl->u.custom->value.data != nullptr) {
        const std::string_view value{reinterpret_cast<const char*>(decl->u.custom->value.data),
                                     decl->u.custom->value.length};
        exp.parts.emplace_back(::zui::Atom(name), ::zui::Atom(value));
        placed = true;
    } else {
        StackBuf buf;
        (void)lxb_css_property_serialize(decl->u.user, static_cast<lxb_css_property_type_t>(decl->type),
                                         stack_append, &buf);
        const std::string_view value =
            buf.overflow ? std::string_view(buf.heap) : std::string_view(buf.data, buf.n);
        exp.parts.emplace_back(::zui::Atom(name), ::zui::Atom(value));
        placed = true;
    }
    if (!placed && !raw.empty()) {
        adopt_parts(exp, raw);
    }
    note_fin_flags(exp);
    if (!cache) {
        g_ephemeral_decl = std::move(exp);
        return g_ephemeral_decl;
    }
    return g_decl_exp.insert(decl, std::move(exp));
}

void prewarm_rules(lxb_css_rule_t* rule, std::string_view source) {
    for (; rule != nullptr; rule = rule->next) {
        if (rule->type == LXB_CSS_RULE_LIST || rule->type == LXB_CSS_RULE_STYLESHEET) {
            prewarm_rules(lxb_css_rule_list(rule)->first, source);
            continue;
        }
        if (rule->type == LXB_CSS_RULE_AT_RULE) {
            auto* at = lxb_css_rule_at(rule);
            if (at->type == LXB_CSS_AT_RULE_MEDIA && at->u.media != nullptr &&
                at->u.media->block != nullptr) {
                prewarm_rules(at->u.media->block->first, source);
            }
            if (at->type == LXB_CSS_AT_RULE__CUSTOM && at->u.custom != nullptr &&
                at->u.custom->block != nullptr) {
                prewarm_rules(at->u.custom->block->first, source);
            }
            continue;
        }
        if (rule->type != LXB_CSS_RULE_STYLE) {
            continue;
        }
        auto* style = lxb_css_rule_style(rule);
        if (style->declarations != nullptr) {
            for (lxb_css_rule_t* decl_rule = style->declarations->first; decl_rule != nullptr;
                 decl_rule = decl_rule->next) {
                if (decl_rule->type != LXB_CSS_RULE_DECLARATION) {
                    continue;
                }
                (void)expansion_for(lxb_css_rule_declaration(decl_rule), source, true);
            }
        }
        if (style->child != nullptr) {
            prewarm_rules(&style->child->rule, source);
        }
    }
}

void prewarm_decl_expansion(const std::vector<lxb_css_stylesheet*>& sheets,
                            const std::vector<std::string>& sources) {
    for (std::size_t i = 0; i < sheets.size(); ++i) {
        lxb_css_stylesheet* sheet = sheets[i];
        if (sheet == nullptr || sheet->root == nullptr) {
            continue;
        }
        const std::string_view source =
            i < sources.size() ? std::string_view(sources[i]) : std::string_view{};
        prewarm_rules(sheet->root, source);
    }
}

void apply_decls(const lxb_css_rule_declaration_list_t* decls, uint32_t spec, OriginRank origin,
                 int& order, DeclSink& sink, std::string_view source, int layer_rank,
                 bool element_attached) {
    if (decls == nullptr) {
        return;
    }
    if (sink.win.empty()) {
        sink.win.reserve(32);
    }
    auto& win = sink.win;
    auto& pending = sink.pending;
    for (lxb_css_rule_t* r = decls->first; r != nullptr; r = r->next) {
        if (r->type != LXB_CSS_RULE_DECLARATION) {
            continue;
        }
        auto* decl = lxb_css_rule_declaration(r);
        // Hit path stays in this function. `expansion_for` is a real call under /Ob0, and a
        // cascade applies the same declarations to every miss.
        const CachedDecl* exp_ptr = nullptr;
        if (!element_attached) {
            DeclExpCache::Slot* s = g_decl_exp.slot_data;
            if (s != nullptr) {
                std::uintptr_t h = reinterpret_cast<std::uintptr_t>(decl);
                h >>= 4;
                h *= static_cast<std::uintptr_t>(0x9E3779B1u);
                h ^= h >> 16;
                const std::uint32_t m = g_decl_exp.mask;
                std::uint32_t i = static_cast<std::uint32_t>(h) & m;
                for (;;) {
                    const lxb_css_rule_declaration_t* k = s[i].key;
                    if (k == decl) {
                        exp_ptr = g_decl_exp.value_data + s[i].index;
                        break;
                    }
                    if (k == nullptr) {
                        break;
                    }
                    i = (i + 1) & m;
                }
            }
        }
        if (exp_ptr == nullptr) {
            exp_ptr = &expansion_for(decl, source, !element_attached);
        }
        const CachedDecl& exp = *exp_ptr;
        sink.fin_flags = static_cast<std::uint8_t>(sink.fin_flags | exp.fin_flags);
        if (!exp.deferred && exp.parts.empty()) {
            continue;
        }
        const bool important = decl->important != 0;
        const uint32_t s = spec;
        const int ord = order++;
        CascadeLayer band = kLayerAuthorNormal;
        if (origin == kOriginEmpty) {
            band = kLayerEmpty;
        } else if (origin == kOriginUa) {
            band = important ? kLayerUaImportant : kLayerUaNormal;
        } else if (important) {
            band = kLayerAuthorImportant;
        }
        if (exp.deferred) {
            PendingShort& slot = pending.emplace_back();
            slot.name = exp.defer_name;
            slot.value = exp.defer_value;
            slot.spec = s;
            slot.order = ord;
            slot.origin = origin;
            slot.important = important;
            slot.layer = layer_rank;
            slot.element_attached = element_attached;
            continue;
        }
        if (exp.parts.empty()) {
            continue;
        }
        // Straight-line winner publish. `declaration_wins` / `cascade_layer` are real calls
        // under /Ob0, and this loop runs once per longhand of every matched declaration.
        const auto* parts = exp.parts.data();
        const std::size_t part_n = exp.parts.size();
        const bool band_important = band == kLayerAuthorImportant || band == kLayerUaImportant;
        for (std::size_t pi = 0; pi < part_n; ++pi) {
            const ::zui::Atom& key = parts[pi].first;
            const ::zui::Atom& value = parts[pi].second;
            Winner cand;
            cand.value = value;
            cand.spec = s;
            cand.order = ord;
            cand.origin = origin;
            cand.important = important;
            cand.layer = layer_rank;
            cand.element_attached = element_attached;
            // Losers exist so `revert` can cascade again. A document with no such keyword never
            // reads the stack, and building it allocated a second map entry per declaration.
            if (g_record_decl_history) {
                sink.decls[key].push_back(cand);
            }
            Winner& slot = win[key];
            CascadeLayer cur = kLayerAuthorNormal;
            if (slot.origin == kOriginEmpty) {
                cur = kLayerEmpty;
            } else if (slot.origin == kOriginUa) {
                cur = slot.important ? kLayerUaImportant : kLayerUaNormal;
            } else if (slot.important) {
                cur = kLayerAuthorImportant;
            }
            bool wins = false;
            if (band != cur) {
                wins = band > cur;
            } else if (element_attached != slot.element_attached) {
                wins = element_attached;
            } else if (layer_rank != slot.layer) {
                wins = band_important ? layer_rank < slot.layer : layer_rank > slot.layer;
            } else if (s != slot.spec) {
                wins = s > slot.spec;
            } else {
                wins = ord >= slot.order;
            }
            if (wins) {
                slot = std::move(cand);
            }
        }
    }
}

lxb_css_parser_t* inline_style_parser() {
    static thread_local lxb_css_parser_t* parser = nullptr;
    static thread_local bool failed = false;
    if (failed) {
        return nullptr;
    }
    if (parser != nullptr) {
        return parser;
    }
    parser = lxb_css_parser_create();
    lxb_css_memory_t* memory = lxb_css_memory_create();
    if (parser == nullptr || memory == nullptr ||
        lxb_css_parser_init(parser, nullptr) != LXB_STATUS_OK ||
        lxb_css_memory_init(memory, 128) != LXB_STATUS_OK) {
        if (parser != nullptr) {
            lxb_css_parser_destroy(parser, true);
        }
        if (memory != nullptr) {
            lxb_css_memory_destroy(memory, true);
        }
        parser = nullptr;
        failed = true;
        return nullptr;
    }
    // Declaration parsing allocates into the parser memory. A stylesheet parse sets this;
    // a bare declaration list does not.
    parser->memory = memory;
    return parser;
}

void apply_inline_style(std::string_view text, int& order, DeclSinks& sinks, int layer_rank) {
    if (text.empty()) {
        return;
    }
    lxb_css_parser_t* parser = inline_style_parser();
    if (parser == nullptr) {
        return;
    }
    lxb_css_rule_declaration_list_t* list = lxb_css_declaration_list_parse(
        parser, reinterpret_cast<const lxb_char_t*>(text.data()), text.size());
    if (list == nullptr) {
        return;
    }
    // `*` has specificity 0; the style attribute lifts that into the style bit (1 << 27), the
    // same bump `visit_rules` applies when it parses `*{...}`.
    apply_decls(list, lxb_css_selector_sp_up_s(0), kOriginAuthor, order,
                sinks.for_target(PseudoTarget::None), text, layer_rank, true);
    // The list lives in the reused pool. Drop parser stacks first so they do not point at it,
    // then reclaim the pool. The declarations have already been copied into the sinks.
    lxb_css_parser_clean(parser);
    lxb_css_memory_clean(parser->memory);
}

}  // namespace detail
}  // namespace css
}  // namespace zui
