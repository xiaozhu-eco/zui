#ifndef ZUI_SOURCE_BACKENDS_OPENGL_GL_COMMANDS_H
#define ZUI_SOURCE_BACKENDS_OPENGL_GL_COMMANDS_H

// Batch builder and frame submission: the VAO/VBO, the vertex list, and the display-list walk
// that turns it into draw calls.

#include "backends/glyph_outlines.h"
#include "backends/opengl/gl_context.h"
#include "backends/opengl/gl_programs.h"
#include "backends/opengl/gl_textures.h"
#include "backends/ui_vertex.h"
#include "paint/display_list.h"

#include <cstdint>
#include <vector>

namespace zui {
namespace backends {

class GlCommands {
public:
    void create(GlContext& ctx);
    void destroy(GlContext& ctx);
    void submit(GlContext& ctx, GlPrograms& progs, GlTextures& tex, const paint::DisplayList& list);
    void present(GlContext& ctx);

    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint glyph_vao_ = 0;
    GLuint glyph_vbo_ = 0;
    GLuint curve_ssbo_ = 0;
    std::uint32_t curve_capacity_ = 0;
    std::uint32_t curve_uploaded_ = 0;
    GlyphOutlines outlines_;
    std::vector<GlyphVert> glyph_verts_;
    // Copy of the colour target so a translucent batch can do 8-bit src-over in the shader.
    GLuint dst_tex_ = 0;
    int dst_w_ = 0;
    int dst_h_ = 0;
    std::vector<Vert> verts_;

    /// Offscreen colour target the size of the framebuffer. Group layers and the shadow blur
    /// ping-pong through these. `fbo == 0` means the target has not been created yet.
    struct OffTarget {
        GLuint fbo = 0;
        GLuint tex = 0;
        int w = 0;
        int h = 0;
    };
    bool ensure_target(GlContext& ctx, OffTarget& target);

    std::vector<OffTarget> layers_;
    OffTarget blur_[2]{};
};

}  // namespace backends
}  // namespace zui

#endif