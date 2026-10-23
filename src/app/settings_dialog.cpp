#include "app/settings_dialog.h"

#include "app/resource.h"
#include "app/theme.h"
#include "version.h"

#include <commctrl.h>

#include <algorithm>
#include <vector>

namespace kodo {

namespace {

constexpr wchar_t kClassName[] = L"KodoSettings";

enum ControlId {
    kReadShortcut = 100,
    kCreateShortcut,
    kHistoryToggle,
    kClearHistory,
    kAutostartToggle,
    kCancel = IDCANCEL,
    kSave = IDOK,
};

// Layout in 96-DPI pixels.
constexpr int kWidth = 460;
constexpr int kPadding = 24;
constexpr int kRowHeight = 40;
constexpr int kFieldWidth = 200;
constexpr int kFieldHeight = 32;
constexpr int kSectionGap = 22;
constexpr int kRadius = 4;

enum class TextStyle { Title, Caption, Label, Hint };

struct TextItem {
    RECT rect;
    std::wstring text;
    TextStyle style;
};

struct State {
    HWND window = nullptr;
    Settings settings;
    ApplySettings apply;
    std::function<void()> clearHistory;
    bool autostart = false;
    bool historyCleared = false;
    int recording = 0;  // Shortcut control waiting for keys, or 0
    std::wstring status;
    bool statusIsError = true;
    bool done = false;

