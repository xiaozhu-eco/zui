#ifndef ZUI_SOURCE_TEXT_FONT_CHAIN_INTERNAL_H
#define ZUI_SOURCE_TEXT_FONT_CHAIN_INTERNAL_H

// Declarations shared by the font_chain translation units. Not part of font_chain.h.

#include "text/font_chain.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <cctype>
#include <string>
#include <unordered_map>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace zui {
namespace text {

struct FaceFile {
    std::string path;
    bool synthetic_bold = false;
};

/// Process-wide FreeType face. Complete in font_chain.cpp; other units only store the pointer.
struct SharedFace;

struct FontChain::Face {
    FT_Face face = nullptr;
    std::string path;
    std::uint32_t key = 0;
    /// The face is standing in for a bold style the family does not have: its advance grows by
    /// `kSyntheticBoldEm` and its glyphs are rasterized with a synthetic bold stroke.
    bool synthetic_bold = false;
    SharedFace* shared = nullptr;
};

/// Family list resolved to face files. Used by `FontChain::init`.
std::vector<FaceFile> files_for(std::string_view families, int weight, bool italic);

/// font-display phase for a family. A name with no timing record is `Use`.
FontDisplayPhase font_display_phase_of(std::string_view family);

inline std::string lower_ascii(std::string_view value) {
    std::string out(value);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

bool generic_family(std::string_view name);
bool add_installed_family(std::vector<FaceFile>& paths, std::unordered_set<std::string>& seen,
                          std::string_view name, int weight, bool italic);
void add_known_name(std::vector<FaceFile>& paths, std::unordered_set<std::string>& seen,
                    std::string_view name, int weight, bool italic);
void add_preference(std::vector<FaceFile>& paths, std::unordered_set<std::string>& seen,
                    const char* const* names, std::size_t count, int weight, bool italic);

std::unordered_map<std::string, std::string>& embedded_fonts();
std::uint64_t& font_registry_generation_ref();

}  // namespace text
}  // namespace zui

#endif
