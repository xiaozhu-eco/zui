#include "text/font_chain.h"
#include "text/font_chain_internal.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace zui {
namespace text {
namespace {

std::string fold_ascii(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        out.push_back(c < 128 ? static_cast<char>(std::tolower(c)) : static_cast<char>(c));
    }
    return out;
}

std::string unquote(std::string_view raw) {
    std::string s = fold_ascii(raw);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.erase(s.begin());
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }
    if (s.size() >= 2 &&
        ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\''))) {
        s = s.substr(1, s.size() - 2);
    }
    return s;
}

struct Known {
    const char* name;
    const char* regular[4];
    const char* bold[4];
    const char* italic[4];
    const char* bold_italic[4];
};

// Bare file names: the same face lives in the system font folder, in the per-user font
// folder, or under /usr/share/fonts depending on how it was installed, so every name is
// resolved against font_dirs() instead of hardcoding one absolute path per platform.
const Known kKnown[] = {
    {"segoe ui",
     {"segoeui.ttf", nullptr, nullptr, nullptr},
     {"segoeuib.ttf", nullptr, nullptr, nullptr},
     {"segoeuii.ttf", nullptr, nullptr, nullptr},
     {"segoeuiz.ttf", nullptr, nullptr, nullptr}},
    {"arial",
     {"arial.ttf", nullptr, nullptr, nullptr},
     {"arialbd.ttf", nullptr, nullptr, nullptr},
     {"ariali.ttf", nullptr, nullptr, nullptr},
     {"arialbi.ttf", nullptr, nullptr, nullptr}},
    {"microsoft yahei",
     {"msyh.ttc", "msyh.ttf", nullptr, nullptr},
     {"msyhbd.ttc", "msyhbd.ttf", nullptr, nullptr},
     {nullptr, nullptr, nullptr, nullptr},
     {nullptr, nullptr, nullptr, nullptr}},
    {"微软雅黑",
     {"msyh.ttc", "msyh.ttf", nullptr, nullptr},
     {"msyhbd.ttc", "msyhbd.ttf", nullptr, nullptr},
     {nullptr, nullptr, nullptr, nullptr},
     {nullptr, nullptr, nullptr, nullptr}},
    {"noto sans sc",
     {"NotoSansSC-VF.ttf", "NotoSansSC-Regular.otf", "NotoSansSC-Regular.ttf", nullptr},
     {"NotoSansSC-VF.ttf", "NotoSansSC-Bold.otf", nullptr, nullptr},
     {"NotoSansSC-VF.ttf", nullptr, nullptr, nullptr},
     {"NotoSansSC-VF.ttf", nullptr, nullptr, nullptr}},
    {"source han serif sc",
     {"SourceHanSerifSC-Regular.otf", "NotoSerifCJKsc-Regular.otf", nullptr, nullptr},
     {"SourceHanSerifSC-Bold.otf", nullptr, nullptr, nullptr},
     {nullptr, nullptr, nullptr, nullptr},
     {nullptr, nullptr, nullptr, nullptr}},
    {"noto serif cjk sc",
     {"NotoSerifCJKsc-Regular.otf", nullptr, nullptr, nullptr},
     {"NotoSerifCJKsc-Bold.otf", nullptr, nullptr, nullptr},
     {nullptr, nullptr, nullptr, nullptr},
     {nullptr, nullptr, nullptr, nullptr}},
    {"noto serif sc",
     {"NotoSerifSC-VF.ttf", nullptr, nullptr, nullptr},
     {"NotoSerifSC-VF.ttf", nullptr, nullptr, nullptr},
     {"NotoSerifSC-VF.ttf", nullptr, nullptr, nullptr},
     {"NotoSerifSC-VF.ttf", nullptr, nullptr, nullptr}},
    {"simsun",
     {"simsun.ttc", nullptr, nullptr, nullptr},
     {"simsun.ttc", nullptr, nullptr, nullptr},
     {nullptr, nullptr, nullptr, nullptr},
     {nullptr, nullptr, nullptr, nullptr}},
    {"kaiti",
     {"simkai.ttf", nullptr, nullptr, nullptr},
     {nullptr, nullptr, nullptr, nullptr},
     {nullptr, nullptr, nullptr, nullptr},
     {nullptr, nullptr, nullptr, nullptr}},
    {"impact",
     {"impact.ttf", nullptr, nullptr, nullptr},
     {nullptr, nullptr, nullptr, nullptr},
     {nullptr, nullptr, nullptr, nullptr},
     {nullptr, nullptr, nullptr, nullptr}},
    {"times new roman",
     {"times.ttf", nullptr, nullptr, nullptr},
     {"timesbd.ttf", nullptr, nullptr, nullptr},
     {"timesi.ttf", nullptr, nullptr, nullptr},
     {"timesbi.ttf", nullptr, nullptr, nullptr}},
    {"georgia",
     {"georgia.ttf", nullptr, nullptr, nullptr},
     {"georgiab.ttf", nullptr, nullptr, nullptr},
     {"georgiai.ttf", nullptr, nullptr, nullptr},
     {"georgiaz.ttf", nullptr, nullptr, nullptr}},
    {"courier new",
     {"cour.ttf", nullptr, nullptr, nullptr},
     {"courbd.ttf", nullptr, nullptr, nullptr},
     {"couri.ttf", nullptr, nullptr, nullptr},
     {"courbi.ttf", nullptr, nullptr, nullptr}},
    {"consolas",
     {"consola.ttf", nullptr, nullptr, nullptr},
     {"consolab.ttf", nullptr, nullptr, nullptr},
     {"consolai.ttf", nullptr, nullptr, nullptr},
     {"consolaz.ttf", nullptr, nullptr, nullptr}},
    {"dejavu sans",
     {"DejaVuSans.ttf", nullptr, nullptr, nullptr},
     {"DejaVuSans-Bold.ttf", nullptr, nullptr, nullptr},
     {"DejaVuSans-Oblique.ttf", nullptr, nullptr, nullptr},
     {"DejaVuSans-BoldOblique.ttf", nullptr, nullptr, nullptr}},
};

