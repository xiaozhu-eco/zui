#include "backends/vulkan/renderer_impl.h"

#ifdef ZUI_HAS_VULKAN
namespace zui {
namespace backends {

void VkRenderer::submit(const paint::DisplayList& list) {

        zui::trace::Zone zone("gpu.submit", zui::trace::Category::Gpu);
        verts_.clear();
        glyph_verts_.clear();
        batches_.clear();
        struct Clip {
            paint::Rect scissor;
            paint::Rect shape;
            float radius = 0;
            bool has_xform = false;
            float a = 1, b = 0, c = 0, d = 0, e = 1, f = 0;
        };
        std::vector<Clip> clips;
        auto pack_rounds = [&](DrawBatch& b) {
            b.round_count = 0;
            // Aff2 clip first (3 slots): local rect + inv0 + inv1; then AA rounded clips.
            for (int i = static_cast<int>(clips.size()) - 1; i >= 0 && b.round_count + 3 <= 4;
                 --i) {
                const Clip& cl = clips[static_cast<size_t>(i)];
                if (!cl.has_xform) {
                    continue;
                }
                const float det = cl.a * cl.e - cl.b * cl.d;
                if (std::fabs(det) <= 1e-8f) {
                    continue;
                }
                const float inv = 1.f / det;
                const float ia = cl.e * inv;
                const float ib = -cl.b * inv;
                const float id = -cl.d * inv;
                const float ie = cl.a * inv;
                const float ic = -(ia * cl.c + ib * cl.f);
                const float iff = -(id * cl.c + ie * cl.f);
                {
                    DrawBatch::Round& dst = b.rounds[b.round_count++];
                    dst.x = cl.shape.x;
                    dst.y = cl.shape.y;
                    dst.w = cl.shape.w;
                    dst.h = cl.shape.h;
                    dst.r = -2.f;
                }
                {
                    DrawBatch::Round& dst = b.rounds[b.round_count++];
                    dst.x = ia;
                    dst.y = ib;
                    dst.w = ic;
                    dst.h = id;
                    dst.r = 0.f;
                }
                {
                    DrawBatch::Round& dst = b.rounds[b.round_count++];
                    dst.x = ie;
                    dst.y = iff;
                    dst.w = cl.radius;
                    dst.h = 0.f;
                    dst.r = 0.f;
                }
                break;
            }
            for (int i = static_cast<int>(clips.size()) - 1; i >= 0 && b.round_count < 4; --i) {
                const Clip& cl = clips[static_cast<size_t>(i)];
                if (cl.has_xform || cl.radius <= 0.5f) {
                    continue;
                }
                DrawBatch::Round& dst = b.rounds[b.round_count++];
                dst.x = cl.shape.x;
                dst.y = cl.shape.y;
                dst.w = cl.shape.w;
                dst.h = cl.shape.h;
                dst.r = cl.radius;
            }
        };
        auto same_rounds = [](const DrawBatch& a, const DrawBatch& b) {
            if (a.round_count != b.round_count) {
                return false;
            }
            for (int i = 0; i < a.round_count; ++i) {
                const DrawBatch::Round& u = a.rounds[i];
                const DrawBatch::Round& v = b.rounds[i];
                if (u.x != v.x || u.y != v.y || u.w != v.w || u.h != v.h || u.r != v.r) {
                    return false;
                }
            }
            return true;
        };

        auto vertex_count = [&](const DrawBatch& b) -> std::size_t {
            return batch_uses_glyph_verts(b.mode) ? glyph_verts_.size() : verts_.size();
        };
        auto close_batch = [&]() {
            if (batches_.empty()) {
                return;
            }
            DrawBatch& b = batches_.back();
            b.count = static_cast<uint32_t>(vertex_count(b) - b.first);
            if (b.count == 0) {
                batches_.pop_back();
            }
        };
        auto ensure_batch = [&](int mode, VkDescriptorSet image_set = VK_NULL_HANDLE) {
            const bool has_clip = !clips.empty();
            const paint::Rect clip = has_clip ? clips.back().scissor : paint::Rect{};
            DrawBatch next;
            next.mode = mode;
            next.image_set = image_set;
            next.has_clip = has_clip;
            next.clip = clip;
            pack_rounds(next);
            if (!batches_.empty()) {
                const DrawBatch& b = batches_.back();
                if (b.mode == mode && b.image_set == image_set && b.has_clip == has_clip &&
                    same_rounds(b, next) &&
                    (!has_clip || (b.clip.x == clip.x && b.clip.y == clip.y && b.clip.w == clip.w &&
                                   b.clip.h == clip.h))) {
                    return;
                }
                close_batch();
            }
            next.first = static_cast<uint32_t>(batch_uses_glyph_verts(mode) ? glyph_verts_.size()
                                                                             : verts_.size());
            batches_.push_back(next);
        };

        auto quad = [&](float x0, float y0, float x1, float y1, float u0, float v0, float u1,
                        float v1, paint::Color col, int mode,
                        VkDescriptorSet image_set = VK_NULL_HANDLE) {
            ensure_batch(mode, image_set);
            const float r = col.r / 255.f, g = col.g / 255.f, b = col.b / 255.f, a = col.a / 255.f;
            Vert q[6] = {
                {x0, y0, u0, v0, r, g, b, a}, {x1, y0, u1, v0, r, g, b, a},
                {x1, y1, u1, v1, r, g, b, a}, {x0, y0, u0, v0, r, g, b, a},
                {x1, y1, u1, v1, r, g, b, a}, {x0, y1, u0, v1, r, g, b, a},
            };
            verts_.insert(verts_.end(), q, q + 6);
        };
        // Aff2 local→world: sharp FillRect/Stroke strips / textured text·image quads.
        auto quad_xform = [&](float x0, float y0, float x1, float y1, float x2, float y2, float x3,
                              float y3, paint::Color col, int mode = 0, float u0 = 0, float v0 = 0,
                              float u1 = 0, float v1 = 0, float u2 = 0, float v2 = 0, float u3 = 0,
                              float v3 = 0, VkDescriptorSet image_set = VK_NULL_HANDLE) {
            ensure_batch(mode, image_set);
            const float r = col.r / 255.f, g = col.g / 255.f, b = col.b / 255.f, a = col.a / 255.f;
            Vert q[6] = {
                {x0, y0, u0, v0, r, g, b, a}, {x1, y1, u1, v1, r, g, b, a},
                {x2, y2, u2, v2, r, g, b, a}, {x0, y0, u0, v0, r, g, b, a},
                {x2, y2, u2, v2, r, g, b, a}, {x3, y3, u3, v3, r, g, b, a},
            };
            verts_.insert(verts_.end(), q, q + 6);
        };
        auto map_aff2 = [](float a, float b, float c0, float d, float e, float f, float x, float y,
                           float& ox, float& oy) {
            ox = a * x + b * y + c0;
            oy = d * x + e * y + f;
        };
        auto xform_aabb = [&](const paint::Rect& lr, float a, float b, float c0, float d, float e,
                              float f) {
            float xs[4], ys[4];
            map_aff2(a, b, c0, d, e, f, lr.x, lr.y, xs[0], ys[0]);
            map_aff2(a, b, c0, d, e, f, lr.x + lr.w, lr.y, xs[1], ys[1]);
            map_aff2(a, b, c0, d, e, f, lr.x + lr.w, lr.y + lr.h, xs[2], ys[2]);
            map_aff2(a, b, c0, d, e, f, lr.x, lr.y + lr.h, xs[3], ys[3]);
            float min_x = xs[0], max_x = xs[0], min_y = ys[0], max_y = ys[0];
            for (int i = 1; i < 4; ++i) {
                min_x = std::min(min_x, xs[i]);
                max_x = std::max(max_x, xs[i]);
                min_y = std::min(min_y, ys[i]);
                max_y = std::max(max_y, ys[i]);
            }
            return paint::Rect{min_x, min_y, std::max(0.f, max_x - min_x),
                               std::max(0.f, max_y - min_y)};
        };
        auto emit_round = [&](const paint::Rect& r, const paint::CornerRadii& radii, float stroke,
                              paint::Color col, bool aff2 = false, float a = 1, float b = 0,
                              float c0 = 0, float d = 0, float e = 1, float f = 0,
                              float blur = 0.f, int batch_mode = kBatchFillShape) {
            close_batch();
            DrawBatch batch;
            batch.first = static_cast<uint32_t>(verts_.size());
            batch.mode = batch_mode;
            batch.has_clip = !clips.empty();
            if (batch.has_clip) {
                batch.clip = clips.back().scissor;
            }
            pack_rounds(batch);
            batch.sx = r.x;
            batch.sy = r.y;
            batch.sw = r.w;
            batch.sh = r.h;
            batch.sradii[0] = radii.tl;
            batch.sradii[1] = radii.tr;
            batch.sradii[2] = radii.br;
            batch.sradii[3] = radii.bl;
            batch.sradii_y[0] = radii.tl_y;
            batch.sradii_y[1] = radii.tr_y;
            batch.sradii_y[2] = radii.br_y;
            batch.sradii_y[3] = radii.bl_y;
            batch.sstroke = stroke;
            // Sigma of a blurred box-shadow (0 keeps the plain antialiased edge); the shader turns
            // it into the convolved coverage of the rounded rect.
            batch.sblur = blur;
            // Aff2 rounded: pack inverse into grad_line/grad_off; grad_count=-8 sentinel.
            if (aff2) {
                const float det = a * e - b * d;
                if (std::fabs(det) > 1e-8f) {
                    const float inv = 1.f / det;
                    const float ia = e * inv;
                    const float ib = -b * inv;
                    const float id = -d * inv;
                    const float ie = a * inv;
                    const float ic = -(ia * c0 + ib * f);
                    const float iff = -(id * c0 + ie * f);
                    batch.grad_count = -8;
                    batch.grad_line[0] = ia;
                    batch.grad_line[1] = ib;
                    batch.grad_line[2] = ic;
                    batch.grad_line[3] = id;
                    batch.grad_off[0] = ie;
                    batch.grad_off[1] = iff;
                }
            }
            batches_.push_back(batch);
            // The quad has to reach the end of the blur: the kernel is 3 sigma wide and the
            // fragment shader is handed sigma = blur/2.
            const float o = 1.5f + blur * 1.5f;
            float x0, y0, x1, y1;
            if (aff2 && batch.grad_count == -8) {
                const paint::Rect aabb = xform_aabb(r, a, b, c0, d, e, f);
                x0 = aabb.x - o;
                y0 = aabb.y - o;
                x1 = aabb.x + aabb.w + o;
                y1 = aabb.y + aabb.h + o;
            } else {
                x0 = r.x - o;
                y0 = r.y - o;
                x1 = r.x + r.w + o;
                y1 = r.y + r.h + o;
            }
            const float cr = col.r / 255.f, cg = col.g / 255.f, cb = col.b / 255.f,
                        ca = col.a / 255.f;
            Vert q[6] = {
                {x0, y0, 0, 0, cr, cg, cb, ca}, {x1, y0, 0, 0, cr, cg, cb, ca},
                {x1, y1, 0, 0, cr, cg, cb, ca}, {x0, y0, 0, 0, cr, cg, cb, ca},
                {x1, y1, 0, 0, cr, cg, cb, ca}, {x0, y1, 0, 0, cr, cg, cb, ca},
            };
            verts_.insert(verts_.end(), q, q + 6);
        };
        auto emit_gradient = [&](const paint::Rect& r, const paint::CornerRadii& radii,
                                 const paint::Gradient& g) {
            std::vector<paint::GradientGpuChunk> chunks;
            paint::gradient_gpu_chunks(g, chunks);
            if (chunks.empty()) {
                return;
            }
            const float o = 1.5f;
            const float x0 = r.x - o;
            const float y0 = r.y - o;
            const float x1 = r.x + r.w + o;
            const float y1 = r.y + r.h + o;
            Vert q[6] = {
                {x0, y0, 0, 0, 1, 1, 1, 1}, {x1, y0, 0, 0, 1, 1, 1, 1}, {x1, y1, 0, 0, 1, 1, 1, 1},
                {x0, y0, 0, 0, 1, 1, 1, 1}, {x1, y1, 0, 0, 1, 1, 1, 1}, {x0, y1, 0, 0, 1, 1, 1, 1},
            };
            for (const paint::GradientGpuChunk& chunk : chunks) {
                close_batch();
                DrawBatch b;
                b.first = static_cast<uint32_t>(verts_.size());
                b.mode = 2;
                b.has_clip = !clips.empty();
                if (b.has_clip) {
                    b.clip = clips.back().scissor;
                }
                pack_rounds(b);
                b.sx = r.x;
                b.sy = r.y;
                b.sw = r.w;
                b.sh = r.h;
                b.sradii[0] = radii.tl;
                b.sradii[1] = radii.tr;
                b.sradii[2] = radii.br;
                b.sradii[3] = radii.bl;
                b.sradii_y[0] = radii.tl_y;
                b.sradii_y[1] = radii.tr_y;
                b.sradii_y[2] = radii.br_y;
                b.sradii_y[3] = radii.bl_y;
                b.sstroke = chunk.stroke_flag;
                b.grad_line[0] = g.x;
                b.grad_line[1] = g.y;
                b.grad_line[2] = g.x + g.dx * g.len;
                b.grad_line[3] = g.y + g.dy * g.len;
                b.grad_count = chunk.grad_count;
                for (int i = 0; i < 4; ++i) {
                    b.grad_off[i] = chunk.off[i];
                }
                for (int i = 0; i < 16; ++i) {
                    b.grad_col[i] = chunk.col[i];
                }
                batches_.push_back(b);
                verts_.insert(verts_.end(), q, q + 6);
            }
        };
        bool xform_layer = false;
        float xform_a = 1, xform_b = 0, xform_c = 0, xform_d = 0, xform_e = 1, xform_f = 0;
        GroupCursor groups;
        pending_text_ = false;
        submit_items(groups, list.items(), [&](const paint::DisplayItem& it) {
            if (it.op == paint::DisplayOp::BeginTransform) {
                xform_layer = it.has_xform;
                xform_a = it.xform_a;
                xform_b = it.xform_b;
                xform_c = it.xform_c;
                xform_d = it.xform_d;
                xform_e = it.xform_e;
                xform_f = it.xform_f;
                return;
            } else if (it.op == paint::DisplayOp::EndTransform) {
                xform_layer = false;
                return;
            } else if (it.op == paint::DisplayOp::PushClip) {
                Clip c;
                if (it.has_xform) {
                    // AABB scissor plus shader Aff2 inverse clip (non-axis-aligned).
                    const paint::Rect aabb = xform_aabb(it.rect, it.xform_a, it.xform_b, it.xform_c,
                                                        it.xform_d, it.xform_e, it.xform_f);
                    c.shape = it.rect;
                    c.radius = it.radius;
                    c.has_xform = true;
                    c.a = it.xform_a;
                    c.b = it.xform_b;
                    c.c = it.xform_c;
                    c.d = it.xform_d;
                    c.e = it.xform_e;
                    c.f = it.xform_f;
                    c.scissor =
                        clips.empty() ? aabb : paint::intersect_rect(clips.back().scissor, aabb);
                } else {
                    c.shape = it.rect;
                    c.radius = it.radius;
                    c.scissor = clips.empty()
                                    ? it.rect
                                    : paint::intersect_rect(clips.back().scissor, it.rect);
                }
                clips.push_back(c);
            } else if (it.op == paint::DisplayOp::PopClip) {
                if (!clips.empty()) {
                    clips.pop_back();
                }
            } else if (it.op == paint::DisplayOp::FillRect) {
                const bool layer_xform = xform_layer && !it.has_xform;
                const float xa = layer_xform ? xform_a : it.xform_a;
                const float xb = layer_xform ? xform_b : it.xform_b;
                const float xc = layer_xform ? xform_c : it.xform_c;
                const float xd = layer_xform ? xform_d : it.xform_d;
                const float xe = layer_xform ? xform_e : it.xform_e;
                const float xf = layer_xform ? xform_f : it.xform_f;
                if (it.has_xform || layer_xform) {
                    // Sharp Aff2: corner quad. Rounded Aff2: SDF after inverse Aff2.
                    if (it.radii.any()) {
                        emit_round(it.rect, it.radii, 0, it.color, true, xa, xb, xc, xd, xe, xf);
                    } else {
                        float x0, y0, x1, y1, x2, y2, x3, y3;
                        const paint::Rect& lr = it.rect;
                        map_aff2(xa, xb, xc, xd, xe, xf, lr.x, lr.y, x0, y0);
                        map_aff2(xa, xb, xc, xd, xe, xf, lr.x + lr.w, lr.y, x1, y1);
                        map_aff2(xa, xb, xc, xd, xe, xf, lr.x + lr.w, lr.y + lr.h, x2, y2);
                        map_aff2(xa, xb, xc, xd, xe, xf, lr.x, lr.y + lr.h, x3, y3);
                        quad_xform(x0, y0, x1, y1, x2, y2, x3, y3, it.color);
                    }
                } else if (it.radii.any()) {
                    emit_round(it.rect, it.radii, 0, it.color);
                } else {
                    quad(it.rect.x, it.rect.y, it.rect.x + it.rect.w, it.rect.y + it.rect.h, 0, 0,
                         0, 0, it.color, 0);
                }
            } else if (it.op == paint::DisplayOp::FillGradient) {
                if (it.gradient.kind == paint::GradientKind::Linear) {
                    emit_gradient(it.rect, it.radii, it.gradient);
                }
            } else if (it.op == paint::DisplayOp::StrokeRect) {
                float stroke = it.stroke_width;
                if (it.hole.w > 0.f && it.hole.h > 0.f) {
                    const float left = std::max(0.f, it.hole.x - it.rect.x);
                    const float top = std::max(0.f, it.hole.y - it.rect.y);
                    const float right =
                        std::max(0.f, (it.rect.x + it.rect.w) - (it.hole.x + it.hole.w));
                    const float bottom =
                        std::max(0.f, (it.rect.y + it.rect.h) - (it.hole.y + it.hole.h));
                    stroke = std::max(1.f, (left + top + right + bottom) * 0.25f);
                }
                if (it.has_xform) {
                    if (it.radii.any()) {
                        emit_round(it.rect, it.radii, stroke, it.color, true, it.xform_a,
                                   it.xform_b, it.xform_c, it.xform_d, it.xform_e, it.xform_f);
                        return;
                    }
                    const float t = std::max(1.0f, stroke);
                    const paint::Rect& lr = it.rect;
                    auto emit_strip = [&](float lx0, float ly0, float lx1, float ly1) {
                        float x0, y0, x1, y1, x2, y2, x3, y3;
                        map_aff2(it.xform_a, it.xform_b, it.xform_c, it.xform_d, it.xform_e,
                                 it.xform_f, lx0, ly0, x0, y0);
                        map_aff2(it.xform_a, it.xform_b, it.xform_c, it.xform_d, it.xform_e,
                                 it.xform_f, lx1, ly0, x1, y1);
                        map_aff2(it.xform_a, it.xform_b, it.xform_c, it.xform_d, it.xform_e,
                                 it.xform_f, lx1, ly1, x2, y2);
                        map_aff2(it.xform_a, it.xform_b, it.xform_c, it.xform_d, it.xform_e,
                                 it.xform_f, lx0, ly1, x3, y3);
                        quad_xform(x0, y0, x1, y1, x2, y2, x3, y3, it.color);
                    };
                    emit_strip(lr.x, lr.y, lr.x + lr.w, lr.y + t);
                    emit_strip(lr.x, lr.y + lr.h - t, lr.x + lr.w, lr.y + lr.h);
                    emit_strip(lr.x, lr.y, lr.x + t, lr.y + lr.h);
                    emit_strip(lr.x + lr.w - t, lr.y, lr.x + lr.w, lr.y + lr.h);
                } else if (it.radii.any()) {
                    emit_round(it.rect, it.radii, stroke, it.color);
                } else {
                    const float t = std::max(1.0f, stroke);
                    const float x = it.rect.x, y = it.rect.y, w = it.rect.w, h = it.rect.h;
                    quad(x, y, x + w, y + t, 0, 0, 0, 0, it.color, 0);
                    quad(x, y + h - t, x + w, y + h, 0, 0, 0, 0, it.color, 0);
                    quad(x, y, x + t, y + h, 0, 0, 0, 0, it.color, 0);
                    quad(x + w - t, y, x + w, y + h, 0, 0, 0, 0, it.color, 0);
                }
            } else if (it.op == paint::DisplayOp::DrawText) {
                const int tag = batch_for(it);
                if (tag == kBatchShadowGlyph) {
                    close_batch();
                    DrawBatch batch;
                    batch.first = static_cast<uint32_t>(glyph_verts_.size());
                    batch.mode = tag;
                    batch.sblur = it.blur * 0.5f;
                    batch.has_clip = !clips.empty();
                    if (batch.has_clip) {
                        batch.clip = clips.back().scissor;
                    }
                    pack_rounds(batch);
                    batches_.push_back(batch);
                } else {
                    ensure_batch(tag);
                }
                append_glyph_quads(outlines_, it, glyph_verts_);
            } else if (it.op == paint::DisplayOp::DrawImage) {
                const VkUploads::ImageTexture* img =
                    up_.image(vk_, pl_.dset_layout_, it.text, it.rect.w * scale_,
                              it.rect.h * scale_);
                if (img == nullptr) {
                    return;
                }
                // Each image texture holds the whole file, so the quad maps the full [0,1] range.
                if (it.has_xform) {
                    const paint::Rect& lr = it.rect;
                    float x0, y0, x1, y1, x2, y2, x3, y3;
                    map_aff2(it.xform_a, it.xform_b, it.xform_c, it.xform_d, it.xform_e, it.xform_f,
                             lr.x, lr.y, x0, y0);
                    map_aff2(it.xform_a, it.xform_b, it.xform_c, it.xform_d, it.xform_e, it.xform_f,
                             lr.x + lr.w, lr.y, x1, y1);
                    map_aff2(it.xform_a, it.xform_b, it.xform_c, it.xform_d, it.xform_e, it.xform_f,
                             lr.x + lr.w, lr.y + lr.h, x2, y2);
                    map_aff2(it.xform_a, it.xform_b, it.xform_c, it.xform_d, it.xform_e, it.xform_f,
                             lr.x, lr.y + lr.h, x3, y3);
                    quad_xform(x0, y0, x1, y1, x2, y2, x3, y3, it.color, batch_for(it), 0.f, 0.f, 1.f,
                               0.f, 1.f, 1.f, 0.f, 1.f, img->set);
                } else {
                    quad(it.rect.x, it.rect.y, it.rect.x + it.rect.w, it.rect.y + it.rect.h, 0.f,
                         0.f, 1.f, 1.f, it.color, batch_for(it), img->set);
                }
            } else if (it.op == paint::DisplayOp::BackdropFilter) {
                // The quad samples the blurred copy of this target. UV is filled at draw time
                // from the pass extent; here the rect is stored and the verts cover the box.
                close_batch();
                float css_w = 0.f;
                float css_h = 0.f;
                if (off_w_ > 0 && off_h_ > 0) {
                    css_w = static_cast<float>(off_w_);
                    css_h = static_cast<float>(off_h_);
                } else if (window_ != nullptr && scale_ > 0.f) {
                    int fw = 0;
                    int fh = 0;
                    window_->framebuffer_size(fw, fh);
                    css_w = static_cast<float>(fw) / scale_;
                    css_h = static_cast<float>(fh) / scale_;
                }
                if (css_w <= 0.f || css_h <= 0.f) {
                    return;
                }
                DrawBatch batch;
                batch.mode = kBatchBackdrop;
                batch.first = static_cast<uint32_t>(verts_.size());
                batch.sblur = it.filter.blur;
                batch.sx = it.rect.x;
                batch.sy = it.rect.y;
                batch.sw = it.rect.w;
                batch.sh = it.rect.h;
                batch.has_clip = !clips.empty();
                if (batch.has_clip) {
                    batch.clip = clips.back().scissor;
                }
                pack_rounds(batch);
                const bool uniform = it.radii.tl == it.radii.tr && it.radii.tr == it.radii.br &&
                                     it.radii.br == it.radii.bl && it.radii.tl_y == it.radii.tl &&
                                     it.radii.tr_y == it.radii.tl && it.radii.br_y == it.radii.tl &&
                                     it.radii.bl_y == it.radii.tl && it.radii.tl > 0.5f;
                if (uniform && batch.round_count < 4) {
                    DrawBatch::Round& rd = batch.rounds[batch.round_count++];
                    rd.x = it.rect.x;
                    rd.y = it.rect.y;
                    rd.w = it.rect.w;
                    rd.h = it.rect.h;
                    rd.r = it.radii.tl;
                }
                batches_.push_back(batch);
                const float u0 = it.rect.x / css_w;
                const float u1 = (it.rect.x + it.rect.w) / css_w;
                // Vulkan image row 0 is the top of the color attachment. CSS y grows downward.
                const float v0 = it.rect.y / css_h;
                const float v1 = (it.rect.y + it.rect.h) / css_h;
                const float bright = std::max(0.f, it.filter.brightness);
                const float opac = std::max(0.f, std::min(1.f, it.filter.opacity));
                const float x0 = it.rect.x;
                const float y0 = it.rect.y;
                const float x1 = it.rect.x + it.rect.w;
                const float y1 = it.rect.y + it.rect.h;
                Vert q[6] = {
                    {x0, y0, u0, v0, bright, bright, bright, opac},
                    {x1, y0, u1, v0, bright, bright, bright, opac},
                    {x1, y1, u1, v1, bright, bright, bright, opac},
                    {x0, y0, u0, v0, bright, bright, bright, opac},
                    {x1, y1, u1, v1, bright, bright, bright, opac},
                    {x0, y1, u0, v1, bright, bright, bright, opac},
                };
                verts_.insert(verts_.end(), q, q + 6);
                perf::note_gpu_effect();
            } else if (it.op == paint::DisplayOp::ShadowRect) {
                if (it.inset) {
                    // Inset: `rect` is the border box and `hole` the punch-out the painter already
                    // resolved (border box offset by the shadow's offset, shrunk by its spread).
                    // The shader's band is `cov - cover(sd + stroke)`, a ring of uniform thickness,
                    // so a concentric hole - `inset 0 0 0 2px` on a selected day cell - is exact and
                    // an asymmetric one is drawn with the mean inset, exactly like `stroke_ring` (a
                    // border with per-side widths) above. Carrying the hole itself needs a second
                    // SDF, and the push constant block is full (256 bytes, asserted below), so GL
                    // takes the same approximation and the two backends stay identical. The blur
                    // softens the ring's outer edge rather than the hole's, so an inset shadow is
                    // drawn with a hard edge.
                    const float left = std::max(0.f, it.hole.x - it.rect.x);
                    const float top = std::max(0.f, it.hole.y - it.rect.y);
                    const float right =
                        std::max(0.f, (it.rect.x + it.rect.w) - (it.hole.x + it.hole.w));
                    const float bottom =
                        std::max(0.f, (it.rect.y + it.rect.h) - (it.hole.y + it.hole.h));
                    const float thickness = std::max(0.25f, (left + top + right + bottom) * 0.25f);
                    emit_round(it.rect, it.radii, thickness, it.color, false, 1, 0, 0, 0, 1, 0, 0.f,
                               batch_for(it));
                } else if (it.blur > 0.5f) {
                    // Soft outer shadow is the shadow program plus the blur targets, not a
                    // CPU readback of the framebuffer.
                    perf::note_gpu_effect();
                    emit_round(it.rect, it.radii, 0, it.color, false, 1, 0, 0, 0, 1, 0,
                               it.blur * 0.5f, batch_for(it));
                } else {
                    emit_round(it.rect, it.radii, 0, it.color, false, 1, 0, 0, 0, 1, 0, 0.f,
                               batch_for(it));
                }
            }
        });
        close_batch();
        // Quads already on the device are kept when this frame matches them, or when every
        // quad is that buffer plus one translation. The draw then shifts the viewport, so a
        // scroll does not copy the page again. Anything else is copied once.
        const bool reuse = reuse_device_quads();
        // A failed match must not keep a previous frame's viewport shift. That shift was
        // computed against the old buffer; applying it to quads we are about to upload
        // would move them twice.
        if (!reuse) {
            batch_ox_.assign(batches_.size(), 0.f);
            batch_oy_.assign(batches_.size(), 0.f);
        }
        if (!curves_.sync(vk_, outlines_.curves(), glyph_verts_, !reuse)) {
            ZUI_ERROR("outline shelf upload failed");
        }
        if (curves_.buffer_replaced()) {
            pl_.bind_curves(vk_.device_, curves_.curve_buffer());
            curves_.clear_replaced();
        }
        if (!reuse && fr_.vbo_map_ && !verts_.empty()) {
            size_t bytes = verts_.size() * sizeof(Vert);
            if (bytes > fr_.vbo_size_) {
                bytes = fr_.vbo_size_;
            }
            std::memcpy(fr_.vbo_map_, verts_.data(), bytes);
            perf::note_gpu_upload();
            gpu_verts_ = verts_;
            gpu_glyphs_ = glyph_verts_;
            gpu_batch_key_.clear();
            gpu_batch_key_.reserve(batches_.size());
            for (const DrawBatch& b : batches_) {
                gpu_batch_key_.push_back(BatchKey{b.first, b.count, b.mode});
            }
        }
        const GlyphPaintDiag glyphs = take_glyph_paint_diag();
        const text::OutlineCaptureDiag outlines = text::take_outline_capture_diag();
        const image::SvgRasterDiag svg = image::take_svg_raster_diag();
        if (glyphs.glyphs > 0 || svg.n > 0) {
            ZUI_TRACE("[ss] p={:.1f} ld={:.1f} xf={:.1f} g={} m={} cap={:.1f} ft={:.1f} dec={:.1f} ol={:.1f} im={:.1f}",
                      glyphs.prepare_ms, glyphs.load_ms, glyphs.xfer_ms, glyphs.glyphs,
                      glyphs.span_miss, glyphs.capture_ms, outlines.ft_load_ms,
                      outlines.decompose_ms, outlines.load_ms, 0.0);
            if (svg.n > 0) {
                ZUI_TRACE("[svg] n={} build={:.1f} rec={:.1f} wait={:.1f} copy={:.1f}", svg.n,
                          svg.build_ms, svg.record_ms, svg.wait_ms, svg.copy_ms);
            }
        }
        last_list_ = &list;
        last_vert_count_ = verts_.size();
    
}

}  // namespace backends
}  // namespace zui
#endif  // ZUI_HAS_VULKAN
