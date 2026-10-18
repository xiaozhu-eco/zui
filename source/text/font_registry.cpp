#include "text/font_chain.h"
#include "text/font_chain_internal.h"

#include <cctype>
#include <chrono>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace zui {
namespace text {

/// Embedded faces, by lowercased family name. Tests register them before the first chain is
/// built; the browser side loads the same file through @font-face (TESTPLAN section 6).
std::unordered_map<std::string, std::string>& embedded_fonts() {
    static std::unordered_map<std::string, std::string> table;
    return table;
}

/// The registry's clock. Leaked on purpose: the layout side may read it from a static destructor.
std::uint64_t& font_registry_generation_ref() {
    static auto* generation = new std::uint64_t{0};
    return *generation;
}

std::uint64_t font_registry_generation() { return font_registry_generation_ref(); }

void register_embedded_font(const std::string& family, const std::string& path) {
    if (family.empty() || path.empty()) {
        return;
    }
    embedded_fonts()[lower_ascii(family)] = path;
    // A cached `ch` / `ex` for that family is not valid any more.
    ++font_registry_generation_ref();
}

std::string embedded_font_path(std::string_view family) {
    const auto it = embedded_fonts().find(lower_ascii(family));
    if (it == embedded_fonts().end()) {
        return {};
    }
    return it->second;
}

namespace {

std::string strip_css_comments(std::string_view css) {
    std::string out;
    out.reserve(css.size());
    bool in_sq = false;
    bool in_dq = false;
    for (size_t i = 0; i < css.size(); ++i) {
        const char c = css[i];
        if (!in_sq && !in_dq && c == '/' && i + 1 < css.size() && css[i + 1] == '*') {
            i += 2;
            while (i + 1 < css.size() && !(css[i] == '*' && css[i + 1] == '/')) {
                ++i;
            }
            if (i + 1 < css.size()) {
                ++i;
            }
            continue;
        }
        if (c == '\'' && !in_dq) {
            in_sq = !in_sq;
        } else if (c == '"' && !in_sq) {
            in_dq = !in_dq;
        }
        out.push_back(c);
    }
    return out;
}

bool starts_lit(std::string_view text, size_t i, std::string_view lit) {
    if (i + lit.size() > text.size()) {
        return false;
    }
    for (size_t k = 0; k < lit.size(); ++k) {
        const auto got = static_cast<unsigned char>(text[i + k]);
        const auto want = static_cast<unsigned char>(lit[k]);
        if (std::tolower(got) != want) {
            return false;
        }
    }
    return true;
}

std::string_view trim_view(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.remove_suffix(1);
    }
    return value;
}

/// First family name in a `font-family` descriptor. The descriptor is one name, not a list.
std::string font_family_name(std::string_view value) {
    value = trim_view(value);
    if (value.empty()) {
        return {};
    }
    if (value.front() == '"' || value.front() == '\'') {
        const char quote = value.front();
        const size_t end = value.find(quote, 1);
        if (end == std::string_view::npos) {
            return {};
        }
        return std::string(value.substr(1, end - 1));
    }
    size_t end = 0;
    while (end < value.size() && !std::isspace(static_cast<unsigned char>(value[end])) &&
           value[end] != ',' && value[end] != ';') {
        ++end;
    }
    return std::string(value.substr(0, end));
}

std::string unquote_url(std::string_view body) {
    body = trim_view(body);
    if (body.size() >= 2 && (body.front() == '"' || body.front() == '\'') &&
        body.back() == body.front()) {
        body = body.substr(1, body.size() - 2);
    }
    return std::string(trim_view(body));
}

bool format_is_woff2(std::string_view after_url) {
    after_url = trim_view(after_url);
    if (!starts_lit(after_url, 0, "format(")) {
        return false;
    }
    const size_t end = after_url.find(')');
    if (end == std::string_view::npos) {
        return false;
    }
    const std::string hint = lower_ascii(after_url.substr(0, end));
    return hint.find("woff2") != std::string::npos;
}

/// First local file in `src` that exists. Later `url()` candidates cover a woff2-then-ttf list.
std::string first_local_face(std::string_view src, const std::filesystem::path& base_dir) {
    namespace fs = std::filesystem;
    for (size_t i = 0; i < src.size(); ++i) {
        if (!starts_lit(src, i, "url(")) {
            continue;
        }
        const size_t open = i + 4;
        int depth = 1;
        size_t j = open;
        for (; j < src.size() && depth > 0; ++j) {
            if (src[j] == '(') {
                ++depth;
            } else if (src[j] == ')') {
                --depth;
            }
        }
        if (depth != 0) {
            continue;
        }
        const std::string raw = unquote_url(src.substr(open, j - open - 1));
        const std::string lowered = lower_ascii(raw);
        const bool remote = lowered.rfind("http:", 0) == 0 || lowered.rfind("https:", 0) == 0 ||
                            lowered.rfind("data:", 0) == 0;
        const size_t after = j;
        const bool woff2 = after < src.size() && format_is_woff2(src.substr(after));
        i = j;
        if (raw.empty() || remote || woff2) {
            continue;
        }
        fs::path file(raw);
        if (file.is_relative()) {
            if (base_dir.empty()) {
                continue;
            }
            file = base_dir / file;
        }
        std::error_code ec;
        if (fs::is_regular_file(file, ec)) {
            return file.string();
        }
    }
    return {};
}

// Chromium's `font-display: swap` block period is 0, not the 100ms the spec text recommends.
// `auto` follows that browser's default, which is the `block` timeline.
enum class FontDisplayMode { Auto, Block, Swap, Fallback, Optional };

struct FontFaceTiming {
    FontDisplayMode mode = FontDisplayMode::Auto;
    bool loaded = false;
    int loaded_at_ms = -1;
    int requested_at_ms = -1;
};

int g_font_display_clock_ms = -1;

int font_display_now_ms() {
    if (g_font_display_clock_ms >= 0) {
        return g_font_display_clock_ms;
    }
    using clock = std::chrono::steady_clock;
    static const clock::time_point start = clock::now();
    return static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - start).count());
}