    Theme theme{};
    HBRUSH background = nullptr;
    HFONT titleFont = nullptr;
    HFONT captionFont = nullptr;
    HFONT textFont = nullptr;
    HFONT iconFont = nullptr;
    std::vector<TextItem> texts;
    RECT statusRect{};
};

HWND openWindow = nullptr;

int Scale(HWND window, int value) { return MulDiv(value, GetDpiForWindow(window), 96); }

// --- Style and layout ------------------------------------------------------------

void RefreshStyle(State& state) {
    state.theme = Theme::Current();
    if (state.background) DeleteObject(state.background);
    state.background = CreateSolidBrush(state.theme.background);
    for (HFONT font : {state.titleFont, state.captionFont, state.textFont, state.iconFont}) {
        if (font) DeleteObject(font);
    }
    const UINT dpi = GetDpiForWindow(state.window);
    state.titleFont = CreateUiFont(dpi, 16, FW_SEMIBOLD);
    state.captionFont = CreateUiFont(dpi, 10, FW_SEMIBOLD);
    state.textFont = CreateUiFont(dpi, 10);
    state.iconFont = CreateIconFont(dpi, 9);
    ApplyWindowStyle(state.window, state.theme);
}

void Layout(State& state) {
    HWND window = state.window;
    const auto scale = [&](int value) { return Scale(window, value); };
    const int width = scale(kWidth);
    const int padding = scale(kPadding);
    const int row = scale(kRowHeight);
    const int fieldWidth = scale(kFieldWidth);
    const int fieldHeight = scale(kFieldHeight);
    const auto place = [&](int id, int x, int y, int w, int h) {
        SetWindowPos(GetDlgItem(window, id), nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    };
    state.texts.clear();
    const auto text = [&](int x, int y, int w, int h, std::wstring value, TextStyle style) {
        state.texts.push_back({{x, y, x + w, y + h}, std::move(value), style});
    };
    const auto labelRow = [&](int y, const wchar_t* label, int controlId, int controlWidth, int controlHeight) {
        text(padding, y, width - 2 * padding - controlWidth, row, label, TextStyle::Label);
        place(controlId, width - padding - controlWidth, y + (row - controlHeight) / 2, controlWidth, controlHeight);
    };

    int y = padding;
    text(padding, y, width - 2 * padding, scale(32), L"Settings", TextStyle::Title);
    y += scale(32) + scale(kSectionGap) - scale(6);

    text(padding, y, width - 2 * padding, scale(24), L"Shortcuts", TextStyle::Caption);
    y += scale(24);
    labelRow(y, L"Read a QR code on screen", kReadShortcut, fieldWidth, fieldHeight);
    y += row;
    labelRow(y, L"Create a QR from the clipboard", kCreateShortcut, fieldWidth, fieldHeight);
    y += row + scale(kSectionGap);

    text(padding, y, width - 2 * padding, scale(24), L"History", TextStyle::Caption);
    y += scale(24);
    labelRow(y, L"Keep a history of codes", kHistoryToggle, scale(44), scale(20));
    y += row;
    const int clearWidth = scale(24) + TextWidth(window, state.textFont, L"Clear history");
    text(padding, y, width - 2 * padding - clearWidth, row,
         state.historyCleared ? L"History cleared" : L"Only on this PC. Wi-Fi passwords included.", TextStyle::Hint);
    place(kClearHistory, width - padding - clearWidth, y + (row - fieldHeight) / 2, clearWidth, fieldHeight);
    y += row + scale(kSectionGap);

    text(padding, y, width - 2 * padding, scale(24), L"General", TextStyle::Caption);
    y += scale(24);
    labelRow(y, L"Start with Windows", kAutostartToggle, scale(44), scale(20));
    y += row + scale(12);

    state.statusRect = {padding, y, width - padding, y + scale(20)};
    y += scale(20) + scale(16);

    const int buttonWidth = scale(96);
    text(padding, y, width - 2 * padding - 2 * buttonWidth - scale(16), fieldHeight,
         L"Kodo QR Instant " KODO_VERSION_STRING L" · VitomDev", TextStyle::Hint);
    place(kSave, width - padding - buttonWidth, y, buttonWidth, fieldHeight);
    place(kCancel, width - padding - 2 * buttonWidth - scale(8), y, buttonWidth, fieldHeight);
    y += fieldHeight + padding;

    // Grow the window around the client area.
    RECT frame{0, 0, width, y};
    AdjustWindowRectExForDpi(&frame, GetWindowLongW(window, GWL_STYLE), FALSE, GetWindowLongW(window, GWL_EXSTYLE),
                             GetDpiForWindow(window));
    SetWindowPos(window, nullptr, 0, 0, frame.right - frame.left, frame.bottom - frame.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(window, nullptr, TRUE);
}

// --- Painting --------------------------------------------------------------------

void Paint(State& state) {
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(state.window, &paint);
    RECT client;
    GetClientRect(state.window, &client);
    FillRect(dc, &client, state.background);
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ previous = SelectObject(dc, state.textFont);
    for (auto& item : state.texts) {
        COLORREF color = state.theme.text;
        HFONT font = state.textFont;
        switch (item.style) {
            case TextStyle::Title: font = state.titleFont; break;
            case TextStyle::Caption: font = state.captionFont; break;
            case TextStyle::Label: break;
            case TextStyle::Hint: color = state.theme.secondaryText; break;
        }
        SelectObject(dc, font);
        SetTextColor(dc, color);
        DrawTextW(dc, item.text.c_str(), -1, &item.rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    if (!state.status.empty()) {
        SelectObject(dc, state.textFont);
        SetTextColor(dc, state.statusIsError ? RGB(232, 72, 85) : state.theme.secondaryText);
        DrawTextW(dc, state.status.c_str(), -1, &state.statusRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    SelectObject(dc, previous);
    EndPaint(state.window, &paint);
}

void DrawToggle(State& state, const DRAWITEMSTRUCT& item, bool on) {
    // Switch: a pill with a knob, accent when on.
    HDC dc = item.hDC;
    const RECT& rect = item.rcItem;
    const Theme& theme = state.theme;
    FillRect(dc, &rect, state.background);
    const int height = rect.bottom - rect.top;
    if (on) {
        FillRounded(dc, rect, theme.accent, height / 2);
    } else {
        if (IsHot(item.hwndItem)) FillRounded(dc, rect, theme.hover, height / 2);
        OutlineRounded(dc, rect, theme.secondaryText, height / 2);
    }
    const int knob = Scale(state.window, 12);
    const int knobX = on ? rect.right - Scale(state.window, 4) - knob : rect.left + Scale(state.window, 4);
    RECT knobRect{knobX, rect.top + (height - knob) / 2, knobX + knob, rect.top + (height + knob) / 2};
    FillRounded(dc, knobRect, on ? theme.accentText : theme.secondaryText, knob / 2);
    if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT)) {
        RECT focus = rect;
        InflateRect(&focus, 2, 2);
        DrawFocusRect(dc, &focus);
    }
}

void DrawControl(State& state, const DRAWITEMSTRUCT& item) {
    const int id = static_cast<int>(item.CtlID);
    const Theme& theme = state.theme;
    const ButtonFonts fonts{state.textFont, state.iconFont};
    const int radius = Scale(state.window, kRadius);

    if (id == kReadShortcut || id == kCreateShortcut) {
        // Input-like field that records the next key combination.
        HDC dc = item.hDC;
        const RECT& rect = item.rcItem;
        const bool recording = state.recording == id;
        FillRect(dc, &rect, state.background);
        FillRounded(dc, rect, IsHot(item.hwndItem) || recording ? theme.hover : theme.pressed, radius);
        OutlineRounded(dc, rect, recording ? theme.accent : theme.divider, radius, recording ? 2 : 1);
        const Hotkey& hotkey = id == kReadShortcut ? state.settings.readHotkey : state.settings.createHotkey;
        const std::wstring label = recording ? L"Press a shortcut…" : ToString(hotkey);
        SetBkMode(dc, TRANSPARENT);
        HGDIOBJ previous = SelectObject(dc, state.textFont);
        SetTextColor(dc, recording ? theme.secondaryText : theme.text);
        RECT inner = rect;
        DrawTextW(dc, label.c_str(), -1, &inner, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, previous);
        if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT)) {
            RECT focus = rect;
            InflateRect(&focus, -2, -2);
            DrawFocusRect(dc, &focus);
        }
    } else if (id == kHistoryToggle) {
        DrawToggle(state, item, state.settings.keepHistory);
    } else if (id == kAutostartToggle) {
        DrawToggle(state, item, state.autostart);
    } else if (id == kClearHistory) {
        DrawButton(item, theme, fonts, ButtonStyle::Subtle, nullptr, L"Clear history", radius);
    } else if (id == kSave) {
        DrawButton(item, theme, fonts, ButtonStyle::Primary, nullptr, L"Save", radius);
    } else if (id == kCancel) {
        DrawButton(item, theme, fonts, ButtonStyle::Standard, nullptr, L"Cancel", radius);
    }
}

void SetStatus(State& state, const std::wstring& status, bool error = true) {
    state.status = status;
    state.statusIsError = error;
    InvalidateRect(state.window, &state.statusRect, TRUE);
}

// --- Actions ---------------------------------------------------------------------

void Save(State& state) {
    if (!state.settings.readHotkey.IsValid() || !state.settings.createHotkey.IsValid()) {
        SetStatus(state, L"Shortcuts need Ctrl, Alt or Shift plus a key.");
        return;
    }
    if (state.settings.readHotkey == state.settings.createHotkey) {
        SetStatus(state, L"Read and Create need different shortcuts.");
        return;
    }
    std::wstring error;
    if (!state.apply(state.settings, state.autostart, error)) {
        SetStatus(state, error);
        return;
    }
    state.done = true;
}

// Shortcut fields record the next key combination while they are active.
LRESULT CALLBACK ShortcutProc(HWND field, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data) {
    auto& state = *reinterpret_cast<State*>(data);
    const int id = GetDlgCtrlID(field);
    if (state.recording == id) {
        switch (message) {
            case WM_GETDLGCODE:
                return DLGC_WANTALLKEYS;
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN: {
                const UINT key = static_cast<UINT>(wParam);
                if (key == VK_ESCAPE) {
                    state.recording = 0;
                } else if (key == VK_CONTROL || key == VK_MENU || key == VK_SHIFT || key == VK_LWIN || key == VK_RWIN) {
                    return 0;  // Wait for the actual key.
                } else {
                    Hotkey hotkey{0, key};
                    if (GetKeyState(VK_CONTROL) < 0) hotkey.modifiers |= MOD_CONTROL;
                    if (GetKeyState(VK_MENU) < 0) hotkey.modifiers |= MOD_ALT;
                    if (GetKeyState(VK_SHIFT) < 0) hotkey.modifiers |= MOD_SHIFT;
                    if (!hotkey.IsValid()) {
                        SetStatus(state, L"Use Ctrl, Alt or Shift together with a key.");
                        return 0;
                    }
                    (id == kReadShortcut ? state.settings.readHotkey : state.settings.createHotkey) = hotkey;
                    state.recording = 0;
                    SetStatus(state, L"");
                }
                InvalidateRect(field, nullptr, FALSE);
                return 0;
            }
            case WM_SYSCHAR:
            case WM_CHAR:
                return 0;
            case WM_KILLFOCUS:
                state.recording = 0;
                InvalidateRect(field, nullptr, FALSE);
                break;
        }
    }
    return DefSubclassProc(field, message, wParam, lParam);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
    }
    auto* state = reinterpret_cast<State*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (!state) return DefWindowProcW(window, message, wParam, lParam);

    switch (message) {
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            if (id == kCancel) {
                state->done = true;
            } else if (id == kSave) {
                Save(*state);
            } else if (id == kReadShortcut || id == kCreateShortcut) {
                state->recording = state->recording == id ? 0 : id;
                SetFocus(GetDlgItem(window, id));
                InvalidateRect(GetDlgItem(window, kReadShortcut), nullptr, FALSE);
                InvalidateRect(GetDlgItem(window, kCreateShortcut), nullptr, FALSE);
            } else if (id == kHistoryToggle) {
                state->settings.keepHistory = !state->settings.keepHistory;
                InvalidateRect(GetDlgItem(window, id), nullptr, FALSE);
            } else if (id == kClearHistory) {
                if (MessageBoxW(window, L"Delete every code in the history?", L"Kodo QR Instant", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    if (state->clearHistory) state->clearHistory();
                    state->historyCleared = true;
                    Layout(*state);
                }
            } else if (id == kAutostartToggle) {
                state->autostart = !state->autostart;
                InvalidateRect(GetDlgItem(window, id), nullptr, FALSE);
            }
            return 0;
        }
        case WM_DRAWITEM:
            DrawControl(*state, *reinterpret_cast<const DRAWITEMSTRUCT*>(lParam));
            return TRUE;
        case WM_PAINT:
            Paint(*state);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_SETTINGCHANGE:
            RefreshStyle(*state);
            Layout(*state);
            return 0;
        case WM_DPICHANGED: {
            const auto* suggested = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(window, nullptr, suggested->left, suggested->top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            RefreshStyle(*state);
            Layout(*state);
            return 0;
        }
        case WM_CLOSE:
            state->done = true;
            return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

}  // namespace

void ShowSettingsDialog(HINSTANCE instance, HWND owner, const Settings& current, const ApplySettings& apply,
                        const std::function<void()>& clearHistory) {
    if (openWindow) {
        SetForegroundWindow(openWindow);
        return;
    }
    static const ATOM windowClass = [instance] {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_KODO));
        wc.lpszClassName = kClassName;
        return RegisterClassExW(&wc);
    }();
    if (!windowClass) return;

    State state;
    state.settings = current;
    state.apply = apply;
    state.clearHistory = clearHistory;
    state.autostart = IsAutostartEnabled();

    // Open on the monitor with the mouse, roughly centered.
    POINT cursor;
    GetCursorPos(&cursor);
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &monitor);
    HWND window = CreateWindowExW(WS_EX_CONTROLPARENT | WS_EX_APPWINDOW, kClassName, L"Kodo Settings",
                                  WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN, monitor.rcWork.left,
                                  monitor.rcWork.top, 100, 100, owner, nullptr, instance, &state);
    if (!window) return;
    state.window = window;
    openWindow = window;

    const auto add = [&](int id) {
        HWND control = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0,
                                       window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
        EnableHover(control);
        return control;
    };
    SetWindowSubclass(add(kReadShortcut), ShortcutProc, 0, reinterpret_cast<DWORD_PTR>(&state));
    SetWindowSubclass(add(kCreateShortcut), ShortcutProc, 0, reinterpret_cast<DWORD_PTR>(&state));
    add(kHistoryToggle);
    add(kClearHistory);
    add(kAutostartToggle);
    add(kCancel);
    add(kSave);

    RefreshStyle(state);
    Layout(state);
    RECT bounds;
    GetWindowRect(window, &bounds);
    const RECT& work = monitor.rcWork;
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    SetWindowPos(window, HWND_TOP, work.left + (work.right - work.left - width) / 2,
                 std::max<int>(work.top, work.top + (work.bottom - work.top - height) / 2), 0, 0, SWP_NOSIZE);
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    // No focus outline until the keyboard is used, like native dialogs.
    SendMessageW(window, WM_CHANGEUISTATE, MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS), 0);

    // Modal loop; the window also closes if Kodo exits meanwhile.
    MSG message;
    while (!state.done && IsWindow(window)) {
        const BOOL got = GetMessageW(&message, nullptr, 0, 0);
        if (got <= 0) {
            if (got == 0) PostQuitMessage(static_cast<int>(message.wParam));
            break;
        }
        if (IsDialogMessageW(window, &message)) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    openWindow = nullptr;
    if (IsWindow(window)) DestroyWindow(window);
    for (HGDIOBJ object : {static_cast<HGDIOBJ>(state.background), static_cast<HGDIOBJ>(state.titleFont),
                           static_cast<HGDIOBJ>(state.captionFont), static_cast<HGDIOBJ>(state.textFont),
                           static_cast<HGDIOBJ>(state.iconFont)}) {
        if (object) DeleteObject(object);
    }
}

}  // namespace kodo
