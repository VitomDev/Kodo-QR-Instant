#pragma once

#include <string>
#include <string_view>

namespace kodo {

std::string ToUtf8(std::wstring_view text);
std::wstring FromUtf8(std::string_view text);

// "\n", "\r" or "\r\n" to "\r\n", the line break edit controls and the
// clipboard expect.
std::wstring ToWindowsLineBreaks(std::wstring_view text);
// Any line break to "\n", the form QR payloads use.
std::wstring ToUnixLineBreaks(std::wstring_view text);

bool HasVisibleText(std::wstring_view text);
std::wstring Trim(std::wstring_view text);

bool StartsWithNoCase(std::wstring_view text, std::wstring_view prefix);

// Short single-line preview for menus and lists, ending in an ellipsis when cut.
std::wstring OneLinePreview(std::wstring_view text, size_t maxLength);

}  // namespace kodo
