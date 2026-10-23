#pragma once

#include "core/image.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace kodo {

// PNG through the Windows Imaging Component, built into Windows. Needs COM
// initialized on the calling thread. Empty on failure.
std::vector<uint8_t> EncodePng(const BgraImage& image);
bool SavePng(const BgraImage& image, const std::filesystem::path& path);

// Decodes any image format WIC knows (PNG, JPEG, BMP...).
std::optional<BgraImage> DecodeImage(const std::vector<uint8_t>& data);

}  // namespace kodo
