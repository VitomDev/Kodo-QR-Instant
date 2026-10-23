#pragma once

#include "app/theme.h"
#include "core/qr.h"

#include <windows.h>

#include <functional>
#include <optional>
#include <string>

namespace kodo {

// The Create QR window: a template (Text, Wi-Fi, Contact or Email) on the
// left, the live QR code on the right, with Copy image and Save PNG.
class GeneratorWindow {
public:
    enum class Mode { Text, Wifi, Contact, Email };

    // Called with the payload when the user copies or saves a code.
    std::function<void(const std::wstring& payload)> onCreated;

    bool Create(HINSTANCE instance);
    void Destroy();

    // Opens (or brings back) the window in `mode`; for Text, `text` replaces
    // the current text.
    void Show(Mode mode, const std::optional<std::wstring>& text = std::nullopt);
    void Hide();

    HWND window() const { return window_; }
    bool IsVisible() const { return window_ && IsWindowVisible(window_); }

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void CreateControls();
    void RefreshStyle();
    void Layout();
    void Paint();
    void Draw(const DRAWITEMSTRUCT& item);
    void SetMode(Mode mode);
    void ShowSecurityMenu();
    std::wstring Payload() const;
    void UpdateCode();
    void CopyImage();
    void SavePng();
    void SetStatus(const std::wstring& status, bool error);
    int Scale(int value) const;

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    Theme theme_{};
    HBRUSH backgroundBrush_ = nullptr;
    HBRUSH fieldBrush_ = nullptr;
    HFONT titleFont_ = nullptr;
    HFONT textFont_ = nullptr;
    HFONT smallFont_ = nullptr;
    HFONT iconFont_ = nullptr;

    Mode mode_ = Mode::Text;
    std::wstring security_ = L"WPA";
    bool hidden_ = false;
    std::optional<QrMatrix> code_;
    std::wstring status_;
    bool statusIsError_ = false;
    bool copied_ = false;

    struct Label {
        RECT rect;
        std::wstring text;
    };
    std::vector<Label> labels_;
    std::vector<RECT> fieldFrames_;
    RECT preview_{};
    RECT statusRect_{};
};

}  // namespace kodo
