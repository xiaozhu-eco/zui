#include "backends/draw_effect.h"
#include "backends/glyph_outlines.h"
#include "backends/group_alpha.h"
#include "backends/opengl/gl_commands.h"
#include "log/log.h"
#include "perf/perf_budget.h"
#include "backends/round_fill.h"
#include "trace/trace.h"

#include <algorithm>
#include <cmath>

namespace zui {
namespace backends {

void GlCommands::create(GlContext& ctx) {
        ctx.gl_.GenVertexArrays(1, &vao_);
        ctx.gl_.GenBuffers(1, &vbo_);
        ctx.gl_.GenVertexArrays(1, &glyph_vao_);
        ctx.gl_.GenBuffers(1, &glyph_vbo_);
        ctx.gl_.GenBuffers(1, &curve_ssbo_);
}

bool GlCommands::ensure_target(GlContext& ctx, OffTarget& target) {
    if (ctx.gl_.GenFramebuffers == nullptr || ctx.gl_.BindFramebuffer == nullptr ||
        ctx.gl_.FramebufferTexture2D == nullptr || ctx.gl_.GenTextures == nullptr || ctx.fb_w_ <= 0 ||
        ctx.fb_h_ <= 0) {
        return false;
    }
    if (target.fbo == 0) {
        ctx.gl_.GenFramebuffers(1, &target.fbo);
        ctx.gl_.GenTextures(1, &target.tex);
    }
    if (target.fbo == 0 || target.tex == 0) {
        return false;
    }
    if (target.w == ctx.fb_w_ && target.h == ctx.fb_h_) {
        return true;
    }
    ctx.gl_.BindTexture(GL_TEXTURE_2D, target.tex);
    ctx.gl_.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    ctx.gl_.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    ctx.gl_.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    ctx.gl_.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    ctx.gl_.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, ctx.fb_w_, ctx.fb_h_, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                       nullptr);
    ctx.gl_.BindFramebuffer(GL_FRAMEBUFFER, target.fbo);
    ctx.gl_.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.tex, 0);
    if (ctx.gl_.CheckFramebufferStatus != nullptr &&
        ctx.gl_.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        return false;
    }
    target.w = ctx.fb_w_;
    target.h = ctx.fb_h_;
    return true;
}