const char* kFallback[] = {
    "segoeui.ttf", "arial.ttf", "msyh.ttc", "msyh.ttf", "NotoSansSC-VF.ttf", "DejaVuSans.ttf",
    // CJK UI faces omit Mathematical Operators. U+22EE VERTICAL ELLIPSIS (the kanban column
    // grip) otherwise loads glyph 0 and paints as .notdef boxes. Segoe UI Symbol is last so
    // it only supplies a codepoint the text faces do not have.
    "seguisym.ttf",
};

// A generic family is mapped by the UA, and the pinned browser maps them to the zh-CN faces on
// this host. Measured with `CSS.getPlatformFontsForNode` (build/font_probe.py): sans-serif ->
// Noto Sans SC, serif -> Noto Serif SC, monospace -> NSimSun, system-ui -> Microsoft YaHei UI,
// cursive -> KaiTi, fantasy -> Impact. `NSimSun` is the non-bitmap face of simsun.ttc, so the
// "simsun" entry reaches the same advances; named families are used as declared and never see
// these lists.
const char* kSansSerif[] = {"noto sans sc", "microsoft yahei", "segoe ui", "arial"};
const char* kSerif[] = {"noto serif sc", "noto serif cjk sc", "source han serif sc", "simsun",
                        "times new roman"};
const char* kMonospace[] = {"simsun", "consolas", "courier new", "dejavu sans"};
const char* kSystemUi[] = {"microsoft yahei ui", "microsoft yahei", "noto sans sc", "segoe ui",
                           "arial"};
const char* kCursive[] = {"kaiti", "noto sans sc"};
const char* kFantasy[] = {"impact", "noto sans sc"};

std::vector<std::string> font_dirs() {
    std::vector<std::string> dirs;
#ifdef _WIN32
    const char* windir = std::getenv("WINDIR");
    dirs.push_back(std::string(windir != nullptr ? windir : "C:\\Windows") + "\\Fonts");
    const char* local = std::getenv("LOCALAPPDATA");
    if (local != nullptr) {
        dirs.push_back(std::string(local) + "\\Microsoft\\Windows\\Fonts");
    }
#else
    dirs.emplace_back("/usr/share/fonts/truetype/dejavu");
    dirs.emplace_back("/usr/share/fonts/truetype/noto");
    dirs.emplace_back("/usr/share/fonts/truetype/liberation");
    dirs.emplace_back("/System/Library/Fonts/Supplemental");
    dirs.emplace_back("/Library/Fonts");
#endif
    return dirs;
}

bool file_exists(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return in.good();
}

void add_path(std::vector<FaceFile>& out, std::unordered_set<std::string>& seen, const char* path,
              bool synthetic_bold = false) {
    if (path == nullptr || seen.count(path) != 0) {
        return;
    }
    seen.insert(path);
    out.push_back(FaceFile{path, synthetic_bold});
}

