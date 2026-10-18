#include "backends/opengl/gl_textures.h"

#include "backends/image_textures.h"

#include "image/image.h"

#include "trace/trace.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace zui {
namespace backends {

void GlTextures::create(GlContext& ctx) {
    ctx.gl_.GenTextures(1, &tex_);
    ctx.gl_.BindTexture(GL_TEXTURE_2D, tex_);
    ctx.gl_.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    ctx.gl_.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    ctx.gl_.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    ctx.gl_.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    ctx.gl_.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    int max_edge = 0;
    if (ctx.gl_.GetIntegerv != nullptr) {
        ctx.gl_.GetIntegerv(GL_MAX_TEXTURE_SIZE, &max_edge);
    }
    max_edge_ = max_edge > 0 ? max_edge : 2048;
    atlas_.set_max_edge(max_edge_);
    // Glyphs no longer sample this. ui.frag still declares sampler2D uTex, and an incomplete
    // texture fails the whole draw, so upload the empty 1024 shelf once.
    ctx.gl_.TexImage2D(GL_TEXTURE_2D, 0, GL_R8, atlas_.width(), atlas_.height(), 0, GL_RED,
                       GL_UNSIGNED_BYTE, atlas_.pixels());
}

void GlTextures::destroy(GlContext& ctx) {
    if (tex_) {
        ctx.gl_.DeleteTextures(1, &tex_);
        tex_ = 0;
    }
    for (auto& entry : images_) {
        if (entry.second.tex != 0) {
            ctx.gl_.DeleteTextures(1, &entry.second.tex);
        }
    }
    images_.clear();
}

const GlImageTexture* GlTextures::image(GlContext& ctx, std::string_view path, float draw_w,
                                        float draw_h) {
    if (path.empty()) {
        return nullptr;
    }
    const std::string key_path(path);
    const image::Bitmap* bmp =
        draw_w > 1.f && draw_h > 1.f
            ? image::load_fitted(key_path, std::max(1, static_cast<int>(std::ceil(draw_w))),
                                 std::max(1, static_cast<int>(std::ceil(draw_h))))
            : image::load(key_path);
    if (bmp == nullptr || bmp->rgba == nullptr || bmp->width <= 0 || bmp->height <= 0) {
        return nullptr;
    }
    const ImageLevel level = image_level(bmp->width, bmp->height, draw_w, draw_h, max_edge_);
    std::string key = key_path;
    key.push_back('#');
    key += std::to_string(level.level);
    if (level.point) {
        key.push_back('p');
        key += std::to_string(level.width);
        key.push_back('x');
        key += std::to_string(level.height);
    }
    auto found = images_.find(key);
    if (found != images_.end()) {
        return &found->second;
    }
    zui::trace::Zone zone("gpu.upload", zui::trace::Category::Gpu);
    std::vector<std::uint8_t> reduced;
    const std::uint8_t* pixels = bmp->rgba;
    if (level.width != bmp->width || level.height != bmp->height) {
        reduced = image_level_pixels(bmp->rgba, bmp->width, bmp->height, level.width, level.height,
                                     level.point);
        if (reduced.empty()) {
            return nullptr;
        }
        pixels = reduced.data();
    }
    GlImageTexture texture;
    texture.width = level.width;
    texture.height = level.height;
    ctx.gl_.GenTextures(1, &texture.tex);
    if (texture.tex == 0) {
        return nullptr;
    }
    ctx.gl_.BindTexture(GL_TEXTURE_2D, texture.tex);
    const int filter = level.point ? GL_NEAREST : GL_LINEAR;
    ctx.gl_.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    ctx.gl_.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    ctx.gl_.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    ctx.gl_.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    ctx.gl_.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, level.width, level.height, 0, GL_RGBA,
                       GL_UNSIGNED_BYTE, pixels);
    auto inserted = images_.emplace(std::move(key), texture);
    return &inserted.first->second;
}

}  // namespace backends
}  // namespace zui
