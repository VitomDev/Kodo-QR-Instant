#pragma once

#include <windows.h>

#include <string>

namespace kodo {

// A small pill under the selection ("Copied to clipboard") that fades out on
// its own. It never takes focus and lets clicks pass through.
class Toast {
public:
    enum class Kind { Success, Info };

    bool Create(HINSTANCE instance);
    void Destroy();
    void Show(const RECT& anchor, const std::wstring& text, Kind kind);

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    void Paint();

    HWND window_ = nullptr;
    std::wstring text_;
    Kind kind_ = Kind::Success;
    int alpha_ = 255;
};

}  // namespace kodo
