#include "app/theme.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>

namespace kodo {

namespace {

bool FontExists(const wchar_t* face) {
    LOGFONTW query{};
    query.lfCharSet = DEFAULT_CHARSET;
    wcscpy_s(query.lfFaceName, face);
    bool found = false;
    HDC dc = GetDC(nullptr);
    EnumFontFamiliesExW(dc, &query, [](const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM found) {
        *reinterpret_cast<bool*>(found) = true;
        return 0;
    }, reinterpret_cast<LPARAM>(&found), 0);
    ReleaseDC(nullptr, dc);
    return found;
}

HFONT CreateFontByFace(const wchar_t* face, UINT dpi, int points, int weight) {
    LOGFONTW font{};
    font.lfHeight = -MulDiv(points, static_cast<int>(dpi), 72);
    font.lfWeight = weight;
    font.lfCharSet = DEFAULT_CHARSET;
    font.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(font.lfFaceName, face);
    return CreateFontIndirectW(&font);
}

COLORREF Blend(COLORREF a, COLORREF b, int percentOfB) {
    const auto mix = [&](int x, int y) { return static_cast<BYTE>((x * (100 - percentOfB) + y * percentOfB) / 100); };
    return RGB(mix(GetRValue(a), GetRValue(b)), mix(GetGValue(a), GetGValue(b)), mix(GetBValue(a), GetBValue(b)));
}

}  // namespace

Theme Theme::Current() {
    DWORD lightApps = 1;
    DWORD size = sizeof(lightApps);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &lightApps, &size);

    Theme theme;
    theme.dark = lightApps == 0;
    if (theme.dark) {
        theme.background = RGB(43, 43, 43);
        theme.text = RGB(255, 255, 255);
        theme.secondaryText = RGB(170, 170, 170);
        theme.divider = RGB(62, 62, 62);
    } else {
        theme.background = RGB(249, 249, 249);
        theme.text = RGB(26, 26, 26);
        theme.secondaryText = RGB(98, 98, 98);
        theme.divider = RGB(229, 229, 229);
    }
    theme.hover = Blend(theme.background, theme.text, 8);
    theme.pressed = Blend(theme.background, theme.text, 4);

    // The Windows accent color (stored as 0xAABBGGRR), like the system flyouts use.
    DWORD accent = 0;
    size = sizeof(accent);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", L"AccentColor", RRF_RT_REG_DWORD,
                     nullptr, &accent, &size) == ERROR_SUCCESS) {
        theme.accent = accent & 0xFFFFFF;
    } else {
        theme.accent = RGB(0, 103, 192);
    }
    // Lighter accent on dark backgrounds, as Windows does.
    if (theme.dark) theme.accent = Blend(theme.accent, RGB(255, 255, 255), 35);
    const int luma = (GetRValue(theme.accent) * 299 + GetGValue(theme.accent) * 587 + GetBValue(theme.accent) * 114) / 1000;
    theme.accentText = luma > 150 ? RGB(0, 0, 0) : RGB(255, 255, 255);
    return theme;
}

HFONT CreateUiFont(UINT dpi, int points, int weight) {
    static const bool variable = FontExists(L"Segoe UI Variable Text");
    return CreateFontByFace(variable ? L"Segoe UI Variable Text" : L"Segoe UI", dpi, points, weight);
}

HFONT CreateIconFont(UINT dpi, int points) {
    static const bool fluent = FontExists(L"Segoe Fluent Icons");
    return CreateFontByFace(fluent ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets", dpi, points, FW_NORMAL);
}

void EnableDarkMenus() {
    // uxtheme's SetPreferredAppMode (ordinal 135, Windows 10 1903+) is not
    // documented, but it is the only way to get dark Win32 menus; it is simply
    // skipped where it does not exist.
    using SetPreferredAppMode = int(WINAPI*)(int mode);
    if (HMODULE uxtheme = GetModuleHandleW(L"uxtheme.dll")) {
        if (auto setMode = reinterpret_cast<SetPreferredAppMode>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(135)))) {
            setMode(1);  // AllowDark: follow the system setting
        }
    }
}

