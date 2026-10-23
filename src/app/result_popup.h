#pragma once

#include "app/theme.h"

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

namespace kodo {

// A button of the result popup. `run` returns false when it could not do its
// job (e.g. the clipboard was busy).
struct PopupAction {
    const wchar_t* icon = nullptr;
    std::wstring label;
    std::function<bool()> run;
    bool closes = false;      // Hide the popup afterwards (e.g. Open link)
    bool showsCopied = false; // Briefly read "Copied" on success
};

struct PopupContent {
    const wchar_t* icon = nullptr;  // Kind of content
    std::wstring title;             // "Wi-Fi network"
    std::wstring body;              // Readable content
    std::wstring note;              // Small line under the body, e.g. "Copied to clipboard"
    std::vector<PopupAction> actions;  // The first one is the primary action (Enter)
};

// The floating card that shows what a QR code contains, next to the
// selection, in the style of Windows 11 flyouts. When the selection held
// several codes, Next shows the following one.
class ResultPopup {
public:
    std::function<void()> onNext;

    bool Create(HINSTANCE instance);
    void Destroy();

    // `anchor` is the selection in screen coordinates; `position` is "2 of 3"
    // or empty for a single code.
    void Show(const RECT& anchor, PopupContent content, const std::wstring& position);
    void Hide();

    HWND window() const { return window_; }
    bool IsVisible() const { return window_ && IsWindowVisible(window_); }

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void RefreshStyle();
    void Layout();
    void Paint();
    void Draw(const DRAWITEMSTRUCT& item);
    void Run(size_t index);
    int Scale(int value) const;

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    Theme theme_{};
    HBRUSH backgroundBrush_ = nullptr;
    HFONT textFont_ = nullptr;
    HFONT titleFont_ = nullptr;
    HFONT smallFont_ = nullptr;
    HFONT buttonFont_ = nullptr;
    HFONT iconFont_ = nullptr;
    HFONT titleIconFont_ = nullptr;
    PopupContent content_;
    std::wstring position_;
    std::vector<HWND> buttons_;
    RECT anchor_{};
    int copiedIndex_ = -1;
    int noteY_ = 0;
};

}  // namespace kodo
