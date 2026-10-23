#include "app/result_popup.h"

#include "core/text.h"

#include <algorithm>

namespace kodo {

namespace {

constexpr wchar_t kClassName[] = L"KodoResultPopup";

enum ControlId {
    kCloseButton = 100,
    kNextButton,
    kBodyEdit,
    kActionFirst = 200,
};

constexpr UINT_PTR kCopiedTimer = 1;

// Layout in 96-DPI pixels.
constexpr int kWidth = 400;
constexpr int kPadding = 16;
constexpr int kHeaderHeight = 32;
constexpr int kButtonHeight = 32;
constexpr int kGap = 12;
constexpr int kMaxBodyHeight = 220;
constexpr int kRadius = 4;

int MeasureHeight(HWND window, HFONT font, const std::wstring& text, int width) {
    HDC dc = GetDC(window);
    HGDIOBJ previous = SelectObject(dc, font);
    RECT rect{0, 0, width, 0};
    DrawTextW(dc, text.empty() ? L" " : text.c_str(), -1, &rect, DT_CALCRECT | DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX);
    TEXTMETRICW metrics{};
    GetTextMetricsW(dc, &metrics);
    SelectObject(dc, previous);
    ReleaseDC(window, dc);
    return std::max<int>(rect.bottom, metrics.tmHeight);
}

}  // namespace

bool ResultPopup::Create(HINSTANCE instance) {
    instance_ = instance;
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    window_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_CONTROLPARENT, kClassName, L"Kodo QR Instant",
                              WS_POPUP | WS_CLIPCHILDREN, 0, 0, kWidth, 200, nullptr, nullptr, instance, this);
    if (!window_) return false;
    const auto add = [&](const wchar_t* type, DWORD style, int id) {
        HWND control = CreateWindowExW(0, type, L"", WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, window_,
                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
        if (wcscmp(type, L"BUTTON") == 0) EnableHover(control);
        return control;
    };
    add(L"BUTTON", BS_OWNERDRAW | WS_TABSTOP, kCloseButton);
    add(L"BUTTON", BS_OWNERDRAW | WS_TABSTOP, kNextButton);
    add(L"EDIT", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP, kBodyEdit);
    return true;
}

void ResultPopup::Destroy() {
    if (window_) DestroyWindow(window_);
    window_ = nullptr;
    for (HGDIOBJ object : {static_cast<HGDIOBJ>(backgroundBrush_), static_cast<HGDIOBJ>(textFont_),
                           static_cast<HGDIOBJ>(titleFont_), static_cast<HGDIOBJ>(smallFont_),
                           static_cast<HGDIOBJ>(buttonFont_), static_cast<HGDIOBJ>(iconFont_),
                           static_cast<HGDIOBJ>(titleIconFont_)}) {
        if (object) DeleteObject(object);
    }
    backgroundBrush_ = nullptr;
    textFont_ = titleFont_ = smallFont_ = buttonFont_ = iconFont_ = titleIconFont_ = nullptr;
}

int ResultPopup::Scale(int value) const { return MulDiv(value, GetDpiForWindow(window_), 96); }

// Picks up the current theme and the DPI of the monitor the popup is on.
void ResultPopup::RefreshStyle() {
    theme_ = Theme::Current();
    if (backgroundBrush_) DeleteObject(backgroundBrush_);
    backgroundBrush_ = CreateSolidBrush(theme_.background);
    for (HFONT font : {textFont_, titleFont_, smallFont_, buttonFont_, iconFont_, titleIconFont_}) {
        if (font) DeleteObject(font);
    }
    const UINT dpi = GetDpiForWindow(window_);
    textFont_ = CreateUiFont(dpi, 11);
    titleFont_ = CreateUiFont(dpi, 10, FW_SEMIBOLD);
    smallFont_ = CreateUiFont(dpi, 9);
    buttonFont_ = CreateUiFont(dpi, 10);
    iconFont_ = CreateIconFont(dpi, 10);
    titleIconFont_ = CreateIconFont(dpi, 12);
    HWND edit = GetDlgItem(window_, kBodyEdit);
    SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(textFont_), FALSE);
    SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
    ApplyWindowStyle(window_, theme_);
}

