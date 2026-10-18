#include "css/transition/animation.h"

#include "css/transition/internal.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace zui {
namespace css {
namespace {

using transition_detail::Easing;
using transition_detail::Prop;
using transition_detail::StyleMap;
using transition_detail::clear_flag;
using transition_detail::css_name;
using transition_detail::is_length_prop;
using transition_detail::lerp_value;
using transition_detail::parse_easing;
using transition_detail::sample_easing;
using transition_detail::specified_value;
using transition_detail::write_used;
using keyframes_scan::ident_char;
using keyframes_scan::skip_block;
using keyframes_scan::skip_comment;
using keyframes_scan::starts_keyframes;

float parse_time_s(std::string_view tok, bool* ok) {
    *ok = false;
    if (tok.size() > 2 && tok.compare(tok.size() - 2, 2, "ms") == 0) {
        try {
            *ok = true;
            return std::stof(std::string(tok.substr(0, tok.size() - 2))) / 1000.f;
        } catch (...) {
            *ok = false;
            return 0.f;
        }
    }
    if (!tok.empty() && tok.back() == 's') {
        try {
            *ok = true;
            return std::stof(std::string(tok.substr(0, tok.size() - 1)));
        } catch (...) {
            *ok = false;
        }
    }
    return 0.f;
}

std::vector<std::string> tokens_of(std::string_view s) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])) != 0) {
            ++i;
        }
        if (i >= s.size()) {
            break;
        }
        const std::size_t b = i;
        int depth = 0;
        while (i < s.size()) {
            if (s[i] == '(') {
                ++depth;
            } else if (s[i] == ')' && depth > 0) {
                --depth;
            } else if (depth == 0 && std::isspace(static_cast<unsigned char>(s[i])) != 0) {
                break;
            }
            ++i;
        }
        out.emplace_back(s.substr(b, i - b));
    }
    return out;
}

struct Decl {
    std::string name;
    float duration = 0.f;
    float delay = 0.f;
    Easing ease = parse_easing("ease");
    bool infinite = false;
    float iterations = 1.f;
    bool alternate = false;
    bool ok = false;
};

bool keyword_not_name(std::string_view s) {
    return s == "none" || s == "infinite" || s == "linear" || s == "ease" || s == "ease-in" ||
           s == "ease-out" || s == "ease-in-out" || s == "normal" || s == "reverse" ||
           s == "alternate" || s == "alternate-reverse" || s == "forwards" || s == "backwards" ||
           s == "both" || s == "running" || s == "paused" || s == "step-start" || s == "step-end" ||
           s.rfind("cubic-bezier", 0) == 0 || s.rfind("steps(", 0) == 0;
}

Decl parse_animation(std::string_view raw) {
    Decl d;
    std::string text(transition_detail::trim(raw));
    const auto bang = text.rfind("!important");
    if (bang != std::string::npos) {
        text.resize(bang);
    }
    int times = 0;
    for (const std::string& tok : tokens_of(text)) {
        const std::string low = transition_detail::lower(tok);
        if (low == "none" && d.name.empty()) {
            return d;
        }
        if (transition_detail::is_easing_token(low)) {
            d.ease = parse_easing(low);
            continue;
        }
        if (low == "infinite") {
            d.infinite = true;
            continue;
        }
        if (low == "alternate" || low == "alternate-reverse") {
            d.alternate = true;
            continue;
        }
        if (low == "normal" || low == "reverse" || low == "forwards" || low == "backwards" ||
            low == "both" || low == "running" || low == "paused") {
            continue;
        }
        bool time_ok = false;
        const float sec = parse_time_s(low, &time_ok);
        if (time_ok) {
            if (times == 0) {
                d.duration = sec;
            } else {
                d.delay = sec;
            }
            ++times;
            continue;
        }
        try {
            std::size_t used = 0;
            const float n = std::stof(low, &used);
            if (used == low.size()) {
                d.iterations = n;
                continue;
            }
        } catch (...) {
        }
        if (!keyword_not_name(low) && d.name.empty()) {
            d.name = low;
        }
    }
    d.ok = !d.name.empty() && d.name != "none" && d.duration > 0.0005f;
    return d;
}

const Prop kAnimProps[] = {Prop::Color,        Prop::Background,    Prop::BorderTop,    Prop::BorderRight,
                           Prop::BorderBottom, Prop::BorderLeft,    Prop::Opacity,      Prop::Transform,
                           Prop::Width,        Prop::Height,        Prop::MarginTop,    Prop::MarginRight,
                           Prop::MarginBottom, Prop::MarginLeft,    Prop::PaddingTop,   Prop::PaddingRight,
                           Prop::PaddingBottom, Prop::PaddingLeft};

std::uint32_t prop_bit(Prop prop) { return 1u << static_cast<unsigned>(prop); }

bool is_anim_longhand(std::string_view name) {
    for (Prop prop : kAnimProps) {
        if (name == css_name(prop)) {
            return true;
        }
    }
    return false;
}

/// Declarations in one keyframe block. Unknown properties are dropped so a stop that only sets
/// `opacity` does not stamp every other used channel.
StyleMap parse_decls(std::string_view block) {
    StyleMap map;
    std::size_t i = 0;
    while (i < block.size()) {
        while (i < block.size() &&
               (std::isspace(static_cast<unsigned char>(block[i])) != 0 || block[i] == ';')) {
            ++i;
        }
        if (i >= block.size()) {
            break;
        }
        const std::size_t name_b = i;
        while (i < block.size() && block[i] != ':' && block[i] != ';' && block[i] != '{' &&
               block[i] != '}') {
            ++i;
        }
        if (i >= block.size() || block[i] != ':') {
            while (i < block.size() && block[i] != ';') {
                ++i;
            }
            continue;
        }
        const std::string name =
            transition_detail::lower(transition_detail::trim(block.substr(name_b, i - name_b)));
        ++i;
        const std::size_t val_b = i;
        int depth = 0;
        while (i < block.size()) {
            if (block[i] == '(') {
                ++depth;
            } else if (block[i] == ')' && depth > 0) {
                --depth;
            } else if (depth == 0 && block[i] == ';') {
                break;
            }
            ++i;
        }
        const std::string value(transition_detail::trim(block.substr(val_b, i - val_b)));
        if (is_anim_longhand(name) && !value.empty()) {
            map[name] = value;
        }
    }
    return map;
}

void add_stops(Animations::Rule& rule, std::string_view prelude, std::string_view block) {
    StyleMap decls = parse_decls(block);
    if (decls.empty()) {
        return;
    }
    std::size_t i = 0;
    while (i < prelude.size()) {
        while (i < prelude.size() &&
               (std::isspace(static_cast<unsigned char>(prelude[i])) != 0 || prelude[i] == ',')) {
            ++i;
        }
        if (i >= prelude.size()) {
            break;
        }
        const std::size_t b = i;
        while (i < prelude.size() && prelude[i] != ',') {
            ++i;
        }
        const std::string low = transition_detail::lower(transition_detail::trim(prelude.substr(b, i - b)));
        float offset = -1.f;
        if (low == "from") {
            offset = 0.f;
        } else if (low == "to") {
            offset = 1.f;
        } else if (!low.empty() && low.back() == '%') {
            try {
                offset = std::stof(low.substr(0, low.size() - 1)) / 100.f;
            } catch (...) {
                offset = -1.f;
            }
        }
        if (offset < 0.f) {
            continue;
        }
        bool replaced = false;
        for (auto& stop : rule.stops) {
            if (std::fabs(stop.offset - offset) < 0.0001f) {
                stop.decls = decls;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            Animations::Stop stop;
            stop.offset = offset;
            stop.decls = decls;
            rule.stops.push_back(std::move(stop));
        }
    }
}

bool under(const html::Node* node, const html::Node* root) {
    for (const html::Node* n = node; n != nullptr; n = n->parent) {
        if (n == root) {
            return true;
        }
    }
    return false;
}

bool in_template(const html::Node* node) {
    for (const html::Node* n = node; n != nullptr; n = n->parent) {
        if (n->tag == "template") {
            return true;
        }
    }
    return false;
}

/// Same segment split the opacity sampler used: easing is per keyframe span, and a progress that
/// lands on a stop writes that stop. `mask` records which used flags this call set.
void apply_stops(html::Node* node, const std::vector<Animations::Stop>& stops, float p,
                 const Easing& ease, std::uint32_t* mask, bool* length) {
    if (node == nullptr || stops.empty() || mask == nullptr) {
        return;
    }
    std::size_t ia = 0;
    std::size_t ib = 0;
    float e = 0.f;
    if (p <= stops.front().offset || p >= stops.back().offset) {
        ia = ib = p >= stops.back().offset ? stops.size() - 1 : 0;
    } else {
        for (std::size_t i = 1; i < stops.size(); ++i) {
            if (p <= stops[i].offset) {
                ia = i - 1;
                ib = i;
                const float span = stops[i].offset - stops[i - 1].offset;
                const float u = span > 0.f ? (p - stops[i - 1].offset) / span : 0.f;
                e = sample_easing(std::clamp(u, 0.f, 1.f), ease);
                break;
            }
        }
    }
    const StyleMap& from_map = stops[ia].decls;
    const StyleMap& to_map = stops[ib].decls;
    for (Prop prop : kAnimProps) {
        const char* name = css_name(prop);
        const bool has_from = from_map.find(name) != from_map.end();
        const bool has_to = to_map.find(name) != to_map.end();
        if (!has_from && !has_to) {
            continue;
        }
        const auto from = specified_value(node, has_from ? &from_map : nullptr, prop);
        const auto to = specified_value(node, has_to ? &to_map : nullptr, prop);
        if (is_length_prop(prop) && (!from.length_ok || !to.length_ok)) {
            continue;
        }
        write_used(node, prop, lerp_value(prop, from, to, e));
        *mask |= prop_bit(prop);
        if (length != nullptr && is_length_prop(prop)) {
            *length = true;
        }
    }
}

const std::string* computed_of(const html::Node* node, const char* key) {
    if (node == nullptr || node->computed == nullptr || key == nullptr) {
        return nullptr;
    }
    const auto it = node->computed->find(key);
    if (it == node->computed->end() || it->second.empty()) {
        return nullptr;
    }
    return it->second.ptr();
}

/// CSS Animations: `display: none` terminates a running animation on that element and on its
/// descendants. The next sync that sees a rendered element starts the animation again.
bool not_rendered(const html::Node* node) {
    for (const html::Node* n = node; n != nullptr; n = n->parent) {
        const std::string* display = computed_of(n, "display");
        if (display != nullptr && *display == "none") {
            return true;
        }
    }
    return false;
}

}  // namespace

void Animations::note_layout(html::Node* node) {
    layout_ = true;
    if (node == nullptr) {
        return;
    }
    for (html::Node* have : layout_nodes_) {
        if (have == node) {
            return;
        }
    }
    layout_nodes_.push_back(node);
}

void Animations::clear() {
    for (Run& run : runs_) {
        clear_overlay(run);
    }
    runs_.clear();
    rules_.clear();
    source_stamp_ = 0;
    layout_ = false;
    layout_nodes_.clear();
}

void Animations::clear_overlay(Run& run) {
    if (!run.applied || run.node == nullptr) {
        run.applied = false;
        run.applied_mask = 0;
        return;
    }
    const std::uint32_t mask = run.applied_mask;
    html::Node* node = run.node;
    run.applied = false;
    run.applied_mask = 0;
    bool length = false;
    for (Prop prop : kAnimProps) {
        if ((mask & prop_bit(prop)) == 0) {
            continue;
        }
        if (is_length_prop(prop)) {
            length = true;
        }
        clear_flag(node, prop);
    }
    if (length) {
        note_layout(node);
    }
}

void Animations::cancel_subtree(html::Node* dead) {
    if (dead == nullptr) {
        return;
    }
    for (Run& run : runs_) {
        if (under(run.node, dead)) {
            clear_overlay(run);
            run.node = nullptr;
        }
    }
    runs_.erase(std::remove_if(runs_.begin(), runs_.end(),
                               [](const Run& run) { return run.node == nullptr; }),
                runs_.end());
}

void Animations::parse_sources(const std::vector<std::string>& sources) {
    std::size_t stamp = sources.size();
    for (const std::string& src : sources) {
        stamp = stamp * 131u + src.size();
    }
    if (stamp == source_stamp_ && !rules_.empty()) {
        return;
    }
    source_stamp_ = stamp;
    rules_.clear();
    for (const std::string& src : sources) {
        std::string_view css = src;
        for (std::size_t i = 0; i < css.size();) {
            skip_comment(css, i);
            if (i >= css.size()) {
                break;
            }
            if (!starts_keyframes(css, i)) {
                ++i;
                continue;
            }
            i += std::string_view("@keyframes").size();
            while (i < css.size() && std::isspace(static_cast<unsigned char>(css[i])) != 0) {
                ++i;
            }
            const std::size_t name_b = i;
            while (i < css.size() && ident_char(css[i])) {
                ++i;
            }
            Rule rule;
            rule.name = transition_detail::lower(css.substr(name_b, i - name_b));
            while (i < css.size() && css[i] != '{') {
                ++i;
            }
            if (i >= css.size()) {
                break;
            }
            const std::size_t block_b = i + 1;
            const std::size_t block_e = skip_block(css, i);
            std::string_view body = css.substr(block_b, block_e > block_b ? block_e - block_b - 1 : 0);
            int depth = 0;
            std::size_t prelude = 0;
            for (std::size_t k = 0; k < body.size(); ++k) {
                if (body[k] == '{') {
                    if (depth == 0) {
                        const std::size_t decl = k + 1;
                        int inner = 1;
                        std::size_t e = decl;
                        while (e < body.size() && inner > 0) {
                            if (body[e] == '{') {
                                ++inner;
                            } else if (body[e] == '}') {
                                --inner;
                            }
                            ++e;
                        }
                        add_stops(rule, body.substr(prelude, k - prelude),
                                  body.substr(decl, e > decl ? e - decl - 1 : 0));
                        prelude = e;
                        k = e - 1;
                    } else {
                        ++depth;
                    }
                }
            }
            std::sort(rule.stops.begin(), rule.stops.end(),
                      [](const Stop& a, const Stop& b) { return a.offset < b.offset; });
            if (!rule.name.empty() && !rule.stops.empty()) {
                rules_.push_back(std::move(rule));
            }
            i = block_e;
        }
    }
}

const Animations::Rule* Animations::find_rule(std::string_view name) const {
    for (const Rule& rule : rules_) {
        if (rule.name == name) {
            return &rule;
        }
    }
    return nullptr;
}

