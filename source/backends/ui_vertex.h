#ifndef ZUI_SOURCE_BACKENDS_UI_VERTEX_H
#define ZUI_SOURCE_BACKENDS_UI_VERTEX_H

// Interleaved UI vertex (position, UV, colour) shared by the GPU backends' pipelines and their
// batch builders.

namespace zui {
namespace backends {

struct Vert {
    float x, y, u, v, r, g, b, a;
};

}  // namespace backends
}  // namespace zui

#endif