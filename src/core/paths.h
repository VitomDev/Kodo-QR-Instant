#pragma once

#include <filesystem>

namespace kodo {

std::filesystem::path ExecutablePath();

// %LOCALAPPDATA%\Kodo QR Instant: the history file. Nothing else is stored
// on disk; settings live in the registry.
std::filesystem::path DataDirectory();
std::filesystem::path HistoryPath();

// The user's Pictures folder, where "Save PNG" opens first.
std::filesystem::path PicturesDirectory();

}  // namespace kodo