void ResultPopup::Layout() {
    const int width = Scale(kWidth);
    const int padding = Scale(kPadding);
    const int gap = Scale(kGap);
    const int inner = width - 2 * padding;
    const ButtonFonts fonts{buttonFont_, iconFont_};
    const UINT dpi = GetDpiForWindow(window_);
    const auto place = [&](HWND control, int x, int y, int w, int h) {
        SetWindowPos(control, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    };

    int y = padding;
    const int header = Scale(kHeaderHeight);
    place(GetDlgItem(window_, kCloseButton), width - padding - header + Scale(8), y, header, header);
    const bool several = !position_.empty();
    HWND next = GetDlgItem(window_, kNextButton);
    ShowWindow(next, several ? SW_SHOW : SW_HIDE);
    if (several) {
        const int nextWidth = ButtonWidth(window_, fonts, nullptr, L"Next code", dpi);
        place(next, width - padding - header - nextWidth + Scale(4), y, nextWidth, header);
    }
    y += header + gap / 2;

    HWND edit = GetDlgItem(window_, kBodyEdit);
    const int needed = MeasureHeight(window_, textFont_, ToWindowsLineBreaks(content_.body), inner);
    const int bodyHeight = std::min(needed, Scale(kMaxBodyHeight));
    ShowScrollBar(edit, SB_VERT, needed > Scale(kMaxBodyHeight));
    place(edit, padding, y, inner, bodyHeight);
    y += bodyHeight;

    noteY_ = 0;
    if (!content_.note.empty() || several) {
        y += Scale(6);
        noteY_ = y;
        y += Scale(20);
    }
    y += gap + Scale(4);

    // Actions in a row, the primary one first; they share the width when
    // their labels would not fit side by side.
    if (!buttons_.empty()) {
        std::vector<int> widths;
        int total = 0;
        for (size_t i = 0; i < buttons_.size(); ++i) {
            const auto& action = content_.actions[i];
            widths.push_back(ButtonWidth(window_, fonts, action.icon, action.label.c_str(), dpi));
            total += widths.back();
        }
        total += Scale(8) * static_cast<int>(buttons_.size() - 1);
        int x = padding;
        for (size_t i = 0; i < buttons_.size(); ++i) {
            int w = widths[i];
            if (total > inner) w = (inner - Scale(8) * static_cast<int>(buttons_.size() - 1)) / static_cast<int>(buttons_.size());
            place(buttons_[i], x, y, w, Scale(kButtonHeight));
            x += w + Scale(8);
        }
        y += Scale(kButtonHeight);
    }
    const int height = y + padding;

    // Next to the selection: below it, or above when there is no room.
    HMONITOR monitor = MonitorFromRect(&anchor_, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(monitor, &info);
    const RECT& work = info.rcWork;
    const int x = std::clamp<int>(anchor_.left, work.left, std::max<int>(work.left, work.right - width));
    int top = anchor_.bottom + Scale(8);
    if (top + height > work.bottom) top = anchor_.top - Scale(8) - height;
    top = std::clamp<int>(top, work.top, std::max<int>(work.top, work.bottom - height));
    SetWindowPos(window_, HWND_TOPMOST, x, top, width, height, SWP_NOACTIVATE);
    InvalidateRect(window_, nullptr, TRUE);
}

void ResultPopup::Paint() {
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(window_, &paint);
    RECT client;
    GetClientRect(window_, &client);
    FillRect(dc, &client, backgroundBrush_);
    SetBkMode(dc, TRANSPARENT);
    const int padding = Scale(kPadding);
    const int header = Scale(kHeaderHeight);

    // Header: kind icon and name.
    HGDIOBJ previous = SelectObject(dc, titleIconFont_);
    SetTextColor(dc, theme_.accent);
    RECT icon{padding, padding, padding + Scale(20), padding + header};
    DrawTextW(dc, content_.icon ? content_.icon : icons::kQrCode, -1, &icon, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, titleFont_);
    SetTextColor(dc, theme_.text);
    RECT title{icon.right + Scale(8), padding, client.right / 2 + Scale(40), padding + header};
    DrawTextW(dc, content_.title.c_str(), -1, &title, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    if (noteY_) {
        SelectObject(dc, smallFont_);
        SetTextColor(dc, theme_.secondaryText);
        std::wstring note = content_.note;
        if (!position_.empty()) note = position_ + (note.empty() ? L"" : L"  ·  " + note);
        RECT rect{padding, noteY_, client.right - padding, noteY_ + Scale(20)};
        DrawTextW(dc, note.c_str(), -1, &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    // A hairline border for Windows 10, where DWM draws none.
    HBRUSH borderBrush = CreateSolidBrush(theme_.divider);
    FrameRect(dc, &client, borderBrush);
    DeleteObject(borderBrush);
    SelectObject(dc, previous);
    EndPaint(window_, &paint);
}

void ResultPopup::Draw(const DRAWITEMSTRUCT& item) {
    const ButtonFonts fonts{buttonFont_, iconFont_};
    const int radius = Scale(kRadius);
    const int id = static_cast<int>(item.CtlID);
    if (id == kCloseButton) {
        DrawButton(item, theme_, fonts, ButtonStyle::Subtle, icons::kClose, nullptr, radius, theme_.secondaryText);
    } else if (id == kNextButton) {
        DrawButton(item, theme_, fonts, ButtonStyle::Subtle, nullptr, L"Next code", radius);
    } else if (id >= kActionFirst && id - kActionFirst < static_cast<int>(content_.actions.size())) {
        const int index = id - kActionFirst;
        const auto& action = content_.actions[index];
        const bool copied = index == copiedIndex_;
        DrawButton(item, theme_, fonts, index == 0 ? ButtonStyle::Primary : ButtonStyle::Standard,
                   copied ? icons::kCheck : action.icon, copied ? L"Copied" : action.label.c_str(), radius);
    }
}

void ResultPopup::Show(const RECT& anchor, PopupContent content, const std::wstring& position) {
    anchor_ = anchor;
    // Move onto the target monitor first so sizes use its DPI.
    HMONITOR monitor = MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(monitor, &info);
    SetWindowPos(window_, nullptr, info.rcWork.left, info.rcWork.top, 0, 0, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSIZE);
    RefreshStyle();

    KillTimer(window_, kCopiedTimer);
    copiedIndex_ = -1;
    for (HWND button : buttons_) DestroyWindow(button);
    buttons_.clear();
    content_ = std::move(content);
    position_ = position;
    for (size_t i = 0; i < content_.actions.size(); ++i) {
        HWND button = CreateWindowExW(0, L"BUTTON", content_.actions[i].label.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                      0, 0, 0, 0, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kActionFirst + i)),
                                      instance_, nullptr);
        EnableHover(button);
        buttons_.push_back(button);
    }
    SetDlgItemTextW(window_, kBodyEdit, ToWindowsLineBreaks(content_.body).c_str());
    Layout();
    ShowWindow(window_, SW_SHOW);
    SetForegroundWindow(window_);
    SetFocus(buttons_.empty() ? GetDlgItem(window_, kCloseButton) : buttons_.front());
    // No focus outline until the keyboard is used, like native dialogs.
    SendMessageW(window_, WM_CHANGEUISTATE, MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS), 0);
}

void ResultPopup::Hide() {
    if (!window_) return;
    ShowWindow(window_, SW_HIDE);
    // Do not keep what was read (passwords, links) around once closed.
    SetDlgItemTextW(window_, kBodyEdit, L"");
    content_ = {};
}

void ResultPopup::Run(size_t index) {
    if (index >= content_.actions.size()) return;
    const PopupAction action = content_.actions[index];
    if (!action.run || !action.run()) return;
    if (action.closes) {
        Hide();
    } else if (action.showsCopied) {
        copiedIndex_ = static_cast<int>(index);
        Layout();
        SetTimer(window_, kCopiedTimer, 1500, nullptr);
    }
}

LRESULT CALLBACK ResultPopup::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* popup = static_cast<ResultPopup*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        popup->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(popup));
    }
    auto* popup = reinterpret_cast<ResultPopup*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    return popup ? popup->HandleMessage(message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT ResultPopup::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            if (id == IDCANCEL || id == kCloseButton) {  // Escape arrives as IDCANCEL through IsDialogMessage.
                Hide();
            } else if (id == IDOK) {
                // Enter runs the focused action, or the primary one.
                const int focused = GetDlgCtrlID(GetFocus());
                if (focused == kCloseButton || focused == kNextButton) {
                    SendMessageW(window_, WM_COMMAND, focused, 0);
                } else {
                    Run(focused >= kActionFirst ? static_cast<size_t>(focused - kActionFirst) : 0);
                }
            } else if (id == kNextButton) {
                if (onNext) onNext();
            } else if (id >= kActionFirst) {
                Run(static_cast<size_t>(id - kActionFirst));
            }
            return 0;
        }
        case WM_DRAWITEM:
            Draw(*reinterpret_cast<const DRAWITEMSTRUCT*>(lParam));
            return TRUE;
        case WM_PAINT:
            Paint();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_TIMER:
            KillTimer(window_, wParam);
            copiedIndex_ = -1;
            if (IsVisible()) Layout();
            return 0;
        case WM_ACTIVATE:
            // Behave like a flyout: clicking elsewhere closes it.
            if (LOWORD(wParam) == WA_INACTIVE) Hide();
            return 0;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
            SetBkColor(reinterpret_cast<HDC>(wParam), theme_.background);
            SetTextColor(reinterpret_cast<HDC>(wParam), theme_.text);
            return reinterpret_cast<LRESULT>(backgroundBrush_);
        case WM_SETTINGCHANGE:
            // Light/dark theme switched while the popup is open.
            if (IsVisible()) {
                RefreshStyle();
                Layout();
            }
            return 0;
        case WM_DPICHANGED:
            RefreshStyle();
            Layout();
            return 0;
        case WM_CLOSE:
            Hide();
            return 0;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}

}  // namespace kodo
