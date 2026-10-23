#pragma once

#include "core/image.h"

#include <windows.h>

#include <optional>
#include <string>
#include <string_view>

namespace kodo {

// Puts Unicode text on the clipboard. Another application may hold the
// clipboard open for a moment, so opening is retried briefly.
bool SetClipboardText(HWND owner, std::wstring_view text);

std::optional<std::wstring> GetClipboardText(HWND owner);

// Puts an image on the clipboard both as a bitmap (CF_DIB) and as PNG, which
// keeps it sharp in apps that understand it (browsers, Office, chat apps).
bool SetClipboardImage(HWND owner, const BgraImage& image);

}  // namespace kodo
