#include "layout/inline/bidi.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace zui {
namespace layout {
namespace inline_text {
namespace {

constexpr std::uint8_t kMaxDepth = 125;

std::uint8_t next_even(std::uint8_t level) {
    return static_cast<std::uint8_t>((level + 2) & ~1u);
}

std::uint8_t next_odd(std::uint8_t level) {
    return static_cast<std::uint8_t>((level + 1) | 1u);
}

enum class DirOverride : std::uint8_t { Neutral, Ltr, Rtl };
enum class EmbedKind : std::uint8_t { Embedding, Override, Isolate };

struct StackEntry {
    std::uint8_t level = 0;
    DirOverride override = DirOverride::Neutral;
    EmbedKind kind = EmbedKind::Embedding;
};

BidiClass first_strong_in_isolate(const std::vector<BidiClass>& types, std::size_t begin) {
    int depth = 1;
    for (std::size_t i = begin; i < types.size(); ++i) {
        const BidiClass c = types[i];
        if (c == BidiClass::LRI || c == BidiClass::RLI || c == BidiClass::FSI) {
            ++depth;
        } else if (c == BidiClass::PDI) {
            --depth;
            if (depth == 0) {
                break;
            }
        } else if (depth == 1) {
            if (c == BidiClass::L) {
                return BidiClass::L;
            }
            if (c == BidiClass::R || c == BidiClass::AL) {
                return BidiClass::R;
            }
        }
    }
    return BidiClass::ON;
}

void apply_weak_neutral_implicit(std::vector<BidiClass>& t, std::vector<std::uint8_t>& levels,
                                 const std::vector<char>& skip, std::uint8_t paragraph) {
    const std::size_t n = t.size();
    auto is_neutral = [](BidiClass c) {
        return c == BidiClass::B || c == BidiClass::S || c == BidiClass::WS || c == BidiClass::ON ||
               c == BidiClass::BN;
    };
    auto strong_side = [](BidiClass c) -> BidiClass {
        if (c == BidiClass::L) {
            return BidiClass::L;
        }
        if (c == BidiClass::R || c == BidiClass::EN || c == BidiClass::AN) {
            return BidiClass::R;
        }
        return BidiClass::ON;
    };

    std::size_t i = 0;
    while (i < n) {
        if (skip[i]) {
            ++i;
            continue;
        }
        const std::uint8_t run_level = levels[i];
        std::size_t j = i + 1;
        while (j < n && !skip[j] && levels[j] == run_level) {
            ++j;
        }

        std::size_t prev = n;
        for (std::size_t k = i; k > 0;) {
            --k;
            if (!skip[k]) {
                prev = k;
                break;
            }
        }
        std::size_t next = n;
        for (std::size_t k = j; k < n; ++k) {
            if (!skip[k]) {
                next = k;
                break;
            }
        }

        const std::uint8_t sor_level = prev == n ? paragraph : std::max(run_level, levels[prev]);
        const std::uint8_t eor_level = next == n ? paragraph : std::max(run_level, levels[next]);
        const BidiClass sor = (sor_level & 1u) != 0 ? BidiClass::R : BidiClass::L;
        const BidiClass eor = (eor_level & 1u) != 0 ? BidiClass::R : BidiClass::L;

        BidiClass last = sor;
        for (std::size_t k = i; k < j; ++k) {
            if (skip[k]) {
                continue;
            }
            if (t[k] == BidiClass::NSM) {
                t[k] = last;
            } else {
                last = t[k];
            }
        }

        BidiClass last_strong = sor;
        for (std::size_t k = i; k < j; ++k) {
            if (skip[k]) {
                continue;
            }
            if (t[k] == BidiClass::AL || t[k] == BidiClass::R || t[k] == BidiClass::L) {
                last_strong = t[k];
            } else if (t[k] == BidiClass::EN && last_strong == BidiClass::AL) {
                t[k] = BidiClass::AN;
            }
        }

        for (std::size_t k = i; k < j; ++k) {
            if (!skip[k] && t[k] == BidiClass::AL) {
                t[k] = BidiClass::R;
            }
        }

        std::vector<std::size_t> idx;
        for (std::size_t k = i; k < j; ++k) {
            if (!skip[k]) {
                idx.push_back(k);
            }
        }

        for (std::size_t p = 1; p + 1 < idx.size(); ++p) {
            const std::size_t k = idx[p];
            const BidiClass left = t[idx[p - 1]];
            const BidiClass right = t[idx[p + 1]];
            if (t[k] == BidiClass::ES && left == BidiClass::EN && right == BidiClass::EN) {
                t[k] = BidiClass::EN;
            } else if (t[k] == BidiClass::CS && left == BidiClass::EN && right == BidiClass::EN) {
                t[k] = BidiClass::EN;
            } else if (t[k] == BidiClass::CS && left == BidiClass::AN && right == BidiClass::AN) {
                t[k] = BidiClass::AN;
            }
        }

        for (std::size_t p = 0; p < idx.size();) {
            if (t[idx[p]] != BidiClass::ET) {
                ++p;
                continue;
            }
            std::size_t q = p;
            while (q < idx.size() && t[idx[q]] == BidiClass::ET) {
                ++q;
            }
            const bool left = p > 0 && t[idx[p - 1]] == BidiClass::EN;
            const bool right = q < idx.size() && t[idx[q]] == BidiClass::EN;
            if (left || right) {
                for (std::size_t r = p; r < q; ++r) {
                    t[idx[r]] = BidiClass::EN;
                }
            }
            p = q;
        }

        for (std::size_t k : idx) {
            if (t[k] == BidiClass::ES || t[k] == BidiClass::ET || t[k] == BidiClass::CS) {
                t[k] = BidiClass::ON;
            }
        }

        last_strong = sor;
        for (std::size_t k : idx) {
            if (t[k] == BidiClass::L || t[k] == BidiClass::R) {
                last_strong = t[k];
            } else if (t[k] == BidiClass::EN && last_strong == BidiClass::L) {
                t[k] = BidiClass::L;
            }
        }

        for (std::size_t p = 0; p < idx.size();) {
            if (!is_neutral(t[idx[p]])) {
                ++p;
                continue;
            }
            std::size_t q = p;
            while (q < idx.size() && is_neutral(t[idx[q]])) {
                ++q;
            }
            const BidiClass lead = p == 0 ? sor : strong_side(t[idx[p - 1]]);
            const BidiClass trail = q == idx.size() ? eor : strong_side(t[idx[q]]);
            BidiClass resolved = BidiClass::ON;
            if (lead == trail && (lead == BidiClass::L || lead == BidiClass::R)) {
                resolved = lead;
            } else {
                resolved = (run_level & 1u) != 0 ? BidiClass::R : BidiClass::L;
            }
            for (std::size_t r = p; r < q; ++r) {
                t[idx[r]] = resolved;
            }
            p = q;
        }

        for (std::size_t k : idx) {
            std::uint8_t level = levels[k];
            if ((level & 1u) == 0) {
                if (t[k] == BidiClass::R) {
                    level = static_cast<std::uint8_t>(level + 1);
                } else if (t[k] == BidiClass::AN || t[k] == BidiClass::EN) {
                    level = static_cast<std::uint8_t>(level + 2);
                }
            } else if (t[k] == BidiClass::L || t[k] == BidiClass::AN || t[k] == BidiClass::EN) {
                level = static_cast<std::uint8_t>(level + 1);
            }
            levels[k] = level;
        }

        i = j;
    }
}

}  // namespace

BidiClass bidi_class_of(std::uint32_t cp) {
    if (cp == 0x202A) {
        return BidiClass::LRE;
    }
    if (cp == 0x202B) {
        return BidiClass::RLE;
    }
    if (cp == 0x202D) {
        return BidiClass::LRO;
    }
    if (cp == 0x202E) {
        return BidiClass::RLO;
    }
    if (cp == 0x202C) {
        return BidiClass::PDF;
    }
    if (cp == 0x2066) {
        return BidiClass::LRI;
    }
    if (cp == 0x2067) {
        return BidiClass::RLI;
    }
    if (cp == 0x2068) {
        return BidiClass::FSI;
    }
    if (cp == 0x2069) {
        return BidiClass::PDI;
    }
    if (cp == '\n' || cp == '\r' || cp == 0x2029 || cp == 0x2028) {
        return BidiClass::B;
    }
    if (cp == '\t' || cp == 0x000B || cp == 0x001F) {
        return BidiClass::S;
    }
    if (cp == ' ' || cp == 0x00A0 || cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200A) ||
        cp == 0x202F || cp == 0x205F || cp == 0x3000) {
        return BidiClass::WS;
    }
    if ((cp >= 0x200B && cp <= 0x200F) || (cp >= 0x2060 && cp <= 0x2064) ||
        (cp >= 0x206A && cp <= 0x206F) || cp == 0xFEFF) {
        return BidiClass::BN;
    }
    if ((cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x0591 && cp <= 0x05BD) || cp == 0x05BF ||
        (cp >= 0x05C1 && cp <= 0x05C2) || (cp >= 0x05C4 && cp <= 0x05C5) || cp == 0x05C7 ||
        (cp >= 0x0610 && cp <= 0x061A) || (cp >= 0x064B && cp <= 0x065F) || cp == 0x0670 ||
        (cp >= 0x06D6 && cp <= 0x06DC) || (cp >= 0x06DF && cp <= 0x06E4) ||
        (cp >= 0x06E7 && cp <= 0x06E8) || (cp >= 0x06EA && cp <= 0x06ED) ||
        (cp >= 0x1AB0 && cp <= 0x1AFF) || (cp >= 0x1DC0 && cp <= 0x1DFF) ||
        (cp >= 0x20D0 && cp <= 0x20FF) || (cp >= 0xFE20 && cp <= 0xFE2F)) {
        return BidiClass::NSM;
    }
    if ((cp >= 0x05D0 && cp <= 0x05EA) || (cp >= 0x05F0 && cp <= 0x05F4) ||
        (cp >= 0xFB1D && cp <= 0xFB28) || (cp >= 0xFB2A && cp <= 0xFB4F)) {
        return BidiClass::R;
    }
    if ((cp >= 0x0608 && cp <= 0x0608) || (cp >= 0x0620 && cp <= 0x064A) ||
        (cp >= 0x066E && cp <= 0x066F) || (cp >= 0x0671 && cp <= 0x06D3) || cp == 0x06D5 ||
        (cp >= 0x06EE && cp <= 0x06EF) || (cp >= 0x06FA && cp <= 0x06FC) || cp == 0x06FF ||
        (cp >= 0x0750 && cp <= 0x077F) || (cp >= 0x08A0 && cp <= 0x08BD) ||
        (cp >= 0xFB50 && cp <= 0xFD3D) || (cp >= 0xFD50 && cp <= 0xFD8F) ||
        (cp >= 0xFD92 && cp <= 0xFDC7) || (cp >= 0xFDF0 && cp <= 0xFDFB) ||
        (cp >= 0xFE70 && cp <= 0xFEFC)) {
        return BidiClass::AL;
    }
    if ((cp >= 0x0660 && cp <= 0x0669) || (cp >= 0x06F0 && cp <= 0x06F9)) {
        return BidiClass::AN;
    }
    if (cp >= '0' && cp <= '9') {
        return BidiClass::EN;
    }
    if (cp == '+' || cp == '-' || cp == 0x207A || cp == 0x207B || cp == 0x208A || cp == 0x208B ||
        cp == 0x2212) {
        return BidiClass::ES;
    }
    if (cp == '%' || cp == '#' || cp == '$' || cp == 0x00A2 || cp == 0x00A3 || cp == 0x00A4 ||
        cp == 0x00A5 || cp == 0x00B0 || cp == 0x060B || (cp >= 0x20A0 && cp <= 0x20CF)) {
        return BidiClass::ET;
    }
    if (cp == ',' || cp == '.' || cp == '/' || cp == ':' || cp == 0x00A0 || cp == 0x060C ||
        cp == 0x2024) {
        return BidiClass::CS;
    }
    if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') || (cp >= 0x00C0 && cp <= 0x02AF) ||
        (cp >= 0x0400 && cp <= 0x04FF) || (cp >= 0x1E00 && cp <= 0x1EFF) ||
        (cp >= 0x3040 && cp <= 0x30FF) || (cp >= 0x3400 && cp <= 0x9FFF) ||
        (cp >= 0xAC00 && cp <= 0xD7AF) || (cp >= 0xF900 && cp <= 0xFAFF) ||
        (cp >= 0xFF21 && cp <= 0xFF3A) || (cp >= 0xFF41 && cp <= 0xFF5A) ||
        (cp >= 0x20000 && cp <= 0x2A6DF)) {
        return BidiClass::L;
    }
    return BidiClass::ON;
}