void GlCommands::destroy(GlContext& ctx) {
    auto drop = [&](OffTarget& target) {
        if (target.fbo != 0 && ctx.gl_.DeleteFramebuffers != nullptr) {
            ctx.gl_.DeleteFramebuffers(1, &target.fbo);
        }
        if (target.tex != 0 && ctx.gl_.DeleteTextures != nullptr) {
            ctx.gl_.DeleteTextures(1, &target.tex);
        }
        target = {};
    };
    for (OffTarget& layer : layers_) {
        drop(layer);
    }
    layers_.clear();
    drop(blur_[0]);
    drop(blur_[1]);
    if (dst_tex_) {
        ctx.gl_.DeleteTextures(1, &dst_tex_);
        dst_tex_ = 0;
        dst_w_ = 0;
        dst_h_ = 0;
    }
    if (vbo_) {
        ctx.gl_.DeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    if (glyph_vbo_) {
        ctx.gl_.DeleteBuffers(1, &glyph_vbo_);
        glyph_vbo_ = 0;
    }
    if (curve_ssbo_) {
        ctx.gl_.DeleteBuffers(1, &curve_ssbo_);
        curve_ssbo_ = 0;
    }
    if (glyph_vao_) {
        ctx.gl_.DeleteVertexArrays(1, &glyph_vao_);
        glyph_vao_ = 0;
    }
    curve_capacity_ = 0;
    curve_uploaded_ = 0;
    if (vao_) {
        ctx.gl_.DeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
}

void GlCommands::submit(GlContext& ctx, GlPrograms& progs, GlTextures& tex,
                        const paint::DisplayList& list) {
    zui::trace::Zone zone("gpu.submit", zui::trace::Category::Gpu);
    {
        verts_.clear();
        glyph_verts_.clear();
        struct Clip {
            paint::Rect scissor;
            paint::Rect shape;
            float radius = 0;
            bool has_xform = false;
            float a = 1, b = 0, c = 0, d = 0, e = 1, f = 0;
            /// Innermost `polygon()` in world px. Empty means the scissor (and radius) is enough.
            std::vector<float> poly;
            bool poly_evenodd = false;
        };
        std::vector<Clip> clips;
        float shape_x = 0;
        float shape_y = 0;
        float shape_w = 0;
        float shape_h = 0;
        float shape_radii[4] = {0, 0, 0, 0};
        float shape_radii_y[4] = {0, 0, 0, 0};
        float shape_s = 0;
        // Sigma of a blurred box-shadow's quad; 0 keeps the plain antialiased edge. The shader
        // turns it into the convolved coverage of the rounded rect (see `shadow_cover`).
        float shape_blur = 0;
        int shape_aff2 = 0;
        float shape_aff2_inv0[4] = {1, 0, 0, 0};
        float shape_aff2_inv1[2] = {1, 0};
        // A gradient rides the shape shader: mode 2 already resolves the rectangle and its radius
        // per fragment, so the stops only need to arrive as uniforms. Four stops is the cap the
        // shader carries; the painter resolves the line and the offsets.
        int grad_count = 0;
        float grad_p0[2] = {0, 0};
        float grad_d[2] = {0, 1};
        float grad_len = 0;
        float grad_off[4] = {0, 0, 0, 0};
        float grad_col[16] = {};
        int grad_repeat = 0;
        int grad_radial = 0;
        int grad_conic = 0;
        float grad_conic_from = 0.f;
        float grad_rx = 1.f;
        float grad_ry = 1.f;
        // One-shot overrides for a layer composite (mode 4) or a blur pass (mode 5). flush clears
        // them so the next ordinary batch cannot inherit the layer texture or the blend mode.
        GLuint pass_tex = 0;
        bool pass_blend_off = false;
        // How many open filter groups are `background-clip: text`. `flush` is defined before the
        // iso stack, so the depth is the flag it can see. Fills inside those groups write RGB
        // and leave alpha at 0.
        int text_clip_depth = 0;
        bool pass_copy_dst = false;
        int pass_blend_mode = 0;
        int pass_mask = 0;
        float pass_opacity = 1.f;
        int pass_blur_radius = 0;
        float pass_blur_sigma = 1.f;
        float pass_blur_dx = 1.f;
        float pass_blur_dy = 0.f;
        float pass_tint_r = 0.f;
        float pass_tint_g = 0.f;
        float pass_tint_b = 0.f;
        float pass_tint_a = 0.f;
        float pass_layer_ox = 0.f;
        float pass_layer_oy = 0.f;
        int pass_color_count = 0;
        float pass_cma[32] = {};
        float pass_cmb[32] = {};
        float pass_cmc[32] = {};
        auto reset_pass = [&]() {
            pass_tex = 0;
            pass_blend_off = false;
            pass_copy_dst = false;
            pass_blend_mode = 0;
            pass_mask = 0;
            pass_opacity = 1.f;
            pass_blur_radius = 0;
            pass_blur_sigma = 1.f;
            pass_blur_dx = 1.f;
            pass_blur_dy = 0.f;
            pass_tint_r = 0.f;
            pass_tint_g = 0.f;
            pass_tint_b = 0.f;
            pass_tint_a = 0.f;
            pass_layer_ox = 0.f;
            pass_layer_oy = 0.f;
            pass_color_count = 0;
        };
        // The texture the current mode-3 batch draws. Set by DrawImage; two images with different
        // textures flush between them, so `flush` binds one handle for the whole batch.
        GLuint image_tex = 0;
        // One polygon clip: the innermost PushClip that carries vertices. The sign of the count
        // is the fill-rule (negative = evenodd). Programs without the uniform ignore location -1.
        auto upload_polygon = [&](GLuint prog) {
            float pts[32] = {};
            int count = 0;
            bool evenodd = false;
            for (int i = static_cast<int>(clips.size()) - 1; i >= 0; --i) {
                const Clip& cl = clips[static_cast<size_t>(i)];
                if (cl.poly.size() < 6) {
                    continue;
                }
                const int npts = static_cast<int>(cl.poly.size() / 2);
                count = npts < 16 ? npts : 16;
                evenodd = cl.poly_evenodd;
                for (int k = 0; k < count * 2; ++k) {
                    pts[k] = cl.poly[static_cast<size_t>(k)];
                }
                break;
            }
            if (ctx.gl_.Uniform1i != nullptr) {
                ctx.gl_.Uniform1i(ctx.gl_.GetUniformLocation(prog, "uPolyCount"),
                                  evenodd ? -count : count);
            }
            if (ctx.gl_.Uniform2fv != nullptr) {
                ctx.gl_.Uniform2fv(ctx.gl_.GetUniformLocation(prog, "uPoly"), 16, pts);
            }
        };
        auto draw_glyphs = [&]() {
            if (glyph_verts_.empty()) {
                return;
            }
            if (progs.glyph_ == 0 || ctx.gl_.BindBufferBase == nullptr ||
                ctx.gl_.BufferSubData == nullptr || ctx.gl_.VertexAttribIPointer == nullptr) {
                static bool once = false;
                if (!once) {
                    ZUI_ERROR("vector text is not supported on this GL context");
                    once = true;
                }
                glyph_verts_.clear();
                return;
            }
            const std::vector<GpuGlyphCurve>& curves = outlines_.curves();
            ctx.gl_.BindBuffer(GL_SHADER_STORAGE_BUFFER, curve_ssbo_);
            if (curves.size() > curve_capacity_) {
                std::uint32_t cap = curve_capacity_ == 0 ? 256u : curve_capacity_;
                while (cap < curves.size()) {
                    cap *= 2u;
                }
                ctx.gl_.BufferData(GL_SHADER_STORAGE_BUFFER,
                                   static_cast<GLsizeiptr>(static_cast<std::size_t>(cap) *
                                                           sizeof(GpuGlyphCurve)),
                                   nullptr, GL_DYNAMIC_DRAW);
                curve_capacity_ = cap;
                curve_uploaded_ = 0;
            }
            if (curves.size() > curve_uploaded_) {
                const std::size_t begin = curve_uploaded_;
                ctx.gl_.BufferSubData(
                    GL_SHADER_STORAGE_BUFFER,
                    static_cast<GLintptr>(begin * sizeof(GpuGlyphCurve)),
                    static_cast<GLsizeiptr>((curves.size() - begin) * sizeof(GpuGlyphCurve)),
                    curves.data() + begin);
                curve_uploaded_ = static_cast<std::uint32_t>(curves.size());
            }
            ctx.gl_.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, curve_ssbo_);
            ctx.gl_.UseProgram(progs.glyph_);
            ctx.gl_.Uniform2f(ctx.gl_.GetUniformLocation(progs.glyph_, "uScreen"),
                              static_cast<float>(ctx.fb_w_) / ctx.scale_,
                              static_cast<float>(ctx.fb_h_) / ctx.scale_);
            if (ctx.gl_.Uniform1f != nullptr) {
                ctx.gl_.Uniform1f(ctx.gl_.GetUniformLocation(progs.glyph_, "uScale"), ctx.scale_);
            }
            float rects[16] = {};
            float radii[4] = {};
            int n = 0;
            for (int i = static_cast<int>(clips.size()) - 1; i >= 0 && n + 3 <= 4; --i) {
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
                const paint::Rect& s = cl.shape;
                rects[n * 4 + 0] = s.x;
                rects[n * 4 + 1] = s.y;
                rects[n * 4 + 2] = s.w;
                rects[n * 4 + 3] = s.h;
                radii[n] = -2.f;
                ++n;
                rects[n * 4 + 0] = ia;
                rects[n * 4 + 1] = ib;
                rects[n * 4 + 2] = ic;
                rects[n * 4 + 3] = id;
                radii[n] = 0.f;
                ++n;
                rects[n * 4 + 0] = ie;
                rects[n * 4 + 1] = iff;
                rects[n * 4 + 2] = cl.radius;
                rects[n * 4 + 3] = 0.f;
                radii[n] = 0.f;
                ++n;
                break;
            }
            for (int i = static_cast<int>(clips.size()) - 1; i >= 0 && n < 4; --i) {
                const Clip& cl = clips[static_cast<size_t>(i)];
                if (cl.has_xform || cl.radius <= 0.5f) {
                    continue;
                }
                const paint::Rect& s = cl.shape;
                rects[n * 4 + 0] = s.x;
                rects[n * 4 + 1] = s.y;
                rects[n * 4 + 2] = s.w;
                rects[n * 4 + 3] = s.h;
                radii[n] = cl.radius;
                ++n;
            }
            if (ctx.gl_.Uniform1i != nullptr) {
                ctx.gl_.Uniform1i(ctx.gl_.GetUniformLocation(progs.glyph_, "uRoundCount"), n);
            }
            if (ctx.gl_.Uniform4fv != nullptr) {
                ctx.gl_.Uniform4fv(ctx.gl_.GetUniformLocation(progs.glyph_, "uRoundRect"), 4, rects);
            }
            if (ctx.gl_.Uniform4f != nullptr) {
                ctx.gl_.Uniform4f(ctx.gl_.GetUniformLocation(progs.glyph_, "uRoundR"), radii[0], radii[1],
                                  radii[2], radii[3]);
            }
            upload_polygon(progs.glyph_);
            ctx.gl_.Enable(GL_BLEND);
            ctx.gl_.BindVertexArray(glyph_vao_);
            ctx.gl_.BindBuffer(GL_ARRAY_BUFFER, glyph_vbo_);
            ctx.gl_.BufferData(GL_ARRAY_BUFFER,
                               static_cast<GLsizeiptr>(glyph_verts_.size() * sizeof(GlyphVert)),
                               glyph_verts_.data(), GL_DYNAMIC_DRAW);
            ctx.gl_.EnableVertexAttribArray(0);
            ctx.gl_.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(GlyphVert), (void*)0);
            ctx.gl_.EnableVertexAttribArray(1);
            ctx.gl_.VertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(GlyphVert),
                                        (void*)(2 * sizeof(float)));
            ctx.gl_.EnableVertexAttribArray(2);
            ctx.gl_.VertexAttribIPointer(2, 2, GL_UNSIGNED_INT, static_cast<GLsizei>(sizeof(GlyphVert)),
                                         (void*)(6 * sizeof(float)));
            ctx.gl_.EnableVertexAttribArray(3);
            ctx.gl_.VertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(GlyphVert),
                                        (void*)(8 * sizeof(float)));
            ctx.gl_.EnableVertexAttribArray(4);
            ctx.gl_.VertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(GlyphVert),
                                        (void*)(11 * sizeof(float)));
            ctx.gl_.DrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(glyph_verts_.size()));
            glyph_verts_.clear();
            ctx.gl_.BindVertexArray(vao_);
        };
        auto flush = [&](int mode) {
            if (batch_uses_glyph_verts(mode)) {
                draw_glyphs();
                reset_pass();
                return;
            }
            if (verts_.empty()) {
                reset_pass();
                return;
            }
            GLuint prog = progs.prog_;
            const GpuProgram effect = program_for_batch(mode, progs.es_);
            if (effect == GpuProgram::Image) {
                prog = progs.image_;
            } else if (effect == GpuProgram::Shadow) {
                prog = progs.shadow_;
            } else if (effect == GpuProgram::Blur) {
                prog = progs.blur_;
            } else if (effect == GpuProgram::Layer) {
                prog = progs.layer_;
            }
            ctx.gl_.UseProgram(prog);
            // DisplayList is logical CSS px; map via uScreen and scale FragCoord with uScale.
            ctx.gl_.Uniform2f(ctx.gl_.GetUniformLocation(prog, "uScreen"),
                          static_cast<float>(ctx.fb_w_) / ctx.scale_, static_cast<float>(ctx.fb_h_) / ctx.scale_);
            if (ctx.gl_.Uniform1f != nullptr) {
                ctx.gl_.Uniform1f(ctx.gl_.GetUniformLocation(prog, "uScale"), ctx.scale_);
            }
            ctx.gl_.Uniform1i(ctx.gl_.GetUniformLocation(prog, "uMode"), mode);
            float rects[16] = {};
            float radii[4] = {};
            int n = 0;
            // Pack Aff2 clips first (3 slots each), then axis-aligned rounded clips.
            for (int i = static_cast<int>(clips.size()) - 1; i >= 0 && n + 3 <= 4; --i) {
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
                const paint::Rect& s = cl.shape;
                rects[n * 4 + 0] = s.x;
                rects[n * 4 + 1] = s.y;
                rects[n * 4 + 2] = s.w;
                rects[n * 4 + 3] = s.h;
                radii[n] = -2.f;
                ++n;
                rects[n * 4 + 0] = ia;
                rects[n * 4 + 1] = ib;
                rects[n * 4 + 2] = ic;
                rects[n * 4 + 3] = id;
                radii[n] = 0.f;
                ++n;
                rects[n * 4 + 0] = ie;
                rects[n * 4 + 1] = iff;
                rects[n * 4 + 2] = cl.radius;
                rects[n * 4 + 3] = 0.f;
                radii[n] = 0.f;
                ++n;
                break;  // one Aff2 clip fits in the 4-slot block
            }
            for (int i = static_cast<int>(clips.size()) - 1; i >= 0 && n < 4; --i) {
                const Clip& cl = clips[static_cast<size_t>(i)];
                if (cl.has_xform || cl.radius <= 0.5f) {
                    continue;
                }
                const paint::Rect& s = cl.shape;
                rects[n * 4 + 0] = s.x;
                rects[n * 4 + 1] = s.y;
                rects[n * 4 + 2] = s.w;
                rects[n * 4 + 3] = s.h;
                radii[n] = cl.radius;
                ++n;
            }
            if (ctx.gl_.Uniform1i != nullptr) {
                ctx.gl_.Uniform1i(ctx.gl_.GetUniformLocation(prog, "uRoundCount"), n);
                ctx.gl_.Uniform1i(ctx.gl_.GetUniformLocation(prog, "uAff2"), shape_aff2);
            }
            if (ctx.gl_.Uniform4fv != nullptr) {
                ctx.gl_.Uniform4fv(ctx.gl_.GetUniformLocation(prog, "uRoundRect"), 4, rects);
            }
            if (ctx.gl_.Uniform4f != nullptr) {
                ctx.gl_.Uniform4f(ctx.gl_.GetUniformLocation(prog, "uRoundR"), radii[0], radii[1],
                              radii[2], radii[3]);
                ctx.gl_.Uniform4f(ctx.gl_.GetUniformLocation(prog, "uShape"), shape_x, shape_y, shape_w,
                              shape_h);
                ctx.gl_.Uniform4f(ctx.gl_.GetUniformLocation(prog, "uAff2Inv0"), shape_aff2_inv0[0],
                              shape_aff2_inv0[1], shape_aff2_inv0[2], shape_aff2_inv0[3]);
            }
            if (ctx.gl_.Uniform4f != nullptr) {
                ctx.gl_.Uniform4f(ctx.gl_.GetUniformLocation(prog, "uShapeRadii"), shape_radii[0],
                              shape_radii[1], shape_radii[2], shape_radii[3]);
                ctx.gl_.Uniform4f(ctx.gl_.GetUniformLocation(prog, "uShapeRadiiY"), shape_radii_y[0],
                              shape_radii_y[1], shape_radii_y[2], shape_radii_y[3]);
            }
            if (ctx.gl_.Uniform2f != nullptr) {
                ctx.gl_.Uniform2f(ctx.gl_.GetUniformLocation(prog, "uAff2Inv1"), shape_aff2_inv1[0],
                              shape_aff2_inv1[1]);
            }
            upload_polygon(prog);
            if (ctx.gl_.Uniform1f != nullptr) {
                ctx.gl_.Uniform1f(ctx.gl_.GetUniformLocation(prog, "uShapeStroke"), shape_s);
                ctx.gl_.Uniform1f(ctx.gl_.GetUniformLocation(prog, "uShapeBlur"), shape_blur);
                ctx.gl_.Uniform1f(ctx.gl_.GetUniformLocation(prog, "uGradLen"), grad_len);
                ctx.gl_.Uniform1f(ctx.gl_.GetUniformLocation(prog, "uConicFrom"), grad_conic_from);
                ctx.gl_.Uniform1f(ctx.gl_.GetUniformLocation(prog, "uGroupOpacity"), pass_opacity);
                ctx.gl_.Uniform1f(ctx.gl_.GetUniformLocation(prog, "uBlurSigma"), pass_blur_sigma);
            }
            ctx.gl_.Uniform1i(ctx.gl_.GetUniformLocation(prog, "uGradCount"), grad_count);
            ctx.gl_.Uniform1i(ctx.gl_.GetUniformLocation(prog, "uGradRepeat"), grad_repeat);
            ctx.gl_.Uniform1i(ctx.gl_.GetUniformLocation(prog, "uRadial"), grad_radial);
            ctx.gl_.Uniform1i(ctx.gl_.GetUniformLocation(prog, "uConic"), grad_conic);
            ctx.gl_.Uniform1i(ctx.gl_.GetUniformLocation(prog, "uBlendMode"), pass_blend_mode);
            ctx.gl_.Uniform1i(ctx.gl_.GetUniformLocation(prog, "uMaskGrad"), pass_mask);
            ctx.gl_.Uniform1i(ctx.gl_.GetUniformLocation(prog, "uBlurRadius"), pass_blur_radius);
            ctx.gl_.Uniform1i(ctx.gl_.GetUniformLocation(prog, "uColorCount"), pass_color_count);
            if (ctx.gl_.Uniform2f != nullptr) {
                ctx.gl_.Uniform2f(ctx.gl_.GetUniformLocation(prog, "uGradP0"), grad_p0[0], grad_p0[1]);
                ctx.gl_.Uniform2f(ctx.gl_.GetUniformLocation(prog, "uGradD"), grad_d[0], grad_d[1]);
                ctx.gl_.Uniform2f(ctx.gl_.GetUniformLocation(prog, "uRadialR"), grad_rx, grad_ry);
                ctx.gl_.Uniform2f(ctx.gl_.GetUniformLocation(prog, "uBlurDir"), pass_blur_dx, pass_blur_dy);
                ctx.gl_.Uniform2f(ctx.gl_.GetUniformLocation(prog, "uLayerOffset"), pass_layer_ox,
                                  pass_layer_oy);
            }
            if (ctx.gl_.Uniform4fv != nullptr) {
                ctx.gl_.Uniform4fv(ctx.gl_.GetUniformLocation(prog, "uCMa"), 8, pass_cma);
                ctx.gl_.Uniform4fv(ctx.gl_.GetUniformLocation(prog, "uCMb"), 8, pass_cmb);
                ctx.gl_.Uniform4fv(ctx.gl_.GetUniformLocation(prog, "uCMc"), 8, pass_cmc);
            }
            if (ctx.gl_.Uniform4f != nullptr) {
                ctx.gl_.Uniform4f(ctx.gl_.GetUniformLocation(prog, "uBlurTint"), pass_tint_r,
                                  pass_tint_g, pass_tint_b, pass_tint_a);
            }
            if (ctx.gl_.Uniform4f != nullptr) {
                ctx.gl_.Uniform4f(ctx.gl_.GetUniformLocation(prog, "uGradOff"), grad_off[0], grad_off[1],
                              grad_off[2], grad_off[3]);
            }
            if (ctx.gl_.Uniform4fv != nullptr) {
                ctx.gl_.Uniform4fv(ctx.gl_.GetUniformLocation(prog, "uGradCol"), 4, grad_col);
            }
            ctx.gl_.Uniform1i(ctx.gl_.GetUniformLocation(prog, "uTex"), 0);
            bool translucent = false;
            if (effect == GpuProgram::Fill) {
                for (const Vert& v : verts_) {
                    if (v.a < 0.999f) {
                        translucent = true;
                        break;
                    }
                }
            }
            const bool int_blend = translucent && ctx.gl_.CopyTexSubImage2D != nullptr &&
                                   ctx.fb_w_ > 0 && ctx.fb_h_ > 0;
            const bool want_dst = (int_blend || pass_copy_dst) && ctx.gl_.CopyTexSubImage2D != nullptr &&
                                  ctx.fb_w_ > 0 && ctx.fb_h_ > 0;
            if (want_dst) {
                if (dst_tex_ == 0) {
                    ctx.gl_.GenTextures(1, &dst_tex_);
                }
                ctx.gl_.ActiveTexture(GL_TEXTURE1);
                ctx.gl_.BindTexture(GL_TEXTURE_2D, dst_tex_);
                ctx.gl_.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                ctx.gl_.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                ctx.gl_.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                ctx.gl_.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                if (dst_w_ != ctx.fb_w_ || dst_h_ != ctx.fb_h_) {
                    ctx.gl_.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, ctx.fb_w_, ctx.fb_h_, 0, GL_RGBA,
                                       GL_UNSIGNED_BYTE, nullptr);
                    dst_w_ = ctx.fb_w_;
                    dst_h_ = ctx.fb_h_;
                }
                ctx.gl_.CopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, ctx.fb_w_, ctx.fb_h_);
                ctx.gl_.Uniform1i(ctx.gl_.GetUniformLocation(prog, "uDst"), 1);
            }
            // background-clip:text fills this layer with straight RGB and alpha 0. Shader alpha
            // still gates RGB so the shape's expanded quad does not paint outside the box.
            // The glyph mask then premuls that RGB by coverage.
            const bool text_clip_fill = text_clip_depth > 0 &&
                                        (mode == kBatchFill || mode == kBatchFillShape);
            if (pass_blend_off) {
                // Mode 4 writes the finished pixel. Mode 5 writes the blurred texel. Hardware
                // blend would premultiply either result a second time.
                ctx.gl_.Uniform1i(ctx.gl_.GetUniformLocation(prog, "uIntBlend"), 0);
                ctx.gl_.Disable(GL_BLEND);
            } else if (int_blend && want_dst && !text_clip_fill) {
                ctx.gl_.Uniform1i(ctx.gl_.GetUniformLocation(prog, "uIntBlend"), 1);
                ctx.gl_.Disable(GL_BLEND);
            } else if (text_clip_fill && ctx.gl_.BlendFuncSeparate != nullptr) {
                if (ctx.gl_.Uniform1i != nullptr) {
                    ctx.gl_.Uniform1i(ctx.gl_.GetUniformLocation(prog, "uIntBlend"), 0);
                }
                ctx.gl_.Enable(GL_BLEND);
                ctx.gl_.BlendFuncSeparate(GL_SRC_ALPHA, GL_ZERO, GL_ZERO, GL_ZERO);
            } else if (ctx.gl_.Uniform1i != nullptr) {
                ctx.gl_.Uniform1i(ctx.gl_.GetUniformLocation(prog, "uIntBlend"), 0);
            }
            ctx.gl_.ActiveTexture(GL_TEXTURE0);
            const GLuint bound_tex =
                pass_tex != 0 ? pass_tex : (mode == kBatchImage ? image_tex : tex.tex_);
            ctx.gl_.BindTexture(GL_TEXTURE_2D, bound_tex);
            ctx.gl_.BindVertexArray(vao_);
            ctx.gl_.BindBuffer(GL_ARRAY_BUFFER, vbo_);
            ctx.gl_.BufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts_.size() * sizeof(Vert)),
                           verts_.data(), GL_DYNAMIC_DRAW);
            ctx.gl_.EnableVertexAttribArray(0);
            ctx.gl_.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vert), (void*)0);
            ctx.gl_.EnableVertexAttribArray(1);
            ctx.gl_.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vert),
                                    (void*)(2 * sizeof(float)));
            ctx.gl_.EnableVertexAttribArray(2);
            ctx.gl_.VertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vert),
                                    (void*)(4 * sizeof(float)));
            ctx.gl_.DrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts_.size()));
            if (int_blend || pass_blend_off) {
                ctx.gl_.Enable(GL_BLEND);
            }
            if (text_clip_fill && ctx.gl_.BlendFuncSeparate != nullptr) {
                ctx.gl_.BlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                                          GL_ONE_MINUS_SRC_ALPHA);
            }
            verts_.clear();
            reset_pass();
        };

        auto apply_clip = [&]() {
            if (clips.empty()) {
                ctx.gl_.Disable(GL_SCISSOR_TEST);
                return;
            }
            const paint::Rect& c = clips.back().scissor;
            // Clip rects are logical; glScissor wants physical framebuffer pixels.
            const float x0 = std::max(0.0f, c.x * ctx.scale_);
            const float y0 = std::max(0.0f, c.y * ctx.scale_);
            const float x1 = std::min(static_cast<float>(ctx.fb_w_), (c.x + c.w) * ctx.scale_);
            const float y1 = std::min(static_cast<float>(ctx.fb_h_), (c.y + c.h) * ctx.scale_);
            const int w = static_cast<int>(std::max(0.0f, x1 - x0));
            const int h = static_cast<int>(std::max(0.0f, y1 - y0));
            ctx.gl_.Enable(GL_SCISSOR_TEST);
            if (w <= 0 || h <= 0) {
                ctx.gl_.Scissor(0, 0, 0, 0);
                return;
            }
            const int y = static_cast<int>(static_cast<float>(ctx.fb_h_) - y1);
            ctx.gl_.Scissor(static_cast<int>(x0), y, w, h);
        };

        auto quad = [&](float x0, float y0, float x1, float y1, float u0, float v0, float u1,
                        float v1, paint::Color col) {
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
                              float y3, paint::Color col, float u0 = 0, float v0 = 0, float u1 = 0,
                              float v1 = 0, float u2 = 0, float v2 = 0, float u3 = 0,
                              float v3 = 0) {
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
        int mode = 0;
        // A sharp primitive with alpha < 255 is its own draw. Integer blend copies the
        // framebuffer once per batch, so a translucent quad packed with the body background
        // composites against the clear, and every opaque quad in that batch is darkened.
        // Rounded draws already flush around the shape shader.
        auto sharp = [&](std::uint8_t alpha, const auto& emit) {
            if (alpha < 255) {
                flush(mode);
                emit();
                flush(mode);
                return;
            }
            emit();
        };
        bool xform_layer = false;
        float xform_a = 1, xform_b = 0, xform_c = 0, xform_d = 0, xform_e = 1, xform_f = 0;
        auto set_shape_aff2 = [&](bool on, float a, float b, float c0, float d, float e, float f) {
            shape_aff2 = 0;
            shape_aff2_inv0[0] = 1;
            shape_aff2_inv0[1] = 0;
            shape_aff2_inv0[2] = 0;
            shape_aff2_inv0[3] = 0;
            shape_aff2_inv1[0] = 1;
            shape_aff2_inv1[1] = 0;
            if (!on) {
                return;
            }
            const float det = a * e - b * d;
            if (std::fabs(det) <= 1e-8f) {
                return;
            }
            const float inv = 1.f / det;
            shape_aff2 = 1;
            shape_aff2_inv0[0] = e * inv;
            shape_aff2_inv0[1] = -b * inv;
            shape_aff2_inv0[3] = -d * inv;
            shape_aff2_inv1[0] = a * inv;
            shape_aff2_inv0[2] = -(shape_aff2_inv0[0] * c0 + shape_aff2_inv0[1] * f);
            shape_aff2_inv1[1] = -(shape_aff2_inv0[3] * c0 + shape_aff2_inv1[0] * f);
        };
        auto emit_round = [&](const paint::Rect& r, const paint::CornerRadii& radii, float stroke,
                              paint::Color col, bool aff2 = false, float a = 1, float b = 0,
                              float c0 = 0, float d = 0, float e = 1, float f = 0,
                              float blur = 0.f, int shape_mode = kBatchFillShape) {
            flush(mode);
            // Snap axis-aligned edges onto device pixels (round half away from zero). A border
            // box at y=9.5 is one solid pixel row in the Edge capture and a two-row blend if the
            // SDF keeps the half pixel. Transformed and blurred shapes stay in their own space.
            shape_x = r.x;
            shape_y = r.y;
            shape_w = r.w;
            shape_h = r.h;
            if (!aff2 && blur <= 0.f) {
                const float x1 = std::round(r.x + r.w);
                const float y1 = std::round(r.y + r.h);
                shape_x = std::round(r.x);
                shape_y = std::round(r.y);
                shape_w = x1 - shape_x;
                shape_h = y1 - shape_y;
            }
            shape_radii[0] = radii.tl;
            shape_radii[1] = radii.tr;
            shape_radii[2] = radii.br;
            shape_radii[3] = radii.bl;
            shape_radii_y[0] = radii.tl_y;
            shape_radii_y[1] = radii.tr_y;
            shape_radii_y[2] = radii.br_y;
            shape_radii_y[3] = radii.bl_y;
            shape_s = stroke;
            shape_blur = blur;
            set_shape_aff2(aff2, a, b, c0, d, e, f);
            // The quad has to reach the end of the blur: the kernel is 3 sigma wide and the
            // fragment shader is handed sigma = blur/2.
            const float o = 1.5f + blur * 1.5f;
            if (aff2 && shape_aff2) {
                const paint::Rect aabb = xform_aabb(r, a, b, c0, d, e, f);
                quad(aabb.x - o, aabb.y - o, aabb.x + aabb.w + o, aabb.y + aabb.h + o, 0, 0, 0, 0,
                     col);
            } else {
                quad(r.x - o, r.y - o, r.x + r.w + o, r.y + r.h + o, 0, 0, 0, 0, col);
            }
            flush(shape_mode);
            set_shape_aff2(false, 1, 0, 0, 0, 1, 0);
            shape_blur = 0;
            mode = 0;
        };

        auto load_gradient = [&](const paint::Gradient& g) {
            grad_p0[0] = g.x;
            grad_p0[1] = g.y;
            grad_d[0] = g.dx;
            grad_d[1] = g.dy;
            grad_len = g.len;
            grad_repeat = g.repeating ? 1 : 0;
            grad_radial = g.kind == paint::GradientKind::Radial ? 1 : 0;
            grad_conic = g.kind == paint::GradientKind::Conic ? 1 : 0;
            grad_conic_from = g.angle;
            grad_rx = g.rx > 0.f ? g.rx : 1.f;
            grad_ry = g.ry > 0.f ? g.ry : 1.f;
            std::vector<paint::GradientGpuChunk> chunks;
            paint::gradient_gpu_chunks(g, chunks);
            if (chunks.empty()) {
                grad_count = 0;
                return;
            }
            const paint::GradientGpuChunk& chunk = chunks[0];
            grad_count = chunk.grad_count;
            for (int i = 0; i < 4; ++i) {
                grad_off[i] = chunk.off[i];
            }
            for (int i = 0; i < 16; ++i) {
                grad_col[i] = chunk.col[i];
            }
        };
        auto clear_gradient = [&]() {
            grad_count = 0;
            grad_repeat = 0;
            grad_radial = 0;
            grad_conic = 0;
            grad_conic_from = 0.f;
        };
        struct IsoGroup {
            float opacity = 1.f;
            int blend = 0;
            bool mask = false;
            bool text_clip = false;
            float blur = 0.f;
            int color_count = 0;
            float cm[paint::FilterEffects::kMaxFilterColorMatrices * 20] = {};
            std::vector<paint::DropShadow> shadows;
            paint::Gradient gradient;
            GLuint prev_fbo = 0;
        };
        std::vector<IsoGroup> iso;
        GLuint current_fbo = ctx.draw_fbo_;
        auto bind_fbo = [&](GLuint fbo) {
            if (ctx.gl_.BindFramebuffer != nullptr) {
                ctx.gl_.BindFramebuffer(GL_FRAMEBUFFER, fbo);
            }
            current_fbo = fbo;
        };
        auto clear_transparent = [&]() {
            ctx.gl_.Disable(GL_SCISSOR_TEST);
            ctx.gl_.ClearColor(0.f, 0.f, 0.f, 0.f);
            ctx.gl_.Clear(GL_COLOR_BUFFER_BIT);
            ctx.gl_.ClearColor(1.f, 1.f, 1.f, 1.f);
            apply_clip();
        };
        auto screen_quad = [&]() {
            const float sw = static_cast<float>(ctx.fb_w_) / std::max(ctx.scale_, 0.0001f);
            const float sh = static_cast<float>(ctx.fb_h_) / std::max(ctx.scale_, 0.0001f);
            quad(0.f, 0.f, sw, sh, 0.f, 0.f, 1.f, 1.f, paint::Color{255, 255, 255, 255});
        };
        // Separable Gaussian into blur_[0]. `sigma_dev` is in framebuffer pixels. A positive
        // `ta` tints the last pass to that premultiplied shadow color (drop-shadow). The two
        // blur targets are shared with backdrop and box-shadow, so callers finish one effect
        // before starting the next.
        auto gaussian = [&](GLuint src, float sigma_dev, float tr, float tg, float tb, float ta,
                            GLuint& out_tex) -> bool {
            if (!ensure_target(ctx, blur_[0]) || !ensure_target(ctx, blur_[1])) {
                return false;
            }
            int radius = 0;
            if (sigma_dev > 0.05f) {
                radius = static_cast<int>(std::ceil(sigma_dev * 3.f));
                if (radius < 1) {
                    radius = 1;
                }
                if (radius > 48) {
                    radius = 48;
                }
            }
            auto pass = [&](OffTarget& dst, GLuint from, float dx, float dy, bool tint) {
                bind_fbo(dst.fbo);
                ctx.gl_.Disable(GL_SCISSOR_TEST);
                pass_tex = from;
                pass_blend_off = true;
                pass_copy_dst = false;
                pass_blur_radius = radius;
                pass_blur_sigma = std::max(sigma_dev, 0.01f);
                pass_blur_dx = dx;
                pass_blur_dy = dy;
                if (tint) {
                    pass_tint_r = tr;
                    pass_tint_g = tg;
                    pass_tint_b = tb;
                    pass_tint_a = ta;
                } else {
                    pass_tint_r = 0.f;
                    pass_tint_g = 0.f;
                    pass_tint_b = 0.f;
                    pass_tint_a = 0.f;
                }
                clear_gradient();
                screen_quad();
                flush(kBatchShadowGauss);
            };
            const bool tint = ta > 0.f;
            if (radius == 0) {
                pass(blur_[0], src, 1.f, 0.f, tint);
                out_tex = blur_[0].tex;
                return true;
            }
            pass(blur_[1], src, 1.f, 0.f, false);
            pass(blur_[0], blur_[1].tex, 0.f, 1.f, tint);
            out_tex = blur_[0].tex;
            return true;
        };

        GroupCursor groups;
        groups.isolate_groups = true;
        submit_items(groups, list.items(), [&](const paint::DisplayItem& it) {
            if (it.op == paint::DisplayOp::BackdropFilter) {
                // Filter Effects 2: blur the pixels already in this target, then replace the
                // border box with that result. The element's own background is painted later,
                // on top. `blur()`'s argument is the Gaussian stddev in CSS px; the kernel
                // steps in framebuffer pixels, so the stddev is scaled.
                flush(mode);
                mode = 0;
                if (!ensure_target(ctx, blur_[0]) || !ensure_target(ctx, blur_[1]) ||
                    ctx.gl_.CopyTexSubImage2D == nullptr || ctx.fb_w_ <= 0 || ctx.fb_h_ <= 0) {
                    return;
                }
                // ensure_target binds the blur framebuffer, and each blur pass overwrites
                // current_fbo. The copy and the composite both belong on the target that was
                // current when this item started.
                const GLuint restore = current_fbo;
                bind_fbo(restore);
                ctx.gl_.ActiveTexture(GL_TEXTURE0);
                ctx.gl_.BindTexture(GL_TEXTURE_2D, blur_[0].tex);
                ctx.gl_.CopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, ctx.fb_w_, ctx.fb_h_);
                const float sigma = std::max(0.f, it.filter.blur) * std::max(ctx.scale_, 0.0001f);
                if (sigma > 0.5f) {
                    perf::note_gpu_effect();
                    int radius = static_cast<int>(std::ceil(sigma * 3.f));
                    if (radius > 48) {
                        radius = 48;
                    }
                    auto blur_pass = [&](OffTarget& dst, GLuint src, float dx, float dy) {
                        bind_fbo(dst.fbo);
                        ctx.gl_.Disable(GL_SCISSOR_TEST);
                        pass_tex = src;
                        pass_blend_off = true;
                        pass_copy_dst = false;
                        pass_blur_radius = radius;
                        pass_blur_sigma = sigma;
                        pass_blur_dx = dx;
                        pass_blur_dy = dy;
                        clear_gradient();
                        screen_quad();
                        flush(kBatchShadowGauss);
                    };
                    blur_pass(blur_[1], blur_[0].tex, 1.f, 0.f);
                    blur_pass(blur_[0], blur_[1].tex, 0.f, 1.f);
                }
                bind_fbo(restore);
                const float sw = static_cast<float>(ctx.fb_w_) / std::max(ctx.scale_, 0.0001f);
                const float sh = static_cast<float>(ctx.fb_h_) / std::max(ctx.scale_, 0.0001f);
                const paint::Rect& box = it.rect;
                const bool uniform = it.radii.tl == it.radii.tr && it.radii.tr == it.radii.br &&
                                     it.radii.br == it.radii.bl && it.radii.tl_y == it.radii.tl &&
                                     it.radii.tr_y == it.radii.tl && it.radii.br_y == it.radii.tl &&
                                     it.radii.bl_y == it.radii.tl && it.radii.tl > 0.5f;
                if (uniform) {
                    Clip c;
                    c.shape = box;
                    c.radius = it.radii.tl;
                    c.scissor = clips.empty() ? box : paint::intersect_rect(clips.back().scissor, box);
                    clips.push_back(c);
                }
                apply_clip();
                const float u0 = box.x / std::max(sw, 0.0001f);
                const float u1 = (box.x + box.w) / std::max(sw, 0.0001f);
                // GL texture row 0 is the bottom of the framebuffer. CSS y grows downward.
                const float v0 = 1.f - box.y / std::max(sh, 0.0001f);
                const float v1 = 1.f - (box.y + box.h) / std::max(sh, 0.0001f);
                const float bright = std::max(0.f, it.filter.brightness);
                const float opac = std::max(0.f, std::min(1.f, it.filter.opacity));
                const float x0 = box.x;
                const float y0 = box.y;
                const float x1 = box.x + box.w;
                const float y1 = box.y + box.h;
                Vert q[6] = {
                    {x0, y0, u0, v0, bright, bright, bright, opac},
                    {x1, y0, u1, v0, bright, bright, bright, opac},
                    {x1, y1, u1, v1, bright, bright, bright, opac},
                    {x0, y0, u0, v0, bright, bright, bright, opac},
                    {x1, y1, u1, v1, bright, bright, bright, opac},
                    {x0, y1, u0, v1, bright, bright, bright, opac},
                };
                verts_.insert(verts_.end(), q, q + 6);
                pass_tex = blur_[0].tex;
                pass_blend_off = false;
                pass_copy_dst = false;
                clear_gradient();
                flush(kBatchImage);
                if (uniform) {
                    clips.pop_back();
                    apply_clip();
                }
                return;
            }
            if (it.op == paint::DisplayOp::BeginFilter) {
                flush(mode);
                mode = 0;
                if (layers_.size() <= iso.size()) {
                    layers_.push_back({});
                }
                OffTarget& layer = layers_[iso.size()];
                if (!ensure_target(ctx, layer)) {
                    return;
                }
                IsoGroup g;
                g.opacity = std::max(0.f, std::min(1.f, it.filter.opacity));
                g.text_clip = it.filter.text_clip;
                g.blur = std::max(0.f, it.filter.blur);
                g.shadows = it.filter.shadows;
                g.color_count = it.filter.has_color_matrix ? it.filter.color_matrix_count : 0;
                if (g.color_count > paint::FilterEffects::kMaxFilterColorMatrices) {
                    g.color_count = paint::FilterEffects::kMaxFilterColorMatrices;
                }
                for (int i = 0; i < g.color_count * 20; ++i) {
                    g.cm[i] = it.filter.color_matrix[i];
                }
                // Shader mode ids are the MixBlendMode values (0 = normal source-over).
                g.blend = static_cast<int>(it.filter.blend);
                g.mask = it.filter.has_mask && it.filter.mask_is_gradient;
                if (g.mask) {
                    g.gradient = it.filter.mask_gradient;
                }
                g.prev_fbo = current_fbo;
                if (g.text_clip) {
                    ++text_clip_depth;
                }
                iso.push_back(std::move(g));
                bind_fbo(layer.fbo);
                clear_transparent();
                return;
            } else if (it.op == paint::DisplayOp::EndFilter) {
                flush(mode);
                mode = 0;
                if (iso.empty()) {
                    return;
                }
                IsoGroup g = std::move(iso.back());
                iso.pop_back();
                if (g.text_clip && text_clip_depth > 0) {
                    --text_clip_depth;
                }
                OffTarget& layer = layers_[iso.size()];
                const float scale = std::max(ctx.scale_, 0.0001f);
                GLuint element_tex = layer.tex;
                const bool blur_element = g.blur > 0.5f;
                // Each drop-shadow is taken from the element's own alpha and drawn under it.
                // A later shadow in the list is painted first, so an earlier one sits closer to
                // the element. `blur()` then softens the element and not those shadows.
                for (int si = static_cast<int>(g.shadows.size()) - 1; si >= 0; --si) {
                    const paint::DropShadow& sh = g.shadows[static_cast<std::size_t>(si)];
                    if (sh.color.a == 0) {
                        continue;
                    }
                    // Chrome applies the drop-shadow radius as the Gaussian stddev (the blur()
                    // parameter), which is wider than the box-shadow radius/2 conversion.
                    const float sigma = std::max(0.f, sh.blur) * scale;
                    GLuint shadow_tex = 0;
                    if (!gaussian(layer.tex, sigma, sh.color.r / 255.f, sh.color.g / 255.f,
                                  sh.color.b / 255.f, sh.color.a / 255.f, shadow_tex)) {
                        continue;
                    }
                    bind_fbo(g.prev_fbo);
                    apply_clip();
                    clear_gradient();
                    pass_tex = shadow_tex;
                    pass_blend_off = true;
                    pass_copy_dst = true;
                    pass_blend_mode = 0;
                    pass_mask = 0;
                    pass_opacity = g.opacity;
                    pass_layer_ox = sh.dx * scale;
                    pass_layer_oy = -sh.dy * scale;
                    screen_quad();
                    flush(kBatchLayer);
                }
                if (blur_element) {
                    GLuint blurred = 0;
                    if (gaussian(layer.tex, g.blur * scale, 0.f, 0.f, 0.f, 0.f, blurred)) {
                        element_tex = blurred;
                    }
                }
                bind_fbo(g.prev_fbo);
                apply_clip();
                if (g.mask) {
                    load_gradient(g.gradient);
                } else {
                    clear_gradient();
                }
                pass_tex = element_tex;
                pass_blend_off = true;
                pass_copy_dst = true;
                pass_blend_mode = g.blend;
                pass_mask = g.mask ? 1 : 0;
                pass_opacity = g.opacity;
                pass_layer_ox = 0.f;
                pass_layer_oy = 0.f;
                pass_color_count = g.color_count;
                for (int s = 0; s < g.color_count; ++s) {
                    const float* row = g.cm + s * 20;
                    pass_cma[s * 4 + 0] = row[0];
                    pass_cma[s * 4 + 1] = row[1];
                    pass_cma[s * 4 + 2] = row[2];
                    pass_cma[s * 4 + 3] = row[4];
                    pass_cmb[s * 4 + 0] = row[5];
                    pass_cmb[s * 4 + 1] = row[6];
                    pass_cmb[s * 4 + 2] = row[7];
                    pass_cmb[s * 4 + 3] = row[9];
                    pass_cmc[s * 4 + 0] = row[10];
                    pass_cmc[s * 4 + 1] = row[11];
                    pass_cmc[s * 4 + 2] = row[12];
                    pass_cmc[s * 4 + 3] = row[14];
                }
                screen_quad();
                flush(kBatchLayer);
                // Multiply/screen, a gradient mask, a color matrix, and a real blur or drop-shadow
                // composite the offscreen layer on the GPU. A plain opacity multiply is the
                // layer's alpha and is not this counter.
                if (g.blend != 0 || g.mask || blur_element || !g.shadows.empty() ||
                    g.color_count > 0) {
                    perf::note_gpu_effect();
                }
                clear_gradient();
                return;
            } else if (it.op == paint::DisplayOp::PushClip) {
                flush(mode);
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
                c.poly = it.poly;
                c.poly_evenodd = it.poly_evenodd;
                clips.push_back(std::move(c));
                apply_clip();
            } else if (it.op == paint::DisplayOp::PopClip) {
                flush(mode);
                if (!clips.empty()) {
                    clips.pop_back();
                }
                apply_clip();
            } else if (it.op == paint::DisplayOp::FillRect) {
                if (mode != 0) {
                    flush(mode);
                    mode = 0;
                }
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
                        sharp(it.color.a, [&] {
                            quad_xform(x0, y0, x1, y1, x2, y2, x3, y3, it.color);
                        });
                    }
                } else if (it.radii.any()) {
                    emit_round(it.rect, it.radii, 0, it.color);
                } else {
                    sharp(it.color.a, [&] {
                        quad(it.rect.x, it.rect.y, it.rect.x + it.rect.w, it.rect.y + it.rect.h, 0,
                             0, 0, 0, it.color);
                    });
                }
            } else if (it.op == paint::DisplayOp::BeginTransform) {
                flush(mode);
                mode = 0;
                xform_layer = it.has_xform;
                xform_a = it.xform_a;
                xform_b = it.xform_b;
                xform_c = it.xform_c;
                xform_d = it.xform_d;
                xform_e = it.xform_e;
                xform_f = it.xform_f;
                return;
            } else if (it.op == paint::DisplayOp::EndTransform) {
                flush(mode);
                mode = 0;
                xform_layer = false;
                return;
            } else if (it.op == paint::DisplayOp::FillGradient) {
                if (mode != 2) {
                    flush(mode);
                    mode = 2;
                }
                load_gradient(it.gradient);
                std::vector<paint::GradientGpuChunk> chunks;
                paint::gradient_gpu_chunks(it.gradient, chunks);
                for (const paint::GradientGpuChunk& chunk : chunks) {
                    grad_count = chunk.grad_count;
                    for (int i = 0; i < 4; ++i) {
                        grad_off[i] = chunk.off[i];
                    }
                    for (int i = 0; i < 16; ++i) {
                        grad_col[i] = chunk.col[i];
                    }
                    emit_round(it.rect, it.radii, chunk.stroke_flag, it.color);
                    flush(mode);
                }
                clear_gradient();
                shape_s = 0;
            } else if (it.op == paint::DisplayOp::StrokeRect) {
                float stroke = it.stroke_width;
                if (it.hole.w > 0.f && it.hole.h > 0.f) {
                    // Asymmetric ring: approximate with mean inset (soft-raster subtracts hole).
                    const float left = std::max(0.f, it.hole.x - it.rect.x);
                    const float top = std::max(0.f, it.hole.y - it.rect.y);
                    const float right =
                        std::max(0.f, (it.rect.x + it.rect.w) - (it.hole.x + it.hole.w));
                    const float bottom =
                        std::max(0.f, (it.rect.y + it.rect.h) - (it.hole.y + it.hole.h));
                    stroke = std::max(1.f, (left + top + right + bottom) * 0.25f);
                }
                if (it.has_xform) {
                    // Sharp Aff2 stroke: four local edge strips. Rounded Aff2: SDF ring.
                    if (it.radii.any()) {
                        emit_round(it.rect, it.radii, stroke, it.color, true, it.xform_a,
                                   it.xform_b, it.xform_c, it.xform_d, it.xform_e, it.xform_f);
                        return;
                    }
                    if (mode != 0) {
                        flush(mode);
                        mode = 0;
                    }
                    const float t = std::max(1.0f, stroke);
                    const paint::Rect& lr = it.rect;
                    sharp(it.color.a, [&] {
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
                    });
                } else if (it.radii.any()) {
                    emit_round(it.rect, it.radii, stroke, it.color);
                } else {
                    if (mode != 0) {
                        flush(mode);
                        mode = 0;
                    }
                    const paint::Color col = it.color;
                    sharp(col.a, [&] {
                        stroke_round_rect(
                            it.rect.x, it.rect.y, it.rect.w, it.rect.h, 0, stroke,
                            [&](float x0, float y0, float x1, float y1, float x2, float y2) {
                                const float r = col.r / 255.f, g = col.g / 255.f, b = col.b / 255.f,
                                            a = col.a / 255.f;
                                Vert t[3] = {{x0, y0, 0, 0, r, g, b, a},
                                             {x1, y1, 0, 0, r, g, b, a},
                                             {x2, y2, 0, 0, r, g, b, a}};
                                verts_.insert(verts_.end(), t, t + 3);
                            });
                    });
                }
            } else if (it.op == paint::DisplayOp::DrawText) {
                const int tag = batch_for(it);
                if (tag == kBatchShadowGlyph) {
                    // Coverage is the glyph program into a mask. The soft edge is the shadow
                    // program's gaussian and blit. GLES has no glyph program and does not
                    // fall back to the atlas.
                    flush(mode);
                    mode = 0;
                    append_glyph_quads(outlines_, it, glyph_verts_);
                    if (progs.glyph_ == 0) {
                        glyph_verts_.clear();
                        return;
                    }
                    if (ensure_target(ctx, blur_[0]) && ensure_target(ctx, blur_[1])) {
                        const GLuint restore = current_fbo;
                        bind_fbo(blur_[0].fbo);
                        clear_transparent();
                        draw_glyphs();
                        const float sigma = std::max(0.5f, it.blur * 0.5f);
                        int radius = static_cast<int>(std::ceil(sigma * 3.f));
                        if (radius > 48) {
                            radius = 48;
                        }
                        auto blur_pass = [&](OffTarget& dst, GLuint src, float dx, float dy) {
                            bind_fbo(dst.fbo);
                            ctx.gl_.Disable(GL_SCISSOR_TEST);
                            pass_tex = src;
                            pass_blend_off = true;
                            pass_copy_dst = false;
                            pass_blur_radius = radius;
                            pass_blur_sigma = sigma;
                            pass_blur_dx = dx;
                            pass_blur_dy = dy;
                            clear_gradient();
                            screen_quad();
                            flush(kBatchShadowGauss);
                        };
                        blur_pass(blur_[1], blur_[0].tex, 1.f, 0.f);
                        blur_pass(blur_[0], blur_[1].tex, 0.f, 1.f);
                        bind_fbo(restore);
                        apply_clip();
                        pass_tex = blur_[0].tex;
                        pass_blend_off = true;
                        pass_copy_dst = true;
                        pass_opacity = 1.f;
                        clear_gradient();
                        screen_quad();
                        flush(kBatchShadowBlit);
                    } else {
                        glyph_verts_.clear();
                    }
                } else {
                    if (mode != kBatchGlyph) {
                        flush(mode);
                        mode = kBatchGlyph;
                    }
                    append_glyph_quads(outlines_, it, glyph_verts_);
                }
            } else if (it.op == paint::DisplayOp::DrawTextMask) {
                flush(mode);
                mode = 0;
                append_glyph_quads(outlines_, it, glyph_verts_);
                if (ctx.gl_.BlendFuncSeparate != nullptr) {
                    ctx.gl_.Enable(GL_BLEND);
                    // Dest RGB is the straight background colour (alpha already 0). Multiply RGB
                    // by glyph coverage and replace alpha with that coverage: premultiplied ink.
                    ctx.gl_.BlendFuncSeparate(GL_ZERO, GL_SRC_ALPHA, GL_ONE, GL_ZERO);
                }
                draw_glyphs();
                if (ctx.gl_.BlendFuncSeparate != nullptr) {
                    ctx.gl_.BlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                                              GL_ONE_MINUS_SRC_ALPHA);
                }
            } else if (it.op == paint::DisplayOp::DrawImage) {
                const GlImageTexture* img =
                    tex.image(ctx, it.text, it.rect.w * ctx.scale_, it.rect.h * ctx.scale_);
                if (img == nullptr) {
                    return;
                }
                const int tag = batch_for(it);
                if (mode != tag || image_tex != img->tex) {
                    flush(mode);
                    mode = tag;
                    image_tex = img->tex;
                }
                // The texture holds the whole file. `src_u*` is the normalized source rect
                // (the full image is 0..1; border-image slices pass a sub-rectangle).
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
                    quad_xform(x0, y0, x1, y1, x2, y2, x3, y3, it.color, it.src_u0, it.src_v0,
                               it.src_u1, it.src_v0, it.src_u1, it.src_v1, it.src_u0, it.src_v1);
                } else {
                    quad(it.rect.x, it.rect.y, it.rect.x + it.rect.w, it.rect.y + it.rect.h,
                         it.src_u0, it.src_v0, it.src_u1, it.src_v1, it.color);
                }
            } else if (it.op == paint::DisplayOp::ShadowRect) {
                const int shadow_tag = batch_for(it);
                if (mode != 0) {
                    flush(mode);
                    mode = 0;
                }
                if (it.inset) {
                    // Inset: `rect` is the border box and `hole` the punch-out the painter already
                    // resolved (border box offset by the shadow's offset, shrunk by its spread).
                    // The shape shader's band is `cov - cover(sd + stroke)`, i.e. a ring of
                    // uniform thickness, so a concentric hole - `inset 0 0 0 2px` on a selected
                    // day cell - is exact and an asymmetric one is drawn with the mean inset,
                    // exactly like `stroke_ring` (a border with per-side widths) below. Carrying
                    // the hole itself would need a second SDF uniform, which the Vulkan push
                    // constant block has no room for (256 bytes; see vk_renderer.cpp), and the two
                    // backends are meant to agree. The blur softens the ring's outer edge rather
                    // than the hole's, so an inset shadow is drawn with a hard edge.
                    const float left = std::max(0.f, it.hole.x - it.rect.x);
                    const float top = std::max(0.f, it.hole.y - it.rect.y);
                    const float right =
                        std::max(0.f, (it.rect.x + it.rect.w) - (it.hole.x + it.hole.w));
                    const float bottom =
                        std::max(0.f, (it.rect.y + it.rect.h) - (it.hole.y + it.hole.h));
                    const float thickness = std::max(0.25f, (left + top + right + bottom) * 0.25f);
                    emit_round(it.rect, it.radii, thickness, it.color, false, 1, 0, 0, 0, 1, 0, 0.f,
                               shadow_tag);
                } else if (it.blur > 0.5f) {
                    // Flush before allocating the blur targets: ensure_target binds the new
                    // framebuffer, and pending quads still belong on the one we are leaving.
                    flush(mode);
                    mode = 0;
                    if (ensure_target(ctx, blur_[0]) && ensure_target(ctx, blur_[1])) {
                    // The shape is blurred on the GPU targets, then composited. Not a CPU pixel loop.
                    perf::note_gpu_effect();
                    // CSS Backgrounds 3: sigma is half the blur radius. A 1D distance-field CDF
                    // overfills the corners (paint-shadow's soft shadow sat about 30 channels
                    // past tolerance there). Draw the shadow shape, blur it for real, then
                    // composite the premultiplied result.
                    const GLuint restore = current_fbo;
                    bind_fbo(blur_[0].fbo);
                    clear_transparent();
                    clear_gradient();
                    emit_round(it.rect, it.radii, 0, it.color, false, 1, 0, 0, 0, 1, 0, 0.f,
                               shadow_tag);
                    const float sigma = std::max(0.5f, it.blur * 0.5f);
                    int radius = static_cast<int>(std::ceil(sigma * 3.f));
                    if (radius > 48) {
                        radius = 48;
                    }
                    auto blur_pass = [&](OffTarget& dst, GLuint src, float dx, float dy) {
                        bind_fbo(dst.fbo);
                        ctx.gl_.Disable(GL_SCISSOR_TEST);
                        pass_tex = src;
                        pass_blend_off = true;
                        pass_copy_dst = false;
                        pass_blur_radius = radius;
                        pass_blur_sigma = sigma;
                        pass_blur_dx = dx;
                        pass_blur_dy = dy;
                        clear_gradient();
                        screen_quad();
                        flush(kBatchShadowGauss);
                    };
                    blur_pass(blur_[1], blur_[0].tex, 1.f, 0.f);
                    blur_pass(blur_[0], blur_[1].tex, 0.f, 1.f);
                    bind_fbo(restore);
                    apply_clip();
                    pass_tex = blur_[0].tex;
                    pass_blend_off = true;
                    pass_copy_dst = true;
                    pass_opacity = 1.f;
                    clear_gradient();
                    screen_quad();
                    flush(kBatchShadowBlit);
                    } else {
                        emit_round(it.rect, it.radii, 0, it.color, false, 1, 0, 0, 0, 1, 0,
                                   it.blur * 0.5f, shadow_tag);
                    }
                } else {
                    emit_round(it.rect, it.radii, 0, it.color, false, 1, 0, 0, 0, 1, 0, 0.f,
                               shadow_tag);
                }
            }
        });
        flush(mode);
    }
}

void GlCommands::present(GlContext& ctx) {
    zui::trace::Zone zone("gpu.present", zui::trace::Category::Gpu);
    if (ctx.window_) {
        ctx.window_->gl_swap();
    }
}

}  // namespace backends
}  // namespace zui
