#ifndef ZUI_SOURCE_BACKENDS_OPENGL_GL_PROGRAMS_H
#define ZUI_SOURCE_BACKENDS_OPENGL_GL_PROGRAMS_H

// Fill, image, shadow, blur, and layer composite are separate programs. Desktop GL and GLES share
// each of those bodies; only the `#version` head differs. Desktop GL also builds the vector-glyph
// program. GLES leaves `glyph_` at 0 and does not sample a glyph atlas.

#include "backends/opengl/gl_context.h"

namespace zui {
namespace backends {

class GlPrograms {
public:
    bool create(GlContext& ctx, bool es);

    GLuint prog_ = 0;
    GLuint image_ = 0;
    GLuint shadow_ = 0;
    GLuint blur_ = 0;
    GLuint layer_ = 0;
    GLuint glyph_ = 0;
    bool es_ = false;

private:
    GLuint compile(GlContext& ctx, const char* vs, const char* fs);
};

}  // namespace backends
}  // namespace zui

#endif