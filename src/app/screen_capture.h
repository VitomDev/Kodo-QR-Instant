#pragma once

#include "core/image.h"

#include <windows.h>

#include <memory>

namespace kodo {

// A frozen copy of the whole virtual screen (all monitors), taken before the
// selection overlay appears so the overlay never ends up in the capture.
class ScreenSnapshot {
public:
    static std::unique_ptr<ScreenSnapshot> Capture();
    ~ScreenSnapshot();

    ScreenSnapshot(const ScreenSnapshot&) = delete;
    ScreenSnapshot& operator=(const ScreenSnapshot&) = delete;

    // Virtual screen bounds in physical pixels; may start at negative values.
    const RECT& bounds() const { return bounds_; }
    int width() const { return bounds_.right - bounds_.left; }
    int height() const { return bounds_.bottom - bounds_.top; }
    HBITMAP bitmap() const { return bitmap_; }
    const uint8_t* pixels() const { return pixels_; }

    // Copies a region given in snapshot coordinates.
    BgraImage Crop(const RECT& rect) const;

private:
    ScreenSnapshot() = default;

    RECT bounds_{};
    HBITMAP bitmap_ = nullptr;
    uint8_t* pixels_ = nullptr;
};

}  // namespace kodo
