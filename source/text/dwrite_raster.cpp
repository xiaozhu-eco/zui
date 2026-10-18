#include "text/dwrite_raster.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <dwrite.h>
#include <dwrite_3.h>

#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "ole32.lib")

namespace zui {
namespace text {
namespace {

struct FaceKey {
    std::wstring path;
    std::vector<std::pair<std::uint32_t, float>> axes;
    bool synthetic_bold = false;

    bool operator==(const FaceKey& other) const {
        return path == other.path && axes == other.axes &&
               synthetic_bold == other.synthetic_bold;
    }
};

struct FaceKeyHash {
    std::size_t operator()(const FaceKey& key) const {
        std::size_t hash = std::hash<std::wstring>{}(key.path);
        hash ^= std::hash<bool>{}(key.synthetic_bold) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
        for (const auto& axis : key.axes) {
            hash ^= std::hash<std::uint32_t>{}(axis.first) + 0x9e3779b9u + (hash << 6) +
                    (hash >> 2);
            hash ^= std::hash<float>{}(axis.second) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
        }
        return hash;
    }
};

struct Cache {
    IDWriteFactory* factory = nullptr;
    std::unordered_map<FaceKey, IDWriteFontFace*, FaceKeyHash> faces;
    std::mutex mu;
};

Cache& cache() {
    static Cache stored;
    return stored;
}

std::wstring widen(const std::string& path) {
    if (path.empty()) {
        return {};
    }
    const int n = MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, nullptr, 0);
    if (n <= 0) {
        return {};
    }
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, out.data(), n);
    if (!out.empty() && out.back() == L'\0') {
        out.pop_back();
    }
    return out;
}

// FreeType tags are big-endian ('w' in the high byte). DirectWrite's OpenType tag is swapped.
std::uint32_t dw_tag(std::uint32_t ft_tag) {
    return ((ft_tag & 0xffu) << 24) | ((ft_tag & 0xff00u) << 8) | ((ft_tag & 0xff0000u) >> 8) |
           ((ft_tag >> 24) & 0xffu);
}

IDWriteFontFace* face_for(const std::wstring& path, const std::uint32_t* axis_tags,
                          const float* axis_values, int axis_count, bool synthetic_bold) {
    Cache& store = cache();
    if (store.factory == nullptr) {
        const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        (void)co;
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                       reinterpret_cast<IUnknown**>(&store.factory)))) {
            return nullptr;
        }
    }
    FaceKey key;
    key.path = path;
    key.synthetic_bold = synthetic_bold;
    key.axes.reserve(static_cast<std::size_t>(axis_count));
    for (int i = 0; i < axis_count; ++i) {
        key.axes.emplace_back(dw_tag(axis_tags[i]), axis_values[i]);
    }
    const auto found = store.faces.find(key);
    if (found != store.faces.end()) {
        return found->second;
    }

    IDWriteFontFile* file = nullptr;
    if (FAILED(store.factory->CreateFontFileReference(path.c_str(), nullptr, &file))) {
        return nullptr;
    }
    BOOL supported = FALSE;
    DWRITE_FONT_FILE_TYPE file_type = DWRITE_FONT_FILE_TYPE_UNKNOWN;
    DWRITE_FONT_FACE_TYPE face_type = DWRITE_FONT_FACE_TYPE_UNKNOWN;
    UINT32 face_count = 0;
    if (FAILED(file->Analyze(&supported, &file_type, &face_type, &face_count)) || !supported) {
        file->Release();
        return nullptr;
    }
    IDWriteFontFace* face = nullptr;
    const DWRITE_FONT_SIMULATIONS simulations =
        synthetic_bold ? DWRITE_FONT_SIMULATIONS_BOLD : DWRITE_FONT_SIMULATIONS_NONE;
    if (axis_count > 0) {
        IDWriteFactory6* factory6 = nullptr;
        if (SUCCEEDED(store.factory->QueryInterface(__uuidof(IDWriteFactory6),
                                                    reinterpret_cast<void**>(&factory6)))) {
            IDWriteFontResource* resource = nullptr;
            if (SUCCEEDED(factory6->CreateFontResource(file, 0, &resource))) {
                std::vector<DWRITE_FONT_AXIS_VALUE> axes(static_cast<std::size_t>(axis_count));
                for (int i = 0; i < axis_count; ++i) {
                    axes[static_cast<std::size_t>(i)].axisTag = static_cast<DWRITE_FONT_AXIS_TAG>(
                        key.axes[static_cast<std::size_t>(i)].first);
                    axes[static_cast<std::size_t>(i)].value =
                        key.axes[static_cast<std::size_t>(i)].second;
                }
                IDWriteFontFace5* instanced = nullptr;
                if (SUCCEEDED(resource->CreateFontFace(simulations, axes.data(),
                                                       static_cast<UINT32>(axis_count),
                                                       &instanced))) {
                    face = instanced;
                }
                resource->Release();
            }
            factory6->Release();
        }
    }
    if (face == nullptr &&
        FAILED(store.factory->CreateFontFace(face_type, 1, &file, 0, simulations,
                                             &face))) {
        file->Release();
        return nullptr;
    }
    file->Release();
    store.faces.emplace(std::move(key), face);
    return face;
}

}  // namespace

