#include "core/clipboard.h"

#include "core/png.h"

#include <cstring>

namespace kodo {

namespace {

bool OpenClipboardWithRetry(HWND owner) {
    for (int attempt = 0; attempt < 10; ++attempt) {
        if (OpenClipboard(owner)) return true;
        Sleep(25);
    }
    return false;
}

HGLOBAL CopyToGlobal(const void* data, size_t size) {
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!memory) return nullptr;
    std::memcpy(GlobalLock(memory), data, size);
    GlobalUnlock(memory);
    return memory;
}

// A bottom-up 32-bit DIB, the layout every application reads.
HGLOBAL CreateDib(const BgraImage& image) {
    const size_t rowBytes = static_cast<size_t>(image.width) * 4;
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + rowBytes * image.height);
    if (!memory) return nullptr;
    auto* header = static_cast<BITMAPINFOHEADER*>(GlobalLock(memory));
    *header = {sizeof(BITMAPINFOHEADER)};
    header->biWidth = image.width;
    header->biHeight = image.height;
    header->biPlanes = 1;
    header->biBitCount = 32;
    header->biCompression = BI_RGB;
    auto* bits = reinterpret_cast<uint8_t*>(header + 1);
    for (int y = 0; y < image.height; ++y) {
        std::memcpy(bits + rowBytes * (image.height - 1 - y), &image.pixels[rowBytes * y], rowBytes);
    }
    GlobalUnlock(memory);
    return memory;
}

}  // namespace

bool SetClipboardText(HWND owner, std::wstring_view text) {
    std::wstring terminated(text);
    HGLOBAL memory = CopyToGlobal(terminated.c_str(), (terminated.size() + 1) * sizeof(wchar_t));
    if (!memory) return false;

    if (!OpenClipboardWithRetry(owner)) {
        GlobalFree(memory);
        return false;
    }
    EmptyClipboard();
    const bool stored = SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
    CloseClipboard();
    // On success the clipboard owns the memory.
    if (!stored) GlobalFree(memory);
    return stored;
}

std::optional<std::wstring> GetClipboardText(HWND owner) {
    if (!OpenClipboardWithRetry(owner)) return std::nullopt;
    std::optional<std::wstring> text;
    if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
        if (const auto* buffer = static_cast<const wchar_t*>(GlobalLock(data))) {
            text = buffer;
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    return text;
}

bool SetClipboardImage(HWND owner, const BgraImage& image) {
    if (image.empty()) return false;
    HGLOBAL dib = CreateDib(image);
    if (!dib) return false;
    const std::vector<uint8_t> png = EncodePng(image);
    HGLOBAL pngMemory = png.empty() ? nullptr : CopyToGlobal(png.data(), png.size());

    if (!OpenClipboardWithRetry(owner)) {
        GlobalFree(dib);
        if (pngMemory) GlobalFree(pngMemory);
        return false;
    }
    EmptyClipboard();
    const bool stored = SetClipboardData(CF_DIB, dib) != nullptr;
    if (!stored) GlobalFree(dib);
    if (pngMemory && !SetClipboardData(RegisterClipboardFormatW(L"PNG"), pngMemory)) GlobalFree(pngMemory);
    CloseClipboard();
    return stored;
}

}  // namespace kodo
