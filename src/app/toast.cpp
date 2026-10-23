#include "app/toast.h"

#include "app/theme.h"

#include <algorithm>

namespace kodo {

namespace {

constexpr wchar_t kClassName[] = L"KodoToast";
constexpr UINT_PTR kHoldTimer = 1;
constexpr UINT_PTR kFadeTimer = 2;
constexpr UINT kHoldMilliseconds = 1200;
constexpr UINT kFadeStepMilliseconds = 15;
constexpr int kFadeStep = 255 / 12;

// Layout in 96-DPI pixels.
constexpr int kHeight = 40;
constexpr int kPadding = 16;
constexpr int kIconGap = 10;
constexpr int kGap = 10;

}  // namespace

bool Toast::Create(HINSTANCE instance) {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);
    window_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT,
                              kClassName, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, this);
    return window_ != nullptr;
}

void Toast::Destroy() {
    if (window_) DestroyWindow(window_);
    window_ = nullptr;
}

void Toast::Show(const RECT& anchor, const std::wstring& text, Kind kind) {
    text_ = text;
    kind_ = kind;

    HMONITOR monitor = MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(monitor, &info);
    // Move onto the monitor first so the size uses its DPI.
    SetWindowPos(window_, nullptr, info.rcWork.left, info.rcWork.top, 1, 1, SWP_NOZORDER | SWP_NOACTIVATE);
    const UINT dpi = GetDpiForWindow(window_);
    const auto scale = [dpi](int value) { return MulDiv(value, static_cast<int>(dpi), 96); };

    HDC dc = GetDC(window_);
    HFONT font = CreateUiFont(dpi, 10);
    HGDIOBJ previous = SelectObject(dc, font);
    SIZE textSize{};
    GetTextExtentPoint32W(dc, text_.c_str(), static_cast<int>(text_.size()), &textSize);
    SelectObject(dc, previous);
    DeleteObject(font);
    ReleaseDC(window_, dc);

    const int width = scale(kPadding * 2 + 16 + kIconGap) + textSize.cx;
    const int height = scale(kHeight);
    const RECT& work = info.rcWork;
    int x = (anchor.left + anchor.right) / 2 - width / 2;
    int y = anchor.bottom + scale(kGap);
    if (y + height > work.bottom) y = anchor.bottom - height - scale(kGap);  // Inside the selection instead.
    x = std::clamp<int>(x, work.left, std::max<int>(work.left, work.right - width));
    y = std::clamp<int>(y, work.top, std::max<int>(work.top, work.bottom - height));

    ApplyWindowStyle(window_, Theme::Current());
    KillTimer(window_, kFadeTimer);
    alpha_ = 255;
    SetLayeredWindowAttributes(window_, 0, static_cast<BYTE>(alpha_), LWA_ALPHA);
    SetWindowPos(window_, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(window_, nullptr, TRUE);
    SetTimer(window_, kHoldTimer, kHoldMilliseconds, nullptr);
}

void Toast::Paint() {
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(window_, &paint);
    const Theme theme = Theme::Current();
    const UINT dpi = GetDpiForWindow(window_);
    const auto scale = [dpi](int value) { return MulDiv(value, static_cast<int>(dpi), 96); };
    RECT client;
    GetClientRect(window_, &client);

    HBRUSH background = CreateSolidBrush(theme.background);
    FillRect(dc, &client, background);
    DeleteObject(background);
    SetBkMode(dc, TRANSPARENT);

    RECT iconRect{scale(kPadding), 0, scale(kPadding + 16), client.bottom};
    HFONT iconFont = CreateIconFont(dpi, 11);
    HGDIOBJ previous = SelectObject(dc, iconFont);
    SetTextColor(dc, kind_ == Kind::Success ? theme.accent : theme.secondaryText);
    DrawTextW(dc, kind_ == Kind::Success ? icons::kCheck : icons::kInfo, -1, &iconRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    HFONT font = CreateUiFont(dpi, 10);
    SelectObject(dc, font);
    SetTextColor(dc, theme.text);
    RECT textRect{iconRect.right + scale(kIconGap), 0, client.right, client.bottom};
    DrawTextW(dc, text_.c_str(), -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    SelectObject(dc, previous);
    DeleteObject(iconFont);
    DeleteObject(font);
    EndPaint(window_, &paint);
}

LRESULT CALLBACK Toast::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* toast = static_cast<Toast*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        toast->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(toast));
    }
    auto* toast = reinterpret_cast<Toast*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (!toast) return DefWindowProcW(window, message, wParam, lParam);
    switch (message) {
        case WM_PAINT:
            toast->Paint();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_TIMER:
            if (wParam == kHoldTimer) {
                KillTimer(window, kHoldTimer);
                SetTimer(window, kFadeTimer, kFadeStepMilliseconds, nullptr);
            } else if (wParam == kFadeTimer) {
                toast->alpha_ -= kFadeStep;
                if (toast->alpha_ <= 0) {
                    KillTimer(window, kFadeTimer);
                    ShowWindow(window, SW_HIDE);
                } else {
                    SetLayeredWindowAttributes(window, 0, static_cast<BYTE>(toast->alpha_), LWA_ALPHA);
                }
            }
            return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

}  // namespace kodo
