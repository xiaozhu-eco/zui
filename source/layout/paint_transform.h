#ifndef ZUI_SOURCE_LAYOUT_PAINT_TRANSFORM_H
#define ZUI_SOURCE_LAYOUT_PAINT_TRANSFORM_H

#include "layout/box.h"

#include <cmath>
#include <string_view>

namespace zui {
namespace layout {
namespace paint_detail {

struct Aff2 {
    float a = 1;
    float b = 0;
    float c = 0;
    float d = 0;
    float e = 1;
    float f = 0;

    Aff2 then(const Aff2& next) const {
        Aff2 out;
        out.a = next.a * a + next.b * d;
        out.b = next.a * b + next.b * e;
        out.c = next.a * c + next.b * f + next.c;
        out.d = next.d * a + next.e * d;
        out.e = next.d * b + next.e * e;
        out.f = next.d * c + next.e * f + next.f;
        return out;
    }

    bool inverse(Aff2& out) const {
        const float det = a * e - b * d;
        if (std::fabs(det) < 1e-6f) {
            return false;
        }
        const float inv = 1.f / det;
        out.a = e * inv;
        out.b = -b * inv;
        out.d = -d * inv;
        out.e = a * inv;
        out.c = -(out.a * c + out.b * f);
        out.f = -(out.d * c + out.e * f);
        return true;
    }

    void apply(float x, float y, float& ox, float& oy) const {
        ox = a * x + b * y + c;
        oy = d * x + e * y + f;
    }
};

/// Used transform of a box. `m` is the `transform` function list, with CSS Transforms 2
/// `scale`, then `rotate`, then `translate` applied outside that list. `ox`/`oy` are the
/// resolved `transform-origin`. Negative ox/oy means the initial centre (50% 50%).
struct Xform {
    Aff2 m;
    float ox = -1;
    float oy = -1;
};

Aff2 translated(float x, float y);
Xform xform_of(const Box& box, float width, float height);
Aff2 local_transform(float width, float height, const Xform& xform);
/// Whether a local transform matrix leaves the box's own coordinate space untouched. A pure
/// translate is *not* identity: it moves the box in the world, even though `paint_transformed`
/// stays false because the box is still axis aligned.
inline bool matrix_is_identity(const Aff2& m) {
    return m.a == 1.f && m.b == 0.f && m.c == 0.f && m.d == 0.f && m.e == 1.f && m.f == 0.f;
}

}  // namespace paint_detail
}  // namespace layout
}  // namespace zui

#endif