void add_font_file(std::vector<FaceFile>& out, std::unordered_set<std::string>& seen,
                   const char* file, bool synthetic_bold = false) {
    if (file == nullptr) {
        return;
    }
    static const std::vector<std::string> dirs = font_dirs();
    const std::string name(file);
    const bool rooted =
        name.find(':') != std::string::npos || (!name.empty() && name.front() == '/');
    if (rooted) {
        if (file_exists(name)) {
            add_path(out, seen, name.c_str(), synthetic_bold);
        }
        return;
    }
    for (const std::string& dir : dirs) {
        const std::string candidate = dir + "/" + name;
        if (file_exists(candidate)) {
            add_path(out, seen, candidate.c_str(), synthetic_bold);
            return;
        }
    }
}

/// The file list for the style, plus whether the list is standing in for a bold request the
/// family cannot answer (the `synthetic_bold` note on `FaceFile`).
const char* const* files_for_style(const Known& known, int weight, bool italic,
                                   bool* synthetic_bold) {
    const bool bold = weight >= 600;
    *synthetic_bold = false;
    if (bold && italic && known.bold_italic[0] != nullptr) {
        return known.bold_italic;
    }
    if (italic && known.italic[0] != nullptr) {
        *synthetic_bold = bold;
        return known.italic;
    }
    if (bold && known.bold[0] != nullptr) {
        return known.bold;
    }
    *synthetic_bold = bold;
    return known.regular;
}

// Installed faces, by lowercased family name. A browser resolves a family name against what is
// installed, not against a table baked into it, so `font-family: "Noto Sans"` has to find the file
// the platform would have found. The curated table below stays as a fallback for names whose files
// are not on this machine.
struct FamilyFiles {
    // regular, bold, italic, bold italic - the same style order the curated table uses.
    std::string file[4];
};

std::unordered_map<std::string, FamilyFiles>& installed_families() {
    static const std::unordered_map<std::string, FamilyFiles> table = [] {
        std::unordered_map<std::string, FamilyFiles> found;
        FT_Library library = nullptr;
        if (FT_Init_FreeType(&library) != 0) {
            return found;
        }
        for (const std::string& dir : font_dirs()) {
            std::error_code error;
            for (const std::filesystem::directory_entry& entry :
                 std::filesystem::directory_iterator(dir, error)) {
                if (error) {
                    break;
                }
                // A `Fonts` folder holds entries that are not font files - reparse points to the
                // component store, a `desktop.ini`, subfolders - and stopping at the first one hid
                // every file after it (the iteration is in directory order, so which families were
                // visible depended on that order). Cascadia Code, Consolas and SimSun all live in
                // the same folder: the first was missing and the others were found, which is what
                // pointed at the scan rather than at the family table.
                if (!entry.is_regular_file(error) || error) {
                    continue;
                }
                const std::string extension = lower_ascii(entry.path().extension().string());
                if (extension != ".ttf" && extension != ".ttc" && extension != ".otf") {
                    continue;
                }
                const std::string path = entry.path().string();
                long faces = 1;
                for (long index = 0; index < faces; ++index) {
                    FT_Face face = nullptr;
                    if (FT_New_Face(library, path.c_str(), index, &face) != 0) {
                        break;
                    }
                    if (index == 0) {
                        faces = face->num_faces > 0 ? face->num_faces : 1;
                    }
                    if (face->family_name != nullptr) {
                        const std::string family = lower_ascii(face->family_name);
                        const bool bold = (face->style_flags & FT_STYLE_FLAG_BOLD) != 0;
                        const bool italic = (face->style_flags & FT_STYLE_FLAG_ITALIC) != 0;
                        const int slot = (bold ? 1 : 0) + (italic ? 2 : 0);
                        FamilyFiles& files = found[family];
                        if (files.file[slot].empty()) {
                            files.file[slot] = path;
                        }
                    }
                    FT_Done_Face(face);
                }
            }
        }
        FT_Done_FreeType(library);
        return found;
    }();
    return const_cast<std::unordered_map<std::string, FamilyFiles>&>(table);
}

}  // namespace

bool generic_family(std::string_view name) {
    return name == "sans-serif" || name == "serif" || name == "monospace" || name == "cursive" ||
           name == "fantasy" || name == "system-ui" || name == "ui-sans-serif" || name == "emoji";
}