void Animations::sync(html::Document& doc, const std::vector<std::string>& sources, double t) {
    parse_sources(sources);
    for (Run& run : runs_) {
        run.seen = false;
    }
    std::vector<html::Node*> stack;
    if (doc.root != nullptr) {
        stack.push_back(doc.root.get());
    }
    while (!stack.empty()) {
        html::Node* node = stack.back();
        stack.pop_back();
        if (node == nullptr) {
            continue;
        }
        for (auto it = node->children.rbegin(); it != node->children.rend(); ++it) {
            if (*it != nullptr) {
                stack.push_back(it->get());
            }
        }
        if (in_template(node) || not_rendered(node)) {
            continue;
        }
        const std::string* anim = computed_of(node, "animation");
        if (anim == nullptr) {
            continue;
        }
        Decl decl = parse_animation(*anim);
        if (const std::string* name = computed_of(node, "animation-name")) {
            const std::string low = transition_detail::lower(transition_detail::trim(*name));
            if (low == "none") {
                decl.ok = false;
            } else if (!low.empty()) {
                decl.name = low;
                decl.ok = decl.duration > 0.0005f;
            }
        }
        if (const std::string* dur = computed_of(node, "animation-duration")) {
            bool ok = false;
            const float sec = parse_time_s(transition_detail::lower(transition_detail::trim(*dur)), &ok);
            if (ok) {
                decl.duration = sec;
                decl.ok = !decl.name.empty() && decl.duration > 0.0005f;
            }
        }
        if (const std::string* delay = computed_of(node, "animation-delay")) {
            bool ok = false;
            const float sec =
                parse_time_s(transition_detail::lower(transition_detail::trim(*delay)), &ok);
            if (ok) {
                decl.delay = sec;
            }
        }
        if (!decl.ok) {
            continue;
        }
        const Rule* rule = find_rule(decl.name);
        if (rule == nullptr) {
            continue;
        }
        Run* existing = nullptr;
        for (Run& run : runs_) {
            if (run.node == node) {
                existing = &run;
                break;
            }
        }
        if (existing != nullptr && existing->name == decl.name &&
            std::fabs(existing->duration - decl.duration) < 0.0001f &&
            std::fabs(existing->delay - decl.delay) < 0.0001f) {
            existing->seen = true;
            existing->stops = rule->stops;
            existing->x1 = decl.ease.x1;
            existing->y1 = decl.ease.y1;
            existing->x2 = decl.ease.x2;
            existing->y2 = decl.ease.y2;
            existing->infinite = decl.infinite;
            existing->iterations = decl.iterations;
            existing->alternate = decl.alternate;
            continue;
        }
        if (existing == nullptr) {
            runs_.push_back(Run{});
            existing = &runs_.back();
            existing->node = node;
            existing->start = t;
        } else {
            clear_overlay(*existing);
            existing->start = t;
        }
        existing->seen = true;
        existing->name = decl.name;
        existing->duration = decl.duration;
        existing->delay = decl.delay;
        existing->x1 = decl.ease.x1;
        existing->y1 = decl.ease.y1;
        existing->x2 = decl.ease.x2;
        existing->y2 = decl.ease.y2;
        existing->infinite = decl.infinite;
        existing->iterations = decl.iterations;
        existing->alternate = decl.alternate;
        existing->stops = rule->stops;
        (void)existing;
    }
    for (Run& run : runs_) {
        if (!run.seen) {
            clear_overlay(run);
            run.node = nullptr;
        }
    }
    runs_.erase(std::remove_if(runs_.begin(), runs_.end(),
                               [](const Run& run) { return run.node == nullptr; }),
                runs_.end());
}

void Animations::sample(double t) {
    // sync/cancel may already have cleared a length overlay. Keep those nodes across this sample
    // so the host, which reads `needs_layout` after `sample`, still lays them out.
    std::vector<html::Node*> held_nodes = std::move(layout_nodes_);
    const bool held_layout = layout_;
    layout_ = false;
    layout_nodes_.clear();
    repaint_ = false;
    for (Run& run : runs_) {
        if (run.node == nullptr || run.duration <= 0.f || run.stops.empty()) {
            continue;
        }
        const double elapsed = t - run.start;
        if (elapsed < static_cast<double>(run.delay)) {
            clear_overlay(run);
            continue;
        }
        const double active = elapsed - static_cast<double>(run.delay);
        const double cycles = active / static_cast<double>(run.duration);
        if (!run.infinite && cycles >= static_cast<double>(run.iterations)) {
            clear_overlay(run);
            continue;
        }
        double frac = cycles - std::floor(cycles);
        const int idx = static_cast<int>(std::floor(cycles));
        if (run.alternate && (idx % 2) != 0) {
            frac = 1.0 - frac;
        }
        Easing ease;
        ease.kind = transition_detail::EasingKind::Bezier;
        ease.x1 = run.x1;
        ease.y1 = run.y1;
        ease.x2 = run.x2;
        ease.y2 = run.y2;
        std::uint32_t mask = 0;
        bool length = false;
        apply_stops(run.node, run.stops, static_cast<float>(frac), ease, &mask, &length);
        const std::uint32_t dropped = run.applied_mask & ~mask;
        for (Prop prop : kAnimProps) {
            if ((dropped & prop_bit(prop)) == 0) {
                continue;
            }
            if (is_length_prop(prop)) {
                length = true;
            }
            clear_flag(run.node, prop);
        }
        run.applied_mask = mask;
        run.applied = mask != 0;
        if (length) {
            note_layout(run.node);
        }
        if (mask != 0) {
            repaint_ = true;
        }
    }
    if (held_layout) {
        for (html::Node* node : held_nodes) {
            note_layout(node);
        }
    }
}

}  // namespace css
}  // namespace zui
