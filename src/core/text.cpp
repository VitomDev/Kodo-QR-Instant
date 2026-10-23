#include "core/text.h"

#include <windows.h>

#include <cwctype>

namespace kodo {

namespace {

bool IsSpace(wchar_t c) { return std::iswspace(c) || c == 0x00A0 || c == 0x3000; }

std::wstring ReplaceLineBreaks(std::wstring_view text, std::wstring_view lineBreak) {
    std::wstring result;
    result.reserve(text.size() + text.size() / 16);
    for (size_t i = 0; i < text.size(); ++i) {
        const wchar_t c = text[i];
        if (c == L'\r' || c == L'\n') {
            if (c == L'\r' && i + 1 < text.size() && text[i + 1] == L'\n') ++i;
            result += lineBreak;
        } else {
            result += c;
        }
    }
    return result;
}

}  // namespace

std::string ToUtf8(std::wstring_view text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring FromUtf8(std::string_view text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
    return result;
}

std::wstring ToWindowsLineBreaks(std::wstring_view text) { return ReplaceLineBreaks(text, L"\r\n"); }

std::wstring ToUnixLineBreaks(std::wstring_view text) { return ReplaceLineBreaks(text, L"\n"); }

bool HasVisibleText(std::wstring_view text) {
    for (wchar_t c : text) {
        if (!IsSpace(c)) return true;
    }
    return false;
}

std::wstring Trim(std::wstring_view text) {
    size_t start = 0, end = text.size();
    while (start < end && IsSpace(text[start])) ++start;
    while (end > start && IsSpace(text[end - 1])) --end;
    return std::wstring(text.substr(start, end - start));
}

bool StartsWithNoCase(std::wstring_view text, std::wstring_view prefix) {
    if (text.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (std::towlower(text[i]) != std::towlower(prefix[i])) return false;
    }
    return true;
}

std::wstring OneLinePreview(std::wstring_view text, size_t maxLength) {
    std::wstring result;
    bool space = false;
    for (wchar_t c : text) {
        if (IsSpace(c)) {
            space = !result.empty();
            continue;
        }
        if (space) result += L' ';
        space = false;
        result += c;
        if (result.size() > maxLength) {
            result.resize(maxLength);
            return result + L"…";
        }
    }
    return result;
}

}  // namespace kodo
