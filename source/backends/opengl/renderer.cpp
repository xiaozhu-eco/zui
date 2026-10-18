
#include "backends/renderer.h"
#include "backends/ui_vertex.h"
#include "backends/opengl/gl_functions.h"
#include "backends/opengl/gl_context.h"
#include "backends/opengl/gl_programs.h"
#include "backends/opengl/gl_textures.h"
#include "backends/opengl/gl_commands.h"
#include "backends/round_fill.h"
#include "image/svg/render/gl_svg_device.h"
#include "image/svg/render/runtime.h"
#include "text/font_chain.h"

#include "backends/frame_probe.h"

#include <cstring>
#include <vector>

namespace zui {
namespace backends {

class GlRenderer final : public IRenderer {
public:
    explicit GlRenderer(bool es)
        : es_(es) {}
    image::svg::GlSvgDevice svg_device_;
    ~GlRenderer() override { destroy(); }

    const char* name() const override { return es_ ? "gles" : "gl"; }

    bool create(IWindow& window) override {
        if (!ctx_.create(window)) {
            return false;
        }
        if (!progs_.create(ctx_, es_)) {
            return false;
        }
        tex_.create(ctx_);
        cmds_.create(ctx_);
        ctx_.gl_.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
        ctx_.gl_.Enable(GL_BLEND);
        // Alpha uses GL_ONE. GL_SRC_ALPHA on the alpha channel squares coverage, so a
        // 0.55 mask stores alpha 192 and a viewer composites the dimmed colour back toward white.
        if (ctx_.gl_.BlendFuncSeparate != nullptr) {
            ctx_.gl_.BlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                                       GL_ONE_MINUS_SRC_ALPHA);
        } else {
            ctx_.gl_.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
        svg_device_.attach(ctx_, progs_.prog_);
        image::svg::Runtime::instance().bind(&svg_device_);
        return true;
    }

    void destroy() override {
        if (ctx_.window_ != nullptr) {
            ctx_.window_->gl_make_current();
        }
        image::svg::Runtime::instance().reset();
        svg_device_.shutdown();
        destroy_offscreen();
        cmds_.destroy(ctx_);
        tex_.destroy(ctx_);
        if (progs_.glyph_) {
            ctx_.gl_.DeleteProgram(progs_.glyph_);
            progs_.glyph_ = 0;
        }
        if (progs_.layer_) {
            ctx_.gl_.DeleteProgram(progs_.layer_);
            progs_.layer_ = 0;
        }
        if (progs_.blur_) {
            ctx_.gl_.DeleteProgram(progs_.blur_);
            progs_.blur_ = 0;
        }
        if (progs_.shadow_) {
            ctx_.gl_.DeleteProgram(progs_.shadow_);
            progs_.shadow_ = 0;
        }
        if (progs_.image_) {
            ctx_.gl_.DeleteProgram(progs_.image_);
            progs_.image_ = 0;
        }
        if (progs_.prog_) {
            ctx_.gl_.DeleteProgram(progs_.prog_);
            progs_.prog_ = 0;
        }
        ctx_.window_ = nullptr;
    }
    void resize(int width, int height) override {
        ctx_.fb_w_ = width > 1 ? width : 1;
        ctx_.fb_h_ = height > 1 ? height : 1;
    }

    void begin_frame() override {
        if (ctx_.window_ == nullptr) {
            return;
        }
        ctx_.window_->gl_make_current();
        if (off_w_ > 0 && fbo_ != 0 && ctx_.gl_.BindFramebuffer != nullptr) {
            ctx_.fb_w_ = off_w_;
            ctx_.fb_h_ = off_h_;
            ctx_.scale_ = 1.f;
            ctx_.gl_.BindFramebuffer(GL_FRAMEBUFFER, fbo_);
            ctx_.draw_fbo_ = fbo_;
        } else {
            if (ctx_.gl_.BindFramebuffer != nullptr) {
                ctx_.gl_.BindFramebuffer(GL_FRAMEBUFFER, 0);
            }
            ctx_.draw_fbo_ = 0;
            int w = 0, h = 0;
            ctx_.window_->framebuffer_size(w, h);
            resize(w, h);
            ctx_.scale_ = ctx_.window_->content_scale();
            if (!(ctx_.scale_ > 0.f)) {
                ctx_.scale_ = 1.f;
            }
        }
        ctx_.gl_.Viewport(0, 0, ctx_.fb_w_, ctx_.fb_h_);
        ctx_.gl_.Disable(GL_SCISSOR_TEST);
        // Document canvas. The browser paints uncovered pixels white. A dark clear
        // stays visible wherever the page does not draw, and that is the picture.
        ctx_.gl_.ClearColor(1.f, 1.f, 1.f, 1.f);
        ctx_.gl_.Clear(GL_COLOR_BUFFER_BIT);
    }
    void submit(const paint::DisplayList& list) override {
        cmds_.submit(ctx_, progs_, tex_, list);
    }

    void present() override { cmds_.present(ctx_); }

