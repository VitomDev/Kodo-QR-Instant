#include "app/screen_capture.h"

namespace kodo {

std::unique_ptr<ScreenSnapshot> ScreenSnapshot::Capture() {
    std::unique_ptr<ScreenSnapshot> snapshot(new ScreenSnapshot());
    // Kodo is per-monitor DPI aware, so these are physical pixels.
    snapshot->bounds_.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    snapshot->bounds_.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    snapshot->bounds_.right = snapshot->bounds_.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    snapshot->bounds_.bottom = snapshot->bounds_.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    const int width = snapshot->width();
    const int height = snapshot->height();
    if (width <= 0 || height <= 0) return nullptr;

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;  // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    HDC screen = GetDC(nullptr);
    void* bits = nullptr;
    snapshot->bitmap_ = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    bool copied = false;
    if (snapshot->bitmap_) {
        snapshot->pixels_ = static_cast<uint8_t*>(bits);
        HDC memory = CreateCompatibleDC(screen);
        HGDIOBJ previous = SelectObject(memory, snapshot->bitmap_);
        // CAPTUREBLT includes layered (translucent) windows.
        copied = BitBlt(memory, 0, 0, width, height, screen, snapshot->bounds_.left, snapshot->bounds_.top,
                        SRCCOPY | CAPTUREBLT) != FALSE;
        SelectObject(memory, previous);
        DeleteDC(memory);
    }
    ReleaseDC(nullptr, screen);
    if (!copied) return nullptr;

    // GDI leaves alpha undefined; the decoder expects opaque pixels.
    const size_t count = static_cast<size_t>(width) * height;
    for (size_t i = 0; i < count; ++i) snapshot->pixels_[i * 4 + 3] = 255;
    return snapshot;
}

ScreenSnapshot::~ScreenSnapshot() {
    if (bitmap_) {
        SecureZeroMemory(pixels_, static_cast<size_t>(width()) * height() * 4);
        DeleteObject(bitmap_);
    }
}

BgraImage ScreenSnapshot::Crop(const RECT& rect) const { return kodo::Crop(pixels_, width(), height(), rect); }

}  // namespace kodo