std::unordered_map<std::string, FontFaceTiming>& font_face_timings() {
    static std::unordered_map<std::string, FontFaceTiming> table;
    return table;
}

FontDisplayMode parse_font_display_mode(std::string_view value) {
    const std::string l = lower_ascii(trim_view(value));
    if (l == "block") {
        return FontDisplayMode::Block;
    }
    if (l == "swap") {
        return FontDisplayMode::Swap;
    }
    if (l == "fallback") {
        return FontDisplayMode::Fallback;
    }
    if (l == "optional") {
        return FontDisplayMode::Optional;
    }
    return FontDisplayMode::Auto;
}

void periods_for(FontDisplayMode mode, int& block_ms, int& swap_ms) {
    switch (mode) {
        case FontDisplayMode::Swap:
            block_ms = 0;
            swap_ms = -1;
            return;
        case FontDisplayMode::Fallback:
            block_ms = 100;
            swap_ms = 3000;
            return;
        case FontDisplayMode::Optional:
            block_ms = 100;
            swap_ms = 0;
            return;
        case FontDisplayMode::Block:
        case FontDisplayMode::Auto:
            block_ms = 3000;
            swap_ms = -1;
            return;
    }
    block_ms = 3000;
    swap_ms = -1;
}

bool arrived_in_open_period(int load_elapsed, int block_ms, int swap_ms) {
    if (load_elapsed < block_ms) {
        return true;
    }
    if (swap_ms < 0) {
        return true;
    }
    return load_elapsed < block_ms + swap_ms;
}

FontDisplayPhase classify_font_display(FontFaceTiming& rec, int now) {
    if (rec.requested_at_ms < 0) {
        rec.requested_at_ms = now;
    }
    const int requested = rec.requested_at_ms;
    if (rec.loaded && rec.loaded_at_ms >= 0 && rec.loaded_at_ms <= requested) {
        return FontDisplayPhase::Use;
    }
    int block_ms = 3000;
    int swap_ms = -1;
    periods_for(rec.mode, block_ms, swap_ms);
    if (rec.loaded && rec.loaded_at_ms >= 0) {
        if (arrived_in_open_period(rec.loaded_at_ms - requested, block_ms, swap_ms)) {
            return FontDisplayPhase::Use;
        }
        return FontDisplayPhase::Fallback;
    }
    const int elapsed = now - requested;
    if (block_ms > 0 && elapsed < block_ms) {
        return FontDisplayPhase::Invisible;
    }
    return FontDisplayPhase::Fallback;
}