Direction direction_of_level(std::uint8_t level) {
    return (level & 1u) != 0 ? Direction::Rtl : Direction::Ltr;
}

std::uint32_t mirror_cp(std::uint32_t cp) {
    switch (cp) {
        case 0x0028:
            return 0x0029;
        case 0x0029:
            return 0x0028;
        case 0x005B:
            return 0x005D;
        case 0x005D:
            return 0x005B;
        case 0x007B:
            return 0x007D;
        case 0x007D:
            return 0x007B;
        case 0x003C:
            return 0x003E;
        case 0x003E:
            return 0x003C;
        case 0x00AB:
            return 0x00BB;
        case 0x00BB:
            return 0x00AB;
        case 0x2018:
            return 0x2019;
        case 0x2019:
            return 0x2018;
        case 0x201C:
            return 0x201D;
        case 0x201D:
            return 0x201C;
        case 0x2039:
            return 0x203A;
        case 0x203A:
            return 0x2039;
        case 0x2045:
            return 0x2046;
        case 0x2046:
            return 0x2045;
        case 0x207D:
            return 0x207E;
        case 0x207E:
            return 0x207D;
        case 0x208D:
            return 0x208E;
        case 0x208E:
            return 0x208D;
        case 0x2264:
            return 0x2265;
        case 0x2265:
            return 0x2264;
        case 0x2266:
            return 0x2267;
        case 0x2267:
            return 0x2266;
        case 0x226A:
            return 0x226B;
        case 0x226B:
            return 0x226A;
        case 0x226E:
            return 0x226F;
        case 0x226F:
            return 0x226E;
        case 0x2282:
            return 0x2283;
        case 0x2283:
            return 0x2282;
        case 0x2284:
            return 0x2285;
        case 0x2285:
            return 0x2284;
        case 0x2286:
            return 0x2287;
        case 0x2287:
            return 0x2286;
        case 0x2308:
            return 0x2309;
        case 0x2309:
            return 0x2308;
        case 0x230A:
            return 0x230B;
        case 0x230B:
            return 0x230A;
        case 0x2329:
            return 0x232A;
        case 0x232A:
            return 0x2329;
        case 0x3008:
            return 0x3009;
        case 0x3009:
            return 0x3008;
        case 0x300A:
            return 0x300B;
        case 0x300B:
            return 0x300A;
        case 0x300C:
            return 0x300D;
        case 0x300D:
            return 0x300C;
        case 0x300E:
            return 0x300F;
        case 0x300F:
            return 0x300E;
        case 0x3010:
            return 0x3011;
        case 0x3011:
            return 0x3010;
        case 0x3014:
            return 0x3015;
        case 0x3015:
            return 0x3014;
        case 0x3016:
            return 0x3017;
        case 0x3017:
            return 0x3016;
        case 0x3018:
            return 0x3019;
        case 0x3019:
            return 0x3018;
        case 0x301A:
            return 0x301B;
        case 0x301B:
            return 0x301A;
        case 0xFF08:
            return 0xFF09;
        case 0xFF09:
            return 0xFF08;
        case 0xFF3B:
            return 0xFF3D;
        case 0xFF3D:
            return 0xFF3B;
        case 0xFF5B:
            return 0xFF5D;
        case 0xFF5D:
            return 0xFF5B;
        default:
            return cp;
    }
}

