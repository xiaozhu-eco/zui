#ifndef ZUI_SOURCE_BACKENDS_OPENGL_GL_TEXTURES_H
#define ZUI_SOURCE_BACKENDS_OPENGL_GL_TEXTURES_H

// Glyph (R8, packed atlas) and image (RGBA, one texture per file·level) textures.

#include "backends/glyph_atlas.h"
#include "backends/opengl/gl_context.h"

#include <string>
#include <string_view>
#include <unordered_map>

namespace zui {
namespace backends {

/// One uploaded image texture. `width`/`height` are the pixels actually resident, which is the
/// source resolution at level 0 and a reduced level for a small draw.
struct GlImageTexture {
    GLuint tex = 0;
    int width = 0;
    int height = 0;
};

class GlTextures {
public:
    void create(GlContext& ctx);
    void destroy(GlContext& ctx);

    /// Cached image texture for `path` at a level chosen from the device-pixel draw size. The same
    /// (path, level) reuses one handle; nothing is packed, so a large photo is not capped. Null when
    /// the file does not decode.
    const GlImageTexture* image(GlContext& ctx, std::string_view path, float draw_w, float draw_h);

    GlyphAtlas atlas_;
    GLuint tex_ = 0;

private:
    std::unordered_map<std::string, GlImageTexture> images_;
    int max_edge_ = 2048;
};

}  // namespace backends
}  // namespace zui

#endif
