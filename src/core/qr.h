#pragma once

#include "core/image.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kodo {

// Finds and decodes every QR code (including Micro QR and rMQR) in an image.
// Codes on dark backgrounds, rotated or tiny on screen are handled too.
// Ordered from largest to smallest, the one the user most likely meant first.
std::vector<std::wstring> DecodeQrCodes(const BgraImage& image);

// The modules of an encoded QR code, without its quiet zone.
struct QrMatrix {
    int size = 0;
    std::vector<uint8_t> dark;  // size * size, 1 for dark modules

    bool empty() const { return size <= 0; }
    bool IsDark(int x, int y) const { return dark[static_cast<size_t>(y) * size + x] != 0; }
};

// Error correction M (15%), the usual choice. Text outside Latin-1 is stored
// as UTF-8 with its ECI marker so phones show it correctly. Returns nothing
// when the text is empty or too long for any QR version (about 2,300 Latin
// characters, fewer with accents or other alphabets).
std::optional<QrMatrix> EncodeQrCode(const std::wstring& text);

// Black modules on white, `moduleSize` pixels each, with the standard
// four-module quiet zone.
BgraImage RenderQrCode(const QrMatrix& matrix, int moduleSize);

// The module size that renders the code close to `targetPixels` wide (at least 1).
int ModuleSizeFor(const QrMatrix& matrix, int targetPixels);

inline constexpr int kQuietZoneModules = 4;

}  // namespace kodo