void resolve_levels(const std::vector<BidiClass>& types, Direction base,
                    std::vector<std::uint8_t>& levels) {
    const std::size_t n = types.size();
    levels.assign(n, 0);
    if (n == 0) {
        return;
    }
    const std::uint8_t paragraph = base == Direction::Rtl ? 1 : 0;
    std::vector<BidiClass> t = types;
    std::vector<char> skip(n, 0);

    std::vector<StackEntry> stack;
    stack.push_back(StackEntry{paragraph, DirOverride::Neutral, EmbedKind::Embedding});
    int overflow_isolate = 0;
    int overflow_embedding = 0;
    int valid_isolate_count = 0;

    auto push_embedding = [&](std::uint8_t new_level, DirOverride ov, EmbedKind kind) {
        if (overflow_isolate > 0) {
            if (kind == EmbedKind::Isolate) {
                ++overflow_isolate;
            } else if (overflow_embedding < 0xffff) {
                ++overflow_embedding;
            }
            return;
        }
        if (new_level > kMaxDepth || stack.size() >= static_cast<std::size_t>(kMaxDepth) + 1) {
            if (kind == EmbedKind::Isolate) {
                ++overflow_isolate;
            } else {
                ++overflow_embedding;
            }
            return;
        }
        stack.push_back(StackEntry{new_level, ov, kind});
        if (kind == EmbedKind::Isolate) {
            ++valid_isolate_count;
        }
    };

    for (std::size_t i = 0; i < n; ++i) {
        const BidiClass c = t[i];
        const StackEntry& top = stack.back();
        levels[i] = top.level;

        if (c == BidiClass::RLE) {
            push_embedding(next_odd(top.level), DirOverride::Neutral, EmbedKind::Embedding);
            skip[i] = 1;
            continue;
        }
        if (c == BidiClass::LRE) {
            push_embedding(next_even(top.level), DirOverride::Neutral, EmbedKind::Embedding);
            skip[i] = 1;
            continue;
        }
        if (c == BidiClass::RLO) {
            push_embedding(next_odd(top.level), DirOverride::Rtl, EmbedKind::Override);
            skip[i] = 1;
            continue;
        }
        if (c == BidiClass::LRO) {
            push_embedding(next_even(top.level), DirOverride::Ltr, EmbedKind::Override);
            skip[i] = 1;
            continue;
        }
        if (c == BidiClass::RLI) {
            push_embedding(next_odd(top.level), DirOverride::Neutral, EmbedKind::Isolate);
            skip[i] = 1;
            continue;
        }
        if (c == BidiClass::LRI) {
            push_embedding(next_even(top.level), DirOverride::Neutral, EmbedKind::Isolate);
            skip[i] = 1;
            continue;
        }
        if (c == BidiClass::FSI) {
            const BidiClass strong = first_strong_in_isolate(types, i + 1);
            if (strong == BidiClass::R) {
                push_embedding(next_odd(top.level), DirOverride::Neutral, EmbedKind::Isolate);
            } else {
                push_embedding(next_even(top.level), DirOverride::Neutral, EmbedKind::Isolate);
            }
            skip[i] = 1;
            continue;
        }
        if (c == BidiClass::PDI) {
            skip[i] = 1;
            if (overflow_isolate > 0) {
                --overflow_isolate;
            } else if (valid_isolate_count > 0) {
                overflow_embedding = 0;
                while (!stack.empty() && stack.back().kind != EmbedKind::Isolate) {
                    stack.pop_back();
                }
                if (!stack.empty() && stack.back().kind == EmbedKind::Isolate) {
                    stack.pop_back();
                    --valid_isolate_count;
                }
            }
            if (!stack.empty()) {
                levels[i] = stack.back().level;
            }
            continue;
        }
        if (c == BidiClass::PDF) {
            skip[i] = 1;
            if (overflow_isolate > 0) {
                // ignored while isolate overflow
            } else if (overflow_embedding > 0) {
                --overflow_embedding;
            } else if (stack.size() >= 2 && stack.back().kind != EmbedKind::Isolate) {
                stack.pop_back();
            }
            if (!stack.empty()) {
                levels[i] = stack.back().level;
            }
            continue;
        }
        if (c == BidiClass::BN) {
            skip[i] = 1;
            continue;
        }
        if (top.override == DirOverride::Ltr) {
            t[i] = BidiClass::L;
        } else if (top.override == DirOverride::Rtl) {
            t[i] = BidiClass::R;
        }
    }

    apply_weak_neutral_implicit(t, levels, skip, paragraph);
}

