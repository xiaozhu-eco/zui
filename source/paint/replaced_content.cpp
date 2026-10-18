#include "paint/painter_internal.h"

#include "paint/display_list.h"

#include "image/image.h"
#include "log/log.h"
#include "perf/perf_budget.h"

#include <cmath>

namespace zui {
namespace paint {

using namespace painter_detail;

void Painter::paint_replaced_content(const layout::Box& box, DisplayList& out, float dx, float dy,
                                     bool paint_self) const {
    if (paint_self && !box.replaced_src.empty() && box.geo.content_width > 0.0f &&
        box.geo.content_height > 0.0f) {
        // Aff2 boxes keep the content rect in border-box local space; axis-aligned boxes stay
        // on world_x/y so object-fit math matches the painted destination.
        const Rect content =
            box.paint_transformed
                ? Rect{dx + box.geo.border.left + box.geo.padding.left,
                       dy + box.geo.border.top + box.geo.padding.top, box.geo.content_width,
                       box.geo.content_height}
                : Rect{box.geo.world_x + dx + box.geo.border.left + box.geo.padding.left,
                       box.geo.world_y + dy + box.geo.border.top + box.geo.padding.top,
                       box.geo.content_width, box.geo.content_height};
        // Object-fit only needs the image's aspect. Rasterizing here and again at the
        // covered draw size made every thumbnail decode twice; the upload is the one
        // raster, and SVG intrinsic size does not render.
        int img_w = 0;
        int img_h = 0;
        const std::uint64_t decodes_before = image::decode_count();
        const bool have_size = image::intrinsic_size(box.replaced_src, img_w, img_h);
        if (image::decode_count() != decodes_before) {
            perf::note_image_decode();
        }
        if (!have_size) {
            // Paint runs every frame, so the failure stays at trace: the layout warn above is the
            // one-shot signal, and this line is for tracing a specific frame's paint walk.
            ZUI_TRACE("[image] paint: replaced source not decodable: {}", box.replaced_src);
        }
        Rect dest = content;
        bool clip_fit = false;
        if (have_size && img_w > 0 && img_h > 0) {
            const std::string* fit = computed(box, "object-fit");
            const std::string* pos = computed(box, "object-position");
            if (resolve_object_fit_rect(content, static_cast<float>(img_w),
                                        static_cast<float>(img_h), fit, pos, dest)) {
                clip_fit = dest.x < content.x - 0.5f || dest.y < content.y - 0.5f ||
                           dest.x + dest.w > content.x + content.w + 0.5f ||
                           dest.y + dest.h > content.y + content.h + 0.5f;
            }
        }
        auto emit_img = [&](Rect r) {
            if (box.paint_transformed) {
                out.draw_image_xform(r, box.replaced_src, box.paint_a, box.paint_b, box.paint_c,
                                     box.paint_d, box.paint_e, box.paint_f);
            } else {
                out.draw_image(r, box.replaced_src);
            }
        };
        if (clip_fit) {
            if (box.paint_transformed) {
                out.push_clip_xform(content, CornerRadii{}, box.paint_a, box.paint_b, box.paint_c,
                                    box.paint_d, box.paint_e, box.paint_f);
            } else {
                out.push_clip(content);
            }
            emit_img(dest);
            out.pop_clip();
        } else {
            emit_img(dest);
        }
    }
}

}  // namespace paint
}  // namespace zui
