#include "app/selection_overlay.h"

#include "core/geometry.h"

#include <windowsx.h>

#include <algorithm>

namespace kodo {

namespace {

constexpr wchar_t kClassName[] = L"KodoSelectionOverlay";
constexpr int kBorderWidth = 2;
constexpr int kDimPercent = 45;

struct Overlay {
    const ScreenSnapshot& snapshot;
    HBITMAP dimmed = nullptr;
    RECT clientBounds{};
    bool dragging = false;
    POINT start{};
    RECT selection{};
    bool finished = false;
    std::optional<RECT> result;

    void Finish(std::optional<RECT> value, HWND window) {
        result = value;
        finished = true;
        ShowWindow(window, SW_HIDE);
    }
};

// A darkened copy of the snapshot, prepared once so dragging only blits.
HBITMAP CreateDimmedCopy(const ScreenSnapshot& snapshot) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = snapshot.width();
    info.bmiHeader.biHeight = -snapshot.height();
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap) return nullptr;
    const size_t bytes = static_cast<size_t>(snapshot.width()) * snapshot.height() * 4;
    const uint8_t* source = snapshot.pixels();
    auto* target = static_cast<uint8_t*>(bits);
    constexpr int keep = (100 - kDimPercent) * 256 / 100;
    for (size_t i = 0; i < bytes; ++i) target[i] = static_cast<uint8_t>((source[i] * keep) >> 8);
    return bitmap;
}

RECT SelectionWithBorder(const RECT& selection) {
    RECT rect = selection;
    InflateRect(&rect, kBorderWidth, kBorderWidth);
    return rect;
}

void Paint(HWND window, Overlay& overlay) {
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(window, &paint);
    const RECT& area = paint.rcPaint;
    const int width = area.right - area.left;
    const int height = area.bottom - area.top;

    // Compose off screen and copy once to avoid flicker.
    HDC buffer = CreateCompatibleDC(dc);
    HBITMAP bufferBitmap = CreateCompatibleBitmap(dc, width, height);
    HGDIOBJ previousBuffer = SelectObject(buffer, bufferBitmap);
    HDC source = CreateCompatibleDC(dc);

    HGDIOBJ previousSource = SelectObject(source, overlay.dimmed);
    BitBlt(buffer, 0, 0, width, height, source, area.left, area.top, SRCCOPY);

    if (!IsRectEmpty(&overlay.selection)) {
        RECT visible;
        if (IntersectRect(&visible, &overlay.selection, &area)) {
            SelectObject(source, overlay.snapshot.bitmap());
            BitBlt(buffer, visible.left - area.left, visible.top - area.top, visible.right - visible.left,
                   visible.bottom - visible.top, source, visible.left, visible.top, SRCCOPY);
        }
        RECT border = SelectionWithBorder(overlay.selection);
        OffsetRect(&border, -area.left, -area.top);
        HBRUSH brush = GetSysColorBrush(COLOR_HIGHLIGHT);
        for (int i = 0; i < kBorderWidth; ++i) {
            FrameRect(buffer, &border, brush);
            InflateRect(&border, -1, -1);
        }
    }

    BitBlt(dc, area.left, area.top, width, height, buffer, 0, 0, SRCCOPY);
    SelectObject(source, previousSource);
    DeleteDC(source);
    SelectObject(buffer, previousBuffer);
    DeleteObject(bufferBitmap);
    DeleteDC(buffer);
    EndPaint(window, &paint);
}

void UpdateSelection(HWND window, Overlay& overlay, POINT current) {
    RECT invalid = SelectionWithBorder(overlay.selection);
    overlay.selection = SelectionRect(overlay.start, current, overlay.clientBounds);
    const RECT updated = SelectionWithBorder(overlay.selection);
    UnionRect(&invalid, &invalid, &updated);
    InvalidateRect(window, &invalid, FALSE);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* overlay = reinterpret_cast<Overlay*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        overlay = static_cast<Overlay*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(overlay));
    }
    if (!overlay) return DefWindowProcW(window, message, wParam, lParam);

    switch (message) {
        case WM_PAINT:
            Paint(window, *overlay);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_SETCURSOR:
            SetCursor(LoadCursorW(nullptr, IDC_CROSS));
            return TRUE;
        case WM_LBUTTONDOWN:
            overlay->dragging = true;
            overlay->start = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            SetCapture(window);
            UpdateSelection(window, *overlay, overlay->start);
            return 0;
        case WM_MOUSEMOVE:
            if (overlay->dragging) UpdateSelection(window, *overlay, {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            return 0;
        case WM_LBUTTONUP:
            if (overlay->dragging) {
                overlay->dragging = false;
                ReleaseCapture();
                UpdateSelection(window, *overlay, {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
                if (IsUsableSelection(overlay->selection)) {
                    overlay->Finish(overlay->selection, window);
                } else {
                    // A click or a sliver: let the user try again.
                    InvalidateRect(window, nullptr, FALSE);
                    overlay->selection = {};
                }
            }
            return 0;
        case WM_RBUTTONUP:
            overlay->Finish(std::nullopt, window);
            return 0;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) overlay->Finish(std::nullopt, window);
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE && !overlay->finished) overlay->Finish(std::nullopt, window);
            return 0;
        case WM_CAPTURECHANGED:
            overlay->dragging = false;
            return 0;
        case WM_DISPLAYCHANGE:
            // The frozen image no longer matches the monitors.
            overlay->Finish(std::nullopt, window);
            return 0;
        case WM_DPICHANGED:
            // The overlay spans all monitors; keep its size in physical pixels.
            return 0;
        case WM_CLOSE:
            overlay->Finish(std::nullopt, window);
            return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

}  // namespace

std::optional<RECT> SelectScreenRegion(HINSTANCE instance, const ScreenSnapshot& snapshot) {
    static const ATOM windowClass = [instance] {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_CROSS);
        wc.lpszClassName = kClassName;
        return RegisterClassExW(&wc);
    }();
    if (!windowClass) return std::nullopt;

    Overlay overlay{snapshot};
    overlay.dimmed = CreateDimmedCopy(snapshot);
    if (!overlay.dimmed) return std::nullopt;
    overlay.clientBounds = {0, 0, snapshot.width(), snapshot.height()};

    const RECT& bounds = snapshot.bounds();
    HWND window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kClassName, L"Kodo selection", WS_POPUP,
                                  bounds.left, bounds.top, snapshot.width(), snapshot.height(), nullptr, nullptr,
                                  instance, &overlay);
    if (window) {
        ShowWindow(window, SW_SHOW);
        UpdateWindow(window);
        SetForegroundWindow(window);
        SetFocus(window);

        MSG message;
        while (!overlay.finished) {
            const BOOL got = GetMessageW(&message, nullptr, 0, 0);
            if (got <= 0) {
                if (got == 0) PostQuitMessage(static_cast<int>(message.wParam));
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        DestroyWindow(window);
    }
    DeleteObject(overlay.dimmed);
    return overlay.result;
}

}  // namespace kodo
