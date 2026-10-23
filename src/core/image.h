#pragma once

#include <windows.h>

#include <cstdint>
#include <vector>

namespace kodo {

// Top-down 32-bit BGRA pixels, as produced by a GDI screen capture.
struct BgraImage {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;  // width * height * 4 bytes

    bool empty() const { return width <= 0 || height <= 0; }
};

// Copies `rect` (in image coordinates, clamped to the image) out of top-down
// BGRA pixels into a new image.
BgraImage Crop(const uint8_t* pixels, int width, int height, const RECT& rect);
inline BgraImage Crop(const BgraImage& image, const RECT& rect) {
    return Crop(image.pixels.data(), image.width, image.height, rect);
}

// Enlarges by an integer factor, repeating pixels. Keeps the hard edges of
// QR modules, which small on-screen codes need to be found.
BgraImage Upscale(const BgraImage& image, int factor);

// Overwrites the pixel buffer with zeros; screenshots are sensitive data.
void Wipe(std::vector<uint8_t>& pixels);

}  // namespace kodo