bool dwrite_raster_glyph(const std::string& path, std::uint32_t glyph, float px, float origin_x,
                         float origin_y, const std::uint32_t* axis_tags, const float* axis_values,
                         int axis_count, DWriteMask& out, bool synthetic_bold) {
    out = {};
    if (path.empty() || px <= 0.f || glyph == 0 || glyph > 0xffffu) {
        return false;
    }
    const std::wstring wide = widen(path);
    if (wide.empty()) {
        return false;
    }
    Cache& store = cache();
    std::lock_guard<std::mutex> lock(store.mu);
    IDWriteFontFace* face = face_for(wide, axis_tags, axis_values, axis_count, synthetic_bold);
    if (face == nullptr || store.factory == nullptr) {
        return false;
    }
    const UINT16 index = static_cast<UINT16>(glyph);
    const FLOAT advance = 0.f;
    DWRITE_GLYPH_OFFSET offset{};
    DWRITE_GLYPH_RUN run{};
    run.fontFace = face;
    run.fontEmSize = px;
    run.glyphCount = 1;
    run.glyphIndices = &index;
    run.glyphAdvances = &advance;
    run.glyphOffsets = &offset;
    IDWriteGlyphRunAnalysis* analysis = nullptr;
    // Natural symmetric ClearType, averaged to one byte. Factory2 grayscale (with and without
    // grid fit) left more of the browser's stems uncovered than this mask.
    const HRESULT hr = store.factory->CreateGlyphRunAnalysis(
        &run, 1.0f, nullptr, DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC, DWRITE_MEASURING_MODE_NATURAL,
        origin_x, origin_y, &analysis);
    if (FAILED(hr) || analysis == nullptr) {
        return false;
    }
    RECT bounds{};
    analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds);
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    if (width <= 0 || height <= 0) {
        analysis->Release();
        return false;
    }
    std::vector<BYTE> raw(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3u);
    if (FAILED(analysis->CreateAlphaTexture(DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds, raw.data(),
                                            static_cast<UINT32>(raw.size())))) {
        analysis->Release();
        return false;
    }
    analysis->Release();
    out.ok = true;
    out.left = bounds.left;
    out.top = -bounds.top;
    out.width = width;
    out.height = height;
    out.alpha.assign(static_cast<std::size_t>(width * height), 0);
    for (int i = 0; i < width * height; ++i) {
        const BYTE* pxel = raw.data() + static_cast<std::size_t>(i) * 3;
        out.alpha[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(
            (static_cast<int>(pxel[0]) + pxel[1] + pxel[2]) / 3);
    }
    return true;
}

}  // namespace text
}  // namespace zui

#else

namespace zui {
namespace text {

bool dwrite_raster_glyph(const std::string&, std::uint32_t, float, float, float,
                         const std::uint32_t*, const float*, int, DWriteMask& out,
                         bool synthetic_bold) {
    out = {};
    return false;
}

}  // namespace text
}  // namespace zui

#endif