    bool read_frame(std::vector<std::uint8_t>& rgba, int& width, int& height) override {
#if !ZUI_PROBE_FRAME
        rgba.clear();
        width = 0;
        height = 0;
        return false;
#else
        if (ctx_.window_ == nullptr || ctx_.gl_.ReadPixels == nullptr || ctx_.fb_w_ <= 0 ||
            ctx_.fb_h_ <= 0) {
            rgba.clear();
            width = 0;
            height = 0;
            return false;
        }
        ctx_.window_->gl_make_current();
        if (off_w_ > 0 && fbo_ != 0 && ctx_.gl_.BindFramebuffer != nullptr) {
            ctx_.gl_.BindFramebuffer(GL_FRAMEBUFFER, fbo_);
        }
        width = ctx_.fb_w_;
        height = ctx_.fb_h_;
        std::vector<std::uint8_t> bottom(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
        ctx_.gl_.PixelStorei(GL_PACK_ALIGNMENT, 1);
        ctx_.gl_.ReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, bottom.data());
        rgba.resize(bottom.size());
        const int stride = width * 4;
        for (int y = 0; y < height; ++y) {
            std::memcpy(rgba.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride),
                        bottom.data() + static_cast<std::size_t>(height - 1 - y) * static_cast<std::size_t>(stride),
                        static_cast<std::size_t>(stride));
        }
        return true;
#endif
    }

    /// Draw the next frames into an FBO of exactly this size at scale 1. The window stays a
    /// context host, so a hidden window's drawable size does not decide the picture.
    bool prepare_offscreen(int width, int height) {
#if !ZUI_PROBE_FRAME
        (void)width;
        (void)height;
        return false;
#else
        if (ctx_.window_ == nullptr || width <= 0 || height <= 0 ||
            ctx_.gl_.GenFramebuffers == nullptr || ctx_.gl_.BindFramebuffer == nullptr ||
            ctx_.gl_.FramebufferTexture2D == nullptr || ctx_.gl_.CheckFramebufferStatus == nullptr) {
            return false;
        }
        ctx_.window_->gl_make_current();
        if (fbo_ == 0) {
            ctx_.gl_.GenFramebuffers(1, &fbo_);
            ctx_.gl_.GenTextures(1, &color_);
        }
        if (width != off_w_ || height != off_h_) {
            ctx_.gl_.BindTexture(GL_TEXTURE_2D, color_);
            ctx_.gl_.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            ctx_.gl_.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            ctx_.gl_.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
                                GL_UNSIGNED_BYTE, nullptr);
            ctx_.gl_.BindFramebuffer(GL_FRAMEBUFFER, fbo_);
            ctx_.gl_.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                          color_, 0);
            if (ctx_.gl_.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                return false;
            }
            off_w_ = width;
            off_h_ = height;
        }
        return fbo_ != 0;
#endif
    }

private:
    void destroy_offscreen() {
        off_w_ = 0;
        off_h_ = 0;
        if (ctx_.gl_.DeleteFramebuffers != nullptr && fbo_ != 0) {
            ctx_.gl_.DeleteFramebuffers(1, &fbo_);
        }
        fbo_ = 0;
        if (color_ != 0) {
            ctx_.gl_.DeleteTextures(1, &color_);
            color_ = 0;
        }
    }

    bool es_ = false;
    GlContext ctx_;
    GlPrograms progs_;
    GlTextures tex_;
    GlCommands cmds_;
    int off_w_ = 0;
    int off_h_ = 0;
    GLuint fbo_ = 0;
    GLuint color_ = 0;
};

std::unique_ptr<IRenderer> create_gl_renderer() { return std::make_unique<GlRenderer>(false); }
std::unique_ptr<IRenderer> create_gles_renderer() { return std::make_unique<GlRenderer>(true); }

ProbeFrame probe_read_frame(const paint::DisplayList& list, int width, int height,
                            const std::vector<ProbeFontFace>& fonts,
                            std::vector<std::uint8_t>& rgba) {
#if !ZUI_PROBE_FRAME
    (void)list;
    (void)width;
    (void)height;
    (void)fonts;
    rgba.clear();
    return ProbeFrame::Unavailable;
#else
    // The hidden window is only a GL context. Pointer and key input stay on
    // source/input/Interaction; this function does not poll the window.
    // These are intentionally leaked. Destroying the GL context from a static
    // destructor runs after SDL's own shutdown and faults.
    static IWindow* window = nullptr;
    static GlRenderer* renderer = nullptr;
    if (renderer == nullptr) {
        WindowDesc desc;
        desc.title = "zui-probe";
        desc.width = 64;
        desc.height = 64;
        desc.api = GfxApi::OpenGL;
        desc.hidden = true;
        std::unique_ptr<IWindow> created = create_window("sdl3");
        if (created == nullptr || !created->create(desc)) {
            rgba.clear();
            return ProbeFrame::Failed;
        }
        renderer = new GlRenderer(false);
        if (!renderer->create(*created)) {
            delete renderer;
            renderer = nullptr;
            rgba.clear();
            return ProbeFrame::Failed;
        }
        window = created.release();
    }
    for (const ProbeFontFace& face : fonts) {
        text::register_embedded_font(face.family, face.path);
    }
    if (!renderer->prepare_offscreen(width, height)) {
        rgba.clear();
        return ProbeFrame::Failed;
    }
    renderer->begin_frame();
    renderer->submit(list);
    int got_w = 0;
    int got_h = 0;
    if (!renderer->read_frame(rgba, got_w, got_h) || got_w != width || got_h != height) {
        rgba.clear();
        return ProbeFrame::Failed;
    }
    return ProbeFrame::Ok;
#endif
}

}  // namespace backends
}  // namespace zui
