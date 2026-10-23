#include "app/tray_app.h"

#include "app/resource.h"
#include "app/screen_capture.h"
#include "app/selection_overlay.h"
#include "app/settings_dialog.h"
#include "app/theme.h"
#include "core/clipboard.h"
#include "core/geometry.h"
#include "core/paths.h"
#include "core/payload.h"
#include "core/qr.h"
#include "core/text.h"

#include <commctrl.h>
#include <windowsx.h>

namespace kodo {

namespace {

constexpr UINT WM_APP_TRAY = WM_APP + 10;
constexpr UINT WM_APP_TASK_DONE = WM_APP + 11;
constexpr UINT WM_APP_OPEN_SETTINGS = WM_APP + 14;

constexpr int kReadHotkeyId = 1;
constexpr int kCreateHotkeyId = 2;

enum MenuId { kMenuRead = 1, kMenuCreateClipboard, kMenuCreate, kMenuHistory, kMenuSettings, kMenuAutostart, kMenuExit };

// Gives memory used by a capture back to the system once Kodo is idle again.
void TrimWorkingSet() { SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1)); }

const wchar_t* KindIcon(PayloadKind kind) {
    switch (kind) {
        case PayloadKind::Link: return icons::kLink;
        case PayloadKind::Wifi: return icons::kWifi;
        case PayloadKind::Email: return icons::kMail;
        case PayloadKind::Phone: return icons::kPhone;
        case PayloadKind::Sms: return icons::kMessage;
        case PayloadKind::Contact: return icons::kContact;
        case PayloadKind::Location: return icons::kLocation;
        case PayloadKind::Text: break;
    }
    return icons::kText;
}

bool OpenWithShell(const std::wstring& target) {
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}

}  // namespace

TrayApp::TrayApp() : history_(HistoryPath()) {}

bool TrayApp::Initialize(HINSTANCE instance, bool startedAtSignIn) {
    instance_ = instance;
    settings_ = Settings::Load();

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = kTrayWindowClass;
    if (!RegisterClassExW(&wc)) return false;
    // A hidden top-level window rather than a message-only one: it must
    // receive the TaskbarCreated broadcast, and the installer finds it by class.
    window_ = CreateWindowExW(0, kTrayWindowClass, L"Kodo QR Instant", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, this);
    if (!window_) return false;

    taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
    AddTrayIcon();
    EnableDarkMenus();
    if (!popup_.Create(instance) || !toast_.Create(instance) || !generator_.Create(instance) ||
        !historyWindow_.Create(instance, &history_)) {
        return false;
    }
    popup_.onNext = [this] {
        if (!results_.empty()) ShowResult((shownResult_ + 1) % results_.size());
    };
    generator_.onCreated = [this](const std::wstring& payload) { Remember(HistoryEntry::Origin::Created, payload); };
    historyWindow_.onShowCode = [this](const std::wstring& text) { generator_.Show(GeneratorWindow::Mode::Text, text); };

    const std::wstring conflicts = RegisterHotkeys(settings_);
    if (!conflicts.empty()) {
        Notify(conflicts + L" Choose another shortcut in Settings.", BalloonAction::OpenSettings);
    } else if (!startedAtSignIn) {
        Notify(L"Kodo is running in the notification area. Press " + ToString(settings_.readHotkey) +
               L" to read a QR code or " + ToString(settings_.createHotkey) + L" to create one from the clipboard.");
    }
    // Very first launch: show Settings once the message loop runs.
    if (ConsumeFirstRun()) PostMessageW(window_, WM_APP_OPEN_SETTINGS, 0, 0);
    TrimWorkingSet();
    return true;
}

bool TrayApp::PreTranslate(MSG& message) {
    for (HWND window : {popup_.window(), generator_.window(), historyWindow_.window()}) {
        if (window && IsWindowVisible(window) && (message.hwnd == window || IsChild(window, message.hwnd))) {
            return IsDialogMessageW(window, &message) != FALSE;
        }
    }
    return false;
}

// --- Tray icon ---------------------------------------------------------------

void TrayApp::AddTrayIcon() {
    icon_ = {sizeof(icon_)};
    icon_.hWnd = window_;
    icon_.uID = 1;
    icon_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    icon_.uCallbackMessage = WM_APP_TRAY;
    LoadIconMetric(instance_, MAKEINTRESOURCEW(IDI_KODO), LIM_SMALL, &icon_.hIcon);
    wcscpy_s(icon_.szTip, L"Kodo QR Instant");
    Shell_NotifyIconW(NIM_ADD, &icon_);
    icon_.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &icon_);
}

void TrayApp::ShowTrayMenu(POINT point) {
    HMENU menu = CreatePopupMenu();
    const std::wstring read = L"Read QR code\t" + ToString(settings_.readHotkey);
    const std::wstring fromClipboard = L"Create QR from clipboard\t" + ToString(settings_.createHotkey);
    AppendMenuW(menu, MF_STRING, kMenuRead, read.c_str());
    AppendMenuW(menu, MF_STRING, kMenuCreateClipboard, fromClipboard.c_str());
    AppendMenuW(menu, MF_STRING, kMenuCreate, L"Create QR code…");
    AppendMenuW(menu, MF_STRING, kMenuHistory, L"History");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuSettings, L"Settings");
    AppendMenuW(menu, MF_STRING | (IsAutostartEnabled() ? MF_CHECKED : 0), kMenuAutostart, L"Start with Windows");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuExit, L"Exit Kodo");
    SetMenuDefaultItem(menu, kMenuRead, FALSE);

    // Required so the menu closes when clicking elsewhere.
    SetForegroundWindow(window_);
    const UINT command = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, point.x, point.y, window_, nullptr);
    DestroyMenu(menu);
    switch (command) {
        case kMenuRead: ReadCode(); break;
        case kMenuCreateClipboard: CreateFromClipboard(); break;
        case kMenuCreate: generator_.Show(GeneratorWindow::Mode::Text); break;
        case kMenuHistory: OpenHistory(); break;
        case kMenuSettings: OpenSettings(); break;
        case kMenuAutostart: ToggleAutostart(); break;
        case kMenuExit: Exit(); break;
    }
}