void note_font_display(const std::string& family, FontDisplayMode mode, bool loaded) {
    FontFaceTiming rec;
    rec.mode = mode;
    rec.loaded = loaded;
    rec.loaded_at_ms = loaded ? font_display_now_ms() : -1;
    font_face_timings()[lower_ascii(family)] = rec;
}

void register_one_face(std::string_view block, const std::filesystem::path& base_dir) {
    std::string family;
    std::string src;
    FontDisplayMode display = FontDisplayMode::Auto;
    size_t i = 0;
    while (i < block.size()) {
        size_t semi = i;
        int depth = 0;
        bool in_sq = false;
        bool in_dq = false;
        for (; semi < block.size(); ++semi) {
            const char c = block[semi];
            if (c == '\'' && !in_dq) {
                in_sq = !in_sq;
            } else if (c == '"' && !in_sq) {
                in_dq = !in_dq;
            } else if (!in_sq && !in_dq && c == '(') {
                ++depth;
            } else if (!in_sq && !in_dq && c == ')' && depth > 0) {
                --depth;
            } else if (!in_sq && !in_dq && depth == 0 && c == ';') {
                break;
            }
        }
        const std::string_view decl = block.substr(i, semi - i);
        const size_t colon = decl.find(':');
        if (colon != std::string_view::npos) {
            const std::string name = lower_ascii(trim_view(decl.substr(0, colon)));
            const std::string_view value = trim_view(decl.substr(colon + 1));
            if (name == "font-family") {
                family = font_family_name(value);
            } else if (name == "src") {
                src = std::string(value);
            } else if (name == "font-display") {
                display = parse_font_display_mode(value);
            }
        }
        i = semi < block.size() ? semi + 1 : block.size();
    }
    if (family.empty() || src.empty()) {
        return;
    }
    const std::string path = first_local_face(src, base_dir);
    if (!path.empty()) {
        register_embedded_font(family, path);
        note_font_display(family, display, true);
    }
}

}  // namespace

FontDisplayPhase font_display_phase_of(std::string_view family) {
    const auto found = font_face_timings().find(lower_ascii(family));
    if (found == font_face_timings().end()) {
        return FontDisplayPhase::Use;
    }
    return classify_font_display(found->second, font_display_now_ms());
}

void register_font_faces(std::string_view css, const std::filesystem::path& base_dir) {
    const std::string text = strip_css_comments(css);
    for (size_t i = 0; i < text.size(); ++i) {
        if (!starts_lit(text, i, "@font-face")) {
            continue;
        }
        const size_t before_ok = i == 0 || !std::isalnum(static_cast<unsigned char>(text[i - 1]));
        if (!before_ok) {
            continue;
        }
        size_t brace = text.find('{', i + 10);
        if (brace == std::string::npos) {
            return;
        }
        int depth = 1;
        bool in_sq = false;
        bool in_dq = false;
        size_t j = brace + 1;
        for (; j < text.size() && depth > 0; ++j) {
            const char c = text[j];
            if (c == '\'' && !in_dq) {
                in_sq = !in_sq;
            } else if (c == '"' && !in_sq) {
                in_dq = !in_dq;
            } else if (!in_sq && !in_dq && c == '{') {
                ++depth;
            } else if (!in_sq && !in_dq && c == '}') {
                --depth;
            }
        }
        if (depth != 0) {
            return;
        }
        register_one_face(std::string_view(text).substr(brace + 1, j - brace - 2), base_dir);
        // `j` is the first character after the closing brace. The loop increments `i`, so leave
        // it on that brace; otherwise `@font-face{}@font-face{}` skips the next at-rule.
        i = j - 1;
    }
}

FontDisplayPhase font_display_phase(std::string_view family) {
    return font_display_phase_of(family);
}

void font_display_test_clock(int now_ms) { g_font_display_clock_ms = now_ms; }

void font_display_test_arm(std::string_view family, int requested_ms, int loaded_at_ms, bool loaded) {
    FontFaceTiming& rec = font_face_timings()[lower_ascii(family)];
    rec.requested_at_ms = requested_ms;
    rec.loaded = loaded;
    rec.loaded_at_ms = loaded ? loaded_at_ms : -1;
}

}  // namespace text
}  // namespace zui
