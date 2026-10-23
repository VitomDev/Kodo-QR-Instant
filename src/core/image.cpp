#include "core/image.h"

#include <algorithm>

namespace kodo {

BgraImage Crop(const uint8_t* pixels, int width, int height, const RECT& rect) {
    const int left = std::clamp<int>(rect.left, 0, width);
    const int top = std::clamp<int>(rect.top, 0, height);
    const int right = std::clamp<int>(rect.right, left, width);
    const int bottom = std::clamp<int>(rect.bottom, top, height);

    BgraImage result;
    result.width = right - left;
    result.height = bottom - top;
    result.pixels.resize(static_cast<size_t>(result.width) * result.height * 4);
    for (int y = 0; y < result.height; ++y) {
        const uint8_t* source = pixels + (static_cast<size_t>(top + y) * width + left) * 4;
        std::copy_n(source, static_cast<size_t>(result.width) * 4,
                    &result.pixels[static_cast<size_t>(y) * result.width * 4]);
    }
    return result;
}

BgraImage Upscale(const BgraImage& image, int factor) {
    if (factor <= 1 || image.empty()) return image;

    BgraImage result;
    result.width = image.width * factor;
    result.height = image.height * factor;
    result.pixels.resize(static_cast<size_t>(result.width) * result.height * 4);
    for (int y = 0; y < result.height; ++y) {
        const auto* source = reinterpret_cast<const uint32_t*>(&image.pixels[static_cast<size_t>(y / factor) * image.width * 4]);
        auto* target = reinterpret_cast<uint32_t*>(&result.pixels[static_cast<size_t>(y) * result.width * 4]);
        for (int x = 0; x < result.width; ++x) target[x] = source[x / factor];
    }
    return result;
}

void Wipe(std::vector<uint8_t>& pixels) {
    SecureZeroMemory(pixels.data(), pixels.size());
    pixels.clear();
    pixels.shrink_to_fit();
}

}  // namespace kodo
