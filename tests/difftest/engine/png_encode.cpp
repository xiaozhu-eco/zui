#include "png_encode.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <vector>

namespace zdt {

bool encode_rgba_png(const std::uint8_t* rgba, int width, int height, std::string& out) {
    if (rgba == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    std::vector<unsigned char> bytes;
    auto write = [](void* context, void* data, int size) {
        if (data == nullptr || size <= 0) {
            return;
        }
        auto* sink = static_cast<std::vector<unsigned char>*>(context);
        const auto* begin = static_cast<const unsigned char*>(data);
        sink->insert(sink->end(), begin, begin + size);
    };
    if (stbi_write_png_to_func(write, &bytes, width, height, 4, rgba, width * 4) == 0) {
        return false;
    }
    out.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return true;
}

}  // namespace zdt
