#pragma once

#include "app/theme.h"
#include "core/history.h"

#include <windows.h>
#include <commctrl.h>

#include <functional>
#include <string>
#include <vector>

namespace kodo {

// Recently read and created codes in a list: show one again as a QR code,
// copy its text, delete it or clear everything.
class HistoryWindow {
public:
    std::function<void(const std::wstring& text)> onShowCode;

    bool Create(HINSTANCE instance, History* history);
    void Destroy();

    // `enabled` is the Keep history setting, which only changes the hint
    // shown when the list is empty.
    void Show(bool enabled);
    void Refresh();
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
    void DrawRow(HDC dc, int index);
    static std::wstring Summary(const HistoryEntry& entry);
    int Selected() const;
    void UpdateButtons();
    int Scale(int value) const;

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    HWND list_ = nullptr;
    History* history_ = nullptr;
    std::vector<HistoryEntry> entries_;
    bool enabled_ = true;
    Theme theme_{};
    HBRUSH backgroundBrush_ = nullptr;
    HFONT titleFont_ = nullptr;
    HFONT textFont_ = nullptr;
    HFONT iconFont_ = nullptr;
    HFONT smallFont_ = nullptr;
    HFONT kindFont_ = nullptr;
    HIMAGELIST rowHeight_ = nullptr;
    RECT titleRect_{};
    int copiedIndex_ = -1;
};

}  // namespace kodo
