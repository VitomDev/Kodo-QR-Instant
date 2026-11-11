#pragma once

#include <windows.h>

#include <cstdint>
#include <vector>

namespace kodo {

// A short "scanned" animation over a screen area: a light sweeping down,
// a glowing frame and sparks flying off its edge. Drawn in a click-through
// layered window, so it never gets in the way.
class ScanEffect {
public:
    bool Create(HINSTANCE instance);
    void Destroy();
    void Play(const RECT& area);

private:
    struct Spark {
        float x, y, vx, vy, size;
    };

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    void Frame();
    void Render(float t);

    HWND window_ = nullptr;
    RECT area_{};     // In window coordinates
    int margin_ = 0;  // Room around the area for the glow and sparks
    int width_ = 0;
    int height_ = 0;
    float scale_ = 1;
    ULONGLONG start_ = 0;
    COLORREF accent_ = 0;
    std::vector<Spark> sparks_;
    std::vector<uint32_t> pixels_;
};

}  // namespace kodo