void reorder_visual(const std::vector<std::uint8_t>& levels, std::vector<std::size_t>& order) {
    const std::size_t n = levels.size();
    order.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        order[i] = i;
    }
    if (n == 0) {
        return;
    }
    std::uint8_t highest = 0;
    std::uint8_t lowest_odd = 255;
    for (std::uint8_t level : levels) {
        highest = std::max(highest, level);
        if ((level & 1u) != 0) {
            lowest_odd = std::min(lowest_odd, level);
        }
    }
    if (lowest_odd == 255) {
        lowest_odd = highest;
        if ((highest & 1u) == 0) {
            if (highest == 0) {
                return;
            }
            lowest_odd = 1;
        }
    }
    for (int lev = static_cast<int>(highest); lev >= static_cast<int>(lowest_odd); --lev) {
        std::size_t i = 0;
        while (i < n) {
            if (levels[order[i]] < static_cast<std::uint8_t>(lev)) {
                ++i;
                continue;
            }
            std::size_t j = i;
            while (j < n && levels[order[j]] >= static_cast<std::uint8_t>(lev)) {
                ++j;
            }
            std::reverse(order.begin() + static_cast<std::ptrdiff_t>(i),
                         order.begin() + static_cast<std::ptrdiff_t>(j));
            i = j;
        }
    }
}

}  // namespace inline_text
}  // namespace layout
}  // namespace zui
