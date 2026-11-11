#pragma once

#include "app/theme.h"
#include "core/image.h"

#include <windows.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace kodo {

// Read with camera: a window showing the chosen camera that closes as soon
// as a QR code is in view, handing its contents to `onFound`.
class CameraWindow {
public:
    // The codes found (largest first) and where the window was, to place the result next to it.
    std::function<void(std::vector<std::wstring> texts, RECT window)> onFound;
    // The user picked another camera or flipped the mirror; worth saving.
    std::function<void(const std::wstring& camera, bool mirror)> onOptionsChanged;

    // `camera` is a device id from a previous session; empty or missing means the default camera.
    void SetOptions(const std::wstring& camera, bool mirror);

    bool Create(HINSTANCE instance);
    void Destroy();
    void Show();
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
    void ShowCameraMenu();
    std::wstring CameraName() const;
    void Start();
    void Stop();
    void Capture();
    void SetStatus(const std::wstring& status, bool error);
    int Scale(int value) const;

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    Theme theme_{};
    HBRUSH backgroundBrush_ = nullptr;
    HFONT titleFont_ = nullptr;
    HFONT textFont_ = nullptr;
    HFONT iconFont_ = nullptr;
    RECT titleRect_{};
    RECT video_{};
    RECT statusRect_{};
    std::wstring status_;
    bool statusIsError_ = false;
    bool laidOut_ = false;

    struct Device {
        std::wstring name;
        std::wstring id;  // Symbolic link, stable across sessions
    };
    std::vector<Device> devices_;
    std::wstring camera_;
    bool mirror_ = true;

    std::thread worker_;
    std::atomic<bool> stop_{false};
    std::mutex frameLock_;
    BgraImage frame_;  // Latest camera frame, guarded by frameLock_
    std::vector<std::wstring> found_;
    std::vector<std::wstring> pending_;  // Found codes, handed over once the flash ends
    bool flash_ = false;
};

}  // namespace kodo