void ApplyWindowStyle(HWND window, const Theme& theme) {
    // Windows 11 only; Windows 10 ignores these attributes.
    const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
    DwmSetWindowAttribute(window, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
    const COLORREF border = theme.divider;
    DwmSetWindowAttribute(window, DWMWA_BORDER_COLOR, &border, sizeof(border));
    const BOOL dark = theme.dark;
    DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    // Child edit controls pick dark scroll bars from this theme name.
    for (HWND child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        SetWindowTheme(child, theme.dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    }
}

void FillRounded(HDC dc, const RECT& rect, COLORREF color, int radius) {
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ previousBrush = SelectObject(dc, brush);
    HGDIOBJ previousPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius * 2, radius * 2);
    SelectObject(dc, previousBrush);
    SelectObject(dc, previousPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void OutlineRounded(HDC dc, const RECT& rect, COLORREF color, int radius, int width) {
    HPEN pen = CreatePen(PS_INSIDEFRAME, width, color);
    HGDIOBJ previousBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    HGDIOBJ previousPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius * 2, radius * 2);
    SelectObject(dc, previousBrush);
    SelectObject(dc, previousPen);
    DeleteObject(pen);
}

int TextWidth(HWND window, HFONT font, const wchar_t* text) {
    HDC dc = GetDC(window);
    HGDIOBJ previous = SelectObject(dc, font);
    SIZE size{};
    GetTextExtentPoint32W(dc, text, static_cast<int>(wcslen(text)), &size);
    SelectObject(dc, previous);
    ReleaseDC(window, dc);
    return size.cx;
}

namespace {

constexpr wchar_t kHotProperty[] = L"KodoHot";

LRESULT CALLBACK HoverProc(HWND button, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
    if (message == WM_MOUSEMOVE && !GetPropW(button, kHotProperty)) {
        SetPropW(button, kHotProperty, reinterpret_cast<HANDLE>(1));
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, button, 0};
        TrackMouseEvent(&track);
        InvalidateRect(button, nullptr, FALSE);
    } else if (message == WM_MOUSELEAVE) {
        RemovePropW(button, kHotProperty);
        InvalidateRect(button, nullptr, FALSE);
    } else if (message == WM_NCDESTROY) {
        RemovePropW(button, kHotProperty);
        RemoveWindowSubclass(button, HoverProc, 0);
    }
    return DefSubclassProc(button, message, wParam, lParam);
}

}  // namespace

void EnableHover(HWND button) { SetWindowSubclass(button, HoverProc, 0, 0); }

bool IsHot(HWND button) { return GetPropW(button, kHotProperty) != nullptr && IsWindowEnabled(button); }

void DrawButton(const DRAWITEMSTRUCT& item, const Theme& theme, const ButtonFonts& fonts, ButtonStyle style,
                const wchar_t* icon, const wchar_t* label, int radius, COLORREF iconColor) {
    HDC dc = item.hDC;
    const RECT& rect = item.rcItem;
    const bool pressed = item.itemState & ODS_SELECTED;
    const bool disabled = item.itemState & ODS_DISABLED;
    const bool hot = IsHot(item.hwndItem);

    HBRUSH background = CreateSolidBrush(theme.background);
    FillRect(dc, &rect, background);
    DeleteObject(background);
    COLORREF foreground = disabled ? theme.secondaryText : theme.text;
    switch (style) {
        case ButtonStyle::Primary: {
            COLORREF fill = disabled ? theme.divider : theme.accent;
            if (hot || pressed) fill = RGB(GetRValue(fill) * 9 / 10, GetGValue(fill) * 9 / 10, GetBValue(fill) * 9 / 10);
            FillRounded(dc, rect, fill, radius);
            if (!disabled) foreground = theme.accentText;
            break;
        }
        case ButtonStyle::Standard:
            FillRounded(dc, rect, hot || pressed ? theme.hover : theme.pressed, radius);
            OutlineRounded(dc, rect, theme.divider, radius);
            break;
        case ButtonStyle::Subtle:
            if (hot || pressed) FillRounded(dc, rect, pressed ? theme.pressed : theme.hover, radius);
            break;
    }

    SetBkMode(dc, TRANSPARENT);
    const UINT dpi = GetDpiForWindow(item.hwndItem);
    const int iconSize = MulDiv(16, static_cast<int>(dpi), 96);
    const int gap = label && *label && icon ? MulDiv(8, static_cast<int>(dpi), 96) : 0;
    HGDIOBJ previous = SelectObject(dc, fonts.text);
    SIZE textSize{};
    if (label) GetTextExtentPoint32W(dc, label, static_cast<int>(wcslen(label)), &textSize);
    const int contentWidth = (icon ? iconSize : 0) + gap + textSize.cx;
    int x = (rect.left + rect.right - contentWidth) / 2;
    if (icon) {
        SelectObject(dc, fonts.icon);
        SetTextColor(dc, iconColor != CLR_INVALID && !disabled && style != ButtonStyle::Primary ? iconColor : foreground);
        RECT iconRect{x, rect.top, x + iconSize, rect.bottom};
        DrawTextW(dc, icon, -1, &iconRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        x += iconSize + gap;
    }
    if (label && *label) {
        SelectObject(dc, fonts.text);
        SetTextColor(dc, foreground);
        RECT textRect{x, rect.top, rect.right, rect.bottom};
        DrawTextW(dc, label, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    SelectObject(dc, previous);

    if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT)) {
        RECT focus = rect;
        InflateRect(&focus, -2, -2);
        DrawFocusRect(dc, &focus);
    }
}

int ButtonWidth(HWND window, const ButtonFonts& fonts, const wchar_t* icon, const wchar_t* label, UINT dpi) {
    const auto scale = [dpi](int value) { return MulDiv(value, static_cast<int>(dpi), 96); };
    int width = scale(32);
    if (icon) width += scale(16);
    if (label && *label) width += TextWidth(window, fonts.text, label) + (icon ? scale(8) : 0);
    return width;
}

}  // namespace kodo
