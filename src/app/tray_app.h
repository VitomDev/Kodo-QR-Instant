#pragma once

#include "app/generator_window.h"
#include "app/history_window.h"
#include "app/result_popup.h"
#include "app/toast.h"
#include "core/history.h"
#include "core/settings.h"

#include <windows.h>
#include <shellapi.h>

#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

namespace kodo {

inline constexpr wchar_t kTrayWindowClass[] = L"KodoQRInstantTray";
inline constexpr UINT WM_APP_ALREADY_RUNNING = WM_APP + 12;

// The background application: tray icon, global shortcuts, reading codes
// from a screen region and the Create QR and History windows.
class TrayApp {
public:
    TrayApp();
    bool Initialize(HINSTANCE instance, bool startedAtSignIn);
    // Lets Kodo's windows handle Tab, Enter and Escape. Returns true if consumed.
    bool PreTranslate(MSG& message);

private:
    enum class BalloonAction { None, OpenSettings };

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void AddTrayIcon();
    void ShowTrayMenu(POINT point);
    void Notify(const std::wstring& message, BalloonAction action = BalloonAction::None);

    std::wstring RegisterHotkeys(const Settings& settings);
    void UnregisterHotkeys();
    bool ApplySettings(const Settings& settings, bool autostart, std::wstring& error);
    void OpenSettings();
    void ToggleAutostart();
    void Exit();

    void ReadCode();
    void FinishRead(std::vector<std::wstring> texts);
    void ShowResult(size_t index);
    void CreateFromClipboard();
    void OpenHistory();
    void Remember(HistoryEntry::Origin origin, const std::wstring& text);

    // Runs `work` on a short-lived thread and `done` back on the UI thread.
    void RunTask(std::function<void()> work, std::function<void()> done);

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    UINT taskbarCreatedMessage_ = 0;
    NOTIFYICONDATAW icon_{};
    Settings settings_;
    BalloonAction balloonAction_ = BalloonAction::None;

    bool captureActive_ = false;
    std::thread task_;
    std::function<void()> taskDone_;
    std::atomic<bool> exiting_{false};

    History history_;
    ResultPopup popup_;
    GeneratorWindow generator_;
    HistoryWindow historyWindow_;
    Toast toast_;
    RECT selectionOnScreen_{};
    std::vector<std::wstring> results_;  // Codes of the last read, largest first
    size_t shownResult_ = 0;
};

}  // namespace kodo