// Adds the installed files for `name` in the style a browser would pick: the exact style, the
// regular face when that style is missing, and the regular face of the family for the other
// weights. Returns true when the family exists at all.
bool add_installed_family(std::vector<FaceFile>& paths, std::unordered_set<std::string>& seen,
                          std::string_view name, int weight, bool italic) {
    const auto it = installed_families().find(std::string(name));
    if (it == installed_families().end()) {
        return false;
    }
    const FamilyFiles& files = it->second;
    const std::size_t before = paths.size();
    const bool bold = weight >= 600;
    const int slot = (bold ? 1 : 0) + (italic ? 2 : 0);
    if (!files.file[slot].empty()) {
        add_path(paths, seen, files.file[slot].c_str());
    }
    if (paths.size() == before && !files.file[0].empty()) {
        // The regular face standing in for a bold or italic request. Only the bold request is
        // answered with a synthetic stroke; a missing italic face is a separate simulation.
        add_path(paths, seen, files.file[0].c_str(), slot != 0 && bold);
    }
    return paths.size() != before;
}

void add_known_name(std::vector<FaceFile>& paths, std::unordered_set<std::string>& seen,
                    std::string_view name, int weight, bool italic) {
    if (add_installed_family(paths, seen, name, weight, italic)) {
        return;
    }
    for (const Known& known : kKnown) {
        if (name == known.name) {
            const std::size_t before = paths.size();
            bool synthetic_bold = false;
            const char* const* styled = files_for_style(known, weight, italic, &synthetic_bold);
            for (int i = 0; i < 4; ++i) {
                if (styled[i] != nullptr) {
                    add_font_file(paths, seen, styled[i], synthetic_bold);
                }
            }
            if (paths.size() == before) {
                for (int i = 0; i < 4; ++i) {
                    add_font_file(paths, seen, known.regular[i]);
                }
            }
        }
    }
}

void add_preference(std::vector<FaceFile>& paths, std::unordered_set<std::string>& seen,
                    const char* const* names, std::size_t count, int weight, bool italic) {
    for (std::size_t i = 0; i < count; ++i) {
        add_known_name(paths, seen, names[i], weight, italic);
    }
}

std::vector<FaceFile> files_for(std::string_view families, int weight, bool italic) {
    std::vector<std::string> requested;
    std::string cur;
    for (char c : std::string(families)) {
        if (c == ',') {
            requested.push_back(unquote(cur));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) {
        requested.push_back(unquote(cur));
    }

    std::vector<FaceFile> paths;
    std::unordered_set<std::string> seen;
    for (const std::string& name : requested) {
        if (name.empty()) {
            continue;
        }
        // A face registered for this exact family name is the one the comparison recorded
        // against, so it is taken before the platform lookup and before the generic families.
        const auto embedded = embedded_fonts().find(lower_ascii(name));
        if (embedded != embedded_fonts().end()) {
            // A face still in its block or swap period, or one that missed its swap window,
            // does not win over the next family. An already-loaded local file stays `Use`.
            if (font_display_phase_of(name) == FontDisplayPhase::Use) {
                add_path(paths, seen, embedded->second.c_str());
            }
            continue;
        }
        if (name == "sans-serif" || name == "ui-sans-serif") {
            add_preference(paths, seen, kSansSerif, std::size(kSansSerif), weight, italic);
            continue;
        }
        if (name == "cursive") {
            add_preference(paths, seen, kCursive, std::size(kCursive), weight, italic);
            continue;
        }
        if (name == "fantasy") {
            add_preference(paths, seen, kFantasy, std::size(kFantasy), weight, italic);
            continue;
        }
        if (name == "system-ui") {
            add_preference(paths, seen, kSystemUi, std::size(kSystemUi), weight, italic);
            continue;
        }
        if (name == "serif" || name == "ui-serif") {
            add_preference(paths, seen, kSerif, std::size(kSerif), weight, italic);
            continue;
        }
        if (name == "monospace" || name == "ui-monospace") {
            add_preference(paths, seen, kMonospace, std::size(kMonospace), weight, italic);
            continue;
        }
        if (generic_family(name)) {
            continue;
        }
        add_known_name(paths, seen, name, weight, italic);
    }
    // No requested family exists on this machine: a browser falls back to the standard font, which
    // on Windows is the CJK-capable face the `sans-serif` generic resolves to (Noto Sans SC here).
    // The curated fallback below is a glyph-level list, and starting it at Segoe UI made an unknown
    // family measure like Segoe UI instead of like the browser's default face.
    if (paths.empty()) {
        add_preference(paths, seen, kSansSerif, std::size(kSansSerif), weight, italic);
    }
    for (const char* path : kFallback) {
        add_font_file(paths, seen, path);
    }
    return paths;
}

}  // namespace text
}  // namespace zui