void TrayApp::Notify(const std::wstring& message, BalloonAction action) {
    balloonAction_ = action;
    NOTIFYICONDATAW data = icon_;
    data.uFlags = NIF_INFO;
    data.dwInfoFlags = NIIF_INFO | NIIF_RESPECT_QUIET_TIME;
    wcscpy_s(data.szInfoTitle, L"Kodo QR Instant");
    wcsncpy_s(data.szInfo, message.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

// --- Settings ----------------------------------------------------------------

std::wstring TrayApp::RegisterHotkeys(const Settings& settings) {
    UnregisterHotkeys();
    std::wstring conflicts;
    for (const auto& [id, hotkey] : {std::pair{kReadHotkeyId, settings.readHotkey}, std::pair{kCreateHotkeyId, settings.createHotkey}}) {
        if (!RegisterHotKey(window_, id, hotkey.modifiers | MOD_NOREPEAT, hotkey.key)) {
            conflicts += (conflicts.empty() ? L"" : L" ") + ToString(hotkey) + L" is already used by another app.";
        }
    }
    return conflicts;
}

void TrayApp::UnregisterHotkeys() {
    UnregisterHotKey(window_, kReadHotkeyId);
    UnregisterHotKey(window_, kCreateHotkeyId);
}

bool TrayApp::ApplySettings(const Settings& settings, bool autostart, std::wstring& error) {
    error = RegisterHotkeys(settings);
    if (!error.empty()) {
        // Settings stays open: keep shortcuts paused; the old ones come back on close.
        UnregisterHotkeys();
        return false;
    }
    settings_ = settings;
    if (!settings_.Save()) {
        error = L"Could not save the settings.";
        return false;
    }
    if (autostart != IsAutostartEnabled() && !SetAutostartEnabled(autostart, ExecutablePath().wstring())) {
        error = L"Could not change Start with Windows.";
        return false;
    }
    return true;
}

void TrayApp::OpenSettings() {
    if (captureActive_) return;
    popup_.Hide();
    // Pause the shortcuts so they can be recorded in Settings.
    UnregisterHotkeys();
    ShowSettingsDialog(
        instance_, window_, settings_,
        [this](const Settings& settings, bool autostart, std::wstring& error) { return ApplySettings(settings, autostart, error); },
        [this] {
            history_.Clear();
            if (historyWindow_.IsVisible()) historyWindow_.Refresh();
        });
    if (!exiting_) RegisterHotkeys(settings_);
    TrimWorkingSet();
}

void TrayApp::ToggleAutostart() {
    if (!SetAutostartEnabled(!IsAutostartEnabled(), ExecutablePath().wstring())) {
        Notify(L"Could not change Start with Windows.");
    }
}

void TrayApp::Exit() {
    exiting_ = true;
    if (task_.joinable()) task_.join();
    popup_.Destroy();
    toast_.Destroy();
    generator_.Destroy();
    historyWindow_.Destroy();
    UnregisterHotkeys();
    Shell_NotifyIconW(NIM_DELETE, &icon_);
    if (icon_.hIcon) DestroyIcon(icon_.hIcon);
    DestroyWindow(window_);
}

// --- Reading -----------------------------------------------------------------

void TrayApp::ReadCode() {
    // One capture at a time; repeated shortcuts while selecting are ignored.
    if (captureActive_ || task_.joinable() || exiting_) return;
    captureActive_ = true;
    popup_.Hide();

    auto snapshot = ScreenSnapshot::Capture();
    if (!snapshot) {
        captureActive_ = false;
        Notify(L"Could not capture the screen.");
        return;
    }
    const auto selection = SelectScreenRegion(instance_, *snapshot);
    if (!selection) {
        captureActive_ = false;  // Canceled.
        return;
    }
    selectionOnScreen_ = SnapshotToScreen(*selection, snapshot->bounds());
    auto image = std::make_shared<BgraImage>(snapshot->Crop(*selection));
    snapshot.reset();

    auto texts = std::make_shared<std::vector<std::wstring>>();
    RunTask(
        [image, texts] {
            *texts = DecodeQrCodes(*image);
            Wipe(image->pixels);
        },
        [this, texts] {
            captureActive_ = false;
            FinishRead(std::move(*texts));
        });
}

void TrayApp::FinishRead(std::vector<std::wstring> texts) {
    if (texts.empty()) {
        toast_.Show(selectionOnScreen_, L"No QR code found", Toast::Kind::Info);
        return;
    }
    for (auto it = texts.rbegin(); it != texts.rend(); ++it) Remember(HistoryEntry::Origin::Read, *it);
    results_ = std::move(texts);
    ShowResult(0);
}

void TrayApp::ShowResult(size_t index) {
    shownResult_ = index;
    const std::wstring text = results_[index];
    const Payload payload = ParsePayload(text);
    PopupContent content;
    content.icon = KindIcon(payload.kind);
    content.title = KindName(payload.kind);
    content.body = Describe(payload);

    HWND owner = window_;
    const auto copy = [owner](std::wstring value) {
        return [owner, value = std::move(value)] { return SetClipboardText(owner, ToWindowsLineBreaks(value)); };
    };
    const auto action = [](const wchar_t* icon, std::wstring label, std::function<bool()> run, bool closes, bool copied) {
        return PopupAction{icon, std::move(label), std::move(run), closes, copied};
    };
    switch (payload.kind) {
        case PayloadKind::Link: {
            const std::wstring link = Trim(text);
            content.actions.push_back(action(icons::kOpen, L"Open link", [link] { return OpenWithShell(link); }, true, false));
            content.actions.push_back(action(icons::kCopy, L"Copy link", copy(link), false, true));
            break;
        }
        case PayloadKind::Wifi:
            if (!payload.wifi.password.empty()) {
                content.actions.push_back(action(icons::kCopy, L"Copy password", copy(payload.wifi.password), false, true));
            }
            content.actions.push_back(action(icons::kWifi, L"Copy network name", copy(payload.wifi.ssid), false, true));
            break;
        case PayloadKind::Email: {
            content.actions.push_back(action(icons::kCopy, L"Copy address", copy(payload.email.to), false, true));
            const std::wstring mail = BuildEmail(payload.email);
            content.actions.push_back(action(icons::kMail, L"Write email", [mail] { return OpenWithShell(mail); }, true, false));
            break;
        }
        case PayloadKind::Phone:
            content.actions.push_back(action(icons::kCopy, L"Copy number", copy(payload.number), false, true));
            break;
        case PayloadKind::Sms:
            content.actions.push_back(action(icons::kCopy, L"Copy number", copy(payload.number), false, true));
            if (!payload.message.empty()) content.actions.push_back(action(icons::kMessage, L"Copy message", copy(payload.message), false, true));
            break;
        case PayloadKind::Contact:
            content.actions.push_back(action(icons::kCopy, L"Copy details", copy(content.body), false, true));
            if (!payload.contact.phone.empty()) content.actions.push_back(action(icons::kPhone, L"Copy phone", copy(payload.contact.phone), false, true));
            break;
        case PayloadKind::Location:
            content.actions.push_back(action(icons::kCopy, L"Copy coordinates", copy(payload.location), false, true));
            break;
        case PayloadKind::Text:
            // Plain text goes straight to the clipboard, as with any copy tool.
            content.note = SetClipboardText(window_, ToWindowsLineBreaks(text)) ? L"Copied to clipboard" : L"Couldn't access the clipboard";
            content.actions.push_back(action(icons::kCopy, L"Copy again", copy(text), false, true));
            break;
    }
    const std::wstring position =
        results_.size() > 1 ? L"Code " + std::to_wstring(index + 1) + L" of " + std::to_wstring(results_.size()) : L"";
    popup_.Show(selectionOnScreen_, std::move(content), position);
}

// --- Creating ----------------------------------------------------------------

void TrayApp::CreateFromClipboard() {
    if (captureActive_) return;
    popup_.Hide();
    const auto text = GetClipboardText(window_);
    if (!text || !HasVisibleText(*text)) {
        generator_.Show(GeneratorWindow::Mode::Text, std::wstring());
        Notify(L"The clipboard has no text. Type the text for the QR code instead.");
        return;
    }
    const std::wstring trimmed = Trim(ToUnixLineBreaks(*text));
    generator_.Show(GeneratorWindow::Mode::Text, trimmed);
    if (EncodeQrCode(trimmed)) Remember(HistoryEntry::Origin::Created, trimmed);
}

void TrayApp::OpenHistory() { historyWindow_.Show(settings_.keepHistory); }

void TrayApp::Remember(HistoryEntry::Origin origin, const std::wstring& text) {
    if (!settings_.keepHistory) return;
    history_.Add(origin, text, UnixTimeNow());
    if (historyWindow_.IsVisible()) historyWindow_.Refresh();
}

void TrayApp::RunTask(std::function<void()> work, std::function<void()> done) {
    taskDone_ = std::move(done);
    task_ = std::thread([this, work = std::move(work)] {
        work();
        PostMessageW(window_, WM_APP_TASK_DONE, 0, 0);
    });
}

// --- Window procedure ----------------------------------------------------------

LRESULT CALLBACK TrayApp::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* app = static_cast<TrayApp*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        app->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    auto* app = reinterpret_cast<TrayApp*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    return app ? app->HandleMessage(message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT TrayApp::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == taskbarCreatedMessage_ && taskbarCreatedMessage_ != 0) {
        AddTrayIcon();  // Explorer restarted.
        return 0;
    }
    switch (message) {
        case WM_HOTKEY:
            if (wParam == kCreateHotkeyId) {
                CreateFromClipboard();
            } else {
                ReadCode();
            }
            return 0;
        case WM_APP_TRAY:
            switch (LOWORD(lParam)) {
                case NIN_SELECT:
                case NIN_KEYSELECT:
                    ReadCode();
                    break;
                case WM_CONTEXTMENU:
                    ShowTrayMenu({GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam)});
                    break;
                case NIN_BALLOONUSERCLICK:
                    if (balloonAction_ == BalloonAction::OpenSettings) OpenSettings();
                    break;
            }
            return 0;
        case WM_APP_TASK_DONE: {
            task_.join();
            auto done = std::move(taskDone_);
            taskDone_ = nullptr;
            if (done && !exiting_) done();
            TrimWorkingSet();
            return 0;
        }
        case WM_APP_OPEN_SETTINGS:
            OpenSettings();
            return 0;
        case WM_APP_ALREADY_RUNNING:
            Notify(L"Kodo is already running. Press " + ToString(settings_.readHotkey) + L" to read a QR code or " +
                   ToString(settings_.createHotkey) + L" to create one from the clipboard.");
            return 0;
        case WM_CLOSE:  // Sent by the installer before upgrading or uninstalling.
        case WM_ENDSESSION:
            if (message == WM_CLOSE || wParam) Exit();
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}

}  // namespace kodo
