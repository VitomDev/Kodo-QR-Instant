#include "app/history_window.h"

#include "app/resource.h"
#include "core/clipboard.h"
#include "core/payload.h"
#include "core/text.h"

#include <commctrl.h>
#include <uxtheme.h>

#include <algorithm>

namespace kodo {

namespace {

constexpr wchar_t kClassName[] = L"KodoHistory";

enum ControlId {
    kList = 100,
    kShowButton,
    kCopyButton,
    kDeleteButton,
    kClearButton,
};

constexpr UINT_PTR kCopiedTimer = 1;

// Layout in 96-DPI pixels.
constexpr int kWidth = 640;
constexpr int kListHeight = 360;
constexpr int kPadding = 24;
constexpr int kButtonHeight = 32;
constexpr int kRadius = 4;
constexpr int kRowHeight = 52;

const wchar_t* KindGlyph(PayloadKind kind) {
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

}  // namespace

bool HistoryWindow::Create(HINSTANCE instance, History* history) {
    instance_ = instance;
    history_ = history;
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_KODO));
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);
    window_ = CreateWindowExW(WS_EX_CONTROLPARENT | WS_EX_APPWINDOW, kClassName, L"History – Kodo",
                              WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN, CW_USEDEFAULT,
                              CW_USEDEFAULT, 100, 100, nullptr, nullptr, instance, this);
    if (!window_) return false;

    list_ = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS |
                                                       LVS_NOCOLUMNHEADER,
                            0, 0, 0, 0, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kList)), instance, nullptr);
    ListView_SetExtendedListViewStyle(list_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    // One column; the rows are drawn by Kodo (see DrawRow).
    LVCOLUMNW column{LVCF_WIDTH};
    column.cx = 100;
    ListView_InsertColumn(list_, 0, &column);
    for (int id : {kShowButton, kCopyButton, kDeleteButton, kClearButton}) {
        HWND button = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, window_,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
        EnableHover(button);
    }
    return true;
}

void HistoryWindow::Destroy() {
    if (window_) DestroyWindow(window_);
    window_ = list_ = nullptr;
    if (rowHeight_) ImageList_Destroy(rowHeight_);
    rowHeight_ = nullptr;
    for (HGDIOBJ object : {static_cast<HGDIOBJ>(backgroundBrush_), static_cast<HGDIOBJ>(titleFont_),
                           static_cast<HGDIOBJ>(textFont_), static_cast<HGDIOBJ>(iconFont_),
                           static_cast<HGDIOBJ>(smallFont_), static_cast<HGDIOBJ>(kindFont_)}) {
        if (object) DeleteObject(object);
    }
    backgroundBrush_ = nullptr;
    titleFont_ = textFont_ = iconFont_ = smallFont_ = kindFont_ = nullptr;
}

int HistoryWindow::Scale(int value) const { return MulDiv(value, GetDpiForWindow(window_), 96); }

void HistoryWindow::RefreshStyle() {
    theme_ = Theme::Current();
    if (backgroundBrush_) DeleteObject(backgroundBrush_);
    backgroundBrush_ = CreateSolidBrush(theme_.background);
    for (HFONT font : {titleFont_, textFont_, iconFont_, smallFont_, kindFont_}) {
        if (font) DeleteObject(font);
    }
    const UINT dpi = GetDpiForWindow(window_);
    titleFont_ = CreateUiFont(dpi, 16, FW_SEMIBOLD);
    textFont_ = CreateUiFont(dpi, 10);
    iconFont_ = CreateIconFont(dpi, 10);
    smallFont_ = CreateUiFont(dpi, 9);
    kindFont_ = CreateIconFont(dpi, 14);
    SendMessageW(list_, WM_SETFONT, reinterpret_cast<WPARAM>(textFont_), FALSE);
    ApplyWindowStyle(window_, theme_);
    SetWindowTheme(list_, theme_.dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    ListView_SetBkColor(list_, theme_.background);
    // A list view takes its row height from its small image list.
    if (rowHeight_) ImageList_Destroy(rowHeight_);
    rowHeight_ = ImageList_Create(1, Scale(kRowHeight), ILC_COLOR32, 1, 0);
    ListView_SetImageList(list_, rowHeight_, LVSIL_SMALL);
}

void HistoryWindow::Layout() {
    const int width = Scale(kWidth);
    const int padding = Scale(kPadding);
    const int inner = width - 2 * padding;
    int y = padding;
    titleRect_ = {padding, y, width - padding, y + Scale(32)};
    y += Scale(32) + Scale(14);

    SetWindowPos(list_, nullptr, padding, y, inner, Scale(kListHeight), SWP_NOZORDER | SWP_NOACTIVATE);
    ListView_SetColumnWidth(list_, 0, inner - GetSystemMetricsForDpi(SM_CXVSCROLL, GetDpiForWindow(window_)));
    y += Scale(kListHeight) + Scale(16);

    // Show QR and Copy on the left, Delete and Clear history on the right.
    const ButtonFonts fonts{textFont_, iconFont_};
    const UINT dpi = GetDpiForWindow(window_);
    const auto place = [&](int id, int x, int w) {
        SetWindowPos(GetDlgItem(window_, id), nullptr, x, y, w, Scale(kButtonHeight), SWP_NOZORDER | SWP_NOACTIVATE);
    };
    const int show = ButtonWidth(window_, fonts, icons::kQrCode, L"Show QR code", dpi);
    const int copy = ButtonWidth(window_, fonts, icons::kCopy, L"Copy text", dpi);
    const int remove = ButtonWidth(window_, fonts, icons::kDelete, L"Delete", dpi);
    const int clear = ButtonWidth(window_, fonts, nullptr, L"Clear history", dpi);
    place(kShowButton, padding, show);
    place(kCopyButton, padding + show + Scale(8), copy);
    place(kClearButton, width - padding - clear, clear);
    place(kDeleteButton, width - padding - clear - Scale(8) - remove, remove);
    y += Scale(kButtonHeight) + padding;

    RECT frame{0, 0, width, y};
    AdjustWindowRectExForDpi(&frame, GetWindowLongW(window_, GWL_STYLE), FALSE, GetWindowLongW(window_, GWL_EXSTYLE),
                             GetDpiForWindow(window_));
    SetWindowPos(window_, nullptr, 0, 0, frame.right - frame.left, frame.bottom - frame.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(window_, nullptr, TRUE);
}

void HistoryWindow::Paint() {
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(window_, &paint);
    RECT client;
    GetClientRect(window_, &client);
    FillRect(dc, &client, backgroundBrush_);
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ previous = SelectObject(dc, titleFont_);
    SetTextColor(dc, theme_.text);
    DrawTextW(dc, L"History", -1, &titleRect_, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, previous);
    EndPaint(window_, &paint);
}

void HistoryWindow::Draw(const DRAWITEMSTRUCT& item) {
    const ButtonFonts fonts{textFont_, iconFont_};
    const int radius = Scale(kRadius);
    switch (item.CtlID) {
        case kShowButton:
            DrawButton(item, theme_, fonts, ButtonStyle::Primary, icons::kQrCode, L"Show QR code", radius);
            break;
        case kCopyButton: {
            const bool copied = copiedIndex_ >= 0 && copiedIndex_ == Selected();
            DrawButton(item, theme_, fonts, ButtonStyle::Standard, copied ? icons::kCheck : icons::kCopy, copied ? L"Copied" : L"Copy text", radius);
            break;
        }
        case kDeleteButton:
            DrawButton(item, theme_, fonts, ButtonStyle::Standard, icons::kDelete, L"Delete", radius);
            break;
        case kClearButton:
            DrawButton(item, theme_, fonts, ButtonStyle::Subtle, nullptr, L"Clear history", radius);
            break;
    }
}

std::wstring HistoryWindow::Summary(const HistoryEntry& entry) {
    const Payload payload = ParsePayload(entry.text);
    // Wi-Fi passwords are not shown in the list, only the network.
    if (payload.kind == PayloadKind::Wifi) return payload.wifi.ssid;
    if (payload.kind == PayloadKind::Contact && !payload.contact.name.empty()) return payload.contact.name;
    if (payload.kind == PayloadKind::Email) return payload.email.to;
    return Describe(payload);
}

void HistoryWindow::DrawRow(HDC dc, int index) {
    if (index < 0 || index >= static_cast<int>(entries_.size())) return;
    const HistoryEntry& entry = entries_[index];
    const Payload payload = ParsePayload(entry.text);
    RECT row;
    ListView_GetItemRect(list_, index, &row, LVIR_BOUNDS);
    RECT client;
    GetClientRect(list_, &client);
    row.right = client.right;
    FillRect(dc, &row, backgroundBrush_);
    const bool selected = (ListView_GetItemState(list_, index, LVIS_SELECTED) & LVIS_SELECTED) != 0;
    RECT card = row;
    InflateRect(&card, 0, -Scale(2));
    if (selected) {
        FillRounded(dc, card, theme_.hover, Scale(kRadius));
        // Accent bar on the left, like Windows 11 lists.
        RECT bar{card.left + Scale(2), card.top + Scale(12), card.left + Scale(5), card.bottom - Scale(12)};
        FillRounded(dc, bar, theme_.accent, Scale(2));
    }
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ previous = SelectObject(dc, kindFont_);
    SetTextColor(dc, theme_.accent);
    RECT icon{card.left + Scale(14), card.top, card.left + Scale(40), card.bottom};
    DrawTextW(dc, KindGlyph(payload.kind), -1, &icon, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    const int textLeft = icon.right + Scale(12);
    const int middle = (card.top + card.bottom) / 2;
    SelectObject(dc, textFont_);
    SetTextColor(dc, theme_.text);
    const std::wstring summary = OneLinePreview(Summary(entry), 200);
    RECT top{textLeft, card.top + Scale(4), card.right - Scale(12), middle + Scale(2)};
    DrawTextW(dc, summary.c_str(), -1, &top, DT_LEFT | DT_BOTTOM | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, smallFont_);
    SetTextColor(dc, theme_.secondaryText);
    const std::wstring details = std::wstring(entry.origin == HistoryEntry::Origin::Read ? L"Read" : L"Created") +
                                 L" \u00B7 " + KindName(payload.kind) + L" \u00B7 " + FormatTime(entry.time);
    RECT bottom{textLeft, middle + Scale(4), card.right - Scale(12), card.bottom - Scale(4)};
    DrawTextW(dc, details.c_str(), -1, &bottom, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, previous);
}

int HistoryWindow::Selected() const { return list_ ? ListView_GetNextItem(list_, -1, LVNI_SELECTED) : -1; }

void HistoryWindow::UpdateButtons() {
    const bool selected = Selected() >= 0;
    for (int id : {kShowButton, kCopyButton, kDeleteButton}) EnableWindow(GetDlgItem(window_, id), selected);
    EnableWindow(GetDlgItem(window_, kClearButton), !entries_.empty());
}

void HistoryWindow::Refresh() {
    const int previous = Selected();
    entries_ = history_->Load();
    SendMessageW(list_, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(list_);
    for (size_t i = 0; i < entries_.size(); ++i) {
        const HistoryEntry& entry = entries_[i];
        // The item text is for accessibility; the row itself is drawn by DrawRow.
        std::wstring text = OneLinePreview(Summary(entry), 160);
        LVITEMW item{LVIF_TEXT};
        item.iItem = static_cast<int>(i);
        item.pszText = text.data();
        ListView_InsertItem(list_, &item);
    }
    if (!entries_.empty()) {
        const int select = std::clamp(previous, 0, static_cast<int>(entries_.size()) - 1);
        ListView_SetItemState(list_, select, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
    SendMessageW(list_, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list_, nullptr, TRUE);
    UpdateButtons();
}

void HistoryWindow::Show(bool enabled) {
    enabled_ = enabled;
    const bool firstShow = !IsWindowVisible(window_) && IsRectEmpty(&titleRect_);
    if (firstShow) {
        POINT cursor;
        GetCursorPos(&cursor);
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &monitor);
        SetWindowPos(window_, nullptr, monitor.rcWork.left, monitor.rcWork.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        RefreshStyle();
        Layout();
        RECT bounds;
        GetWindowRect(window_, &bounds);
        const RECT& work = monitor.rcWork;
        SetWindowPos(window_, nullptr, work.left + (work.right - work.left - (bounds.right - bounds.left)) / 2,
                     std::max<int>(work.top, work.top + (work.bottom - work.top - (bounds.bottom - bounds.top)) / 2), 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER);
    } else {
        RefreshStyle();
        Layout();
    }
    Refresh();
    ShowWindow(window_, IsIconic(window_) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(window_);
    SetFocus(list_);
}

void HistoryWindow::Hide() {
    if (!window_) return;
    ShowWindow(window_, SW_HIDE);
    ListView_DeleteAllItems(list_);
    entries_.clear();
}

LRESULT CALLBACK HistoryWindow::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* self = static_cast<HistoryWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<HistoryWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    return self ? self->HandleMessage(message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT HistoryWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_COMMAND: {
            const int index = Selected();
            switch (LOWORD(wParam)) {
                case IDOK:
                case kShowButton:
                    if (index >= 0 && onShowCode) onShowCode(entries_[index].text);
                    return 0;
                case kCopyButton:
                    if (index >= 0 && SetClipboardText(window_, ToWindowsLineBreaks(entries_[index].text))) {
                        copiedIndex_ = index;
                        InvalidateRect(GetDlgItem(window_, kCopyButton), nullptr, FALSE);
                        SetTimer(window_, kCopiedTimer, 1500, nullptr);
                    }
                    return 0;
                case kDeleteButton:
                    if (index >= 0) {
                        history_->Remove(static_cast<size_t>(index));
                        Refresh();
                    }
                    return 0;
                case kClearButton:
                    if (MessageBoxW(window_, L"Delete every code in the history?", L"Kodo QR Instant", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                        history_->Clear();
                        Refresh();
                    }
                    return 0;
                case IDCANCEL:
                    Hide();
                    return 0;
            }
            break;
        }
        case WM_NOTIFY: {
            const auto* header = reinterpret_cast<const NMHDR*>(lParam);
            if (header->idFrom != kList) break;
            if (header->code == LVN_ITEMCHANGED) {
                InvalidateRect(list_, nullptr, FALSE);
                UpdateButtons();
            } else if (header->code == NM_DBLCLK) {
                SendMessageW(window_, WM_COMMAND, kShowButton, 0);
            } else if (header->code == LVN_KEYDOWN && reinterpret_cast<const NMLVKEYDOWN*>(lParam)->wVKey == VK_DELETE) {
                SendMessageW(window_, WM_COMMAND, kDeleteButton, 0);
            } else if (header->code == NM_CUSTOMDRAW) {
                auto* draw = reinterpret_cast<NMLVCUSTOMDRAW*>(lParam);
                if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW | CDRF_NOTIFYPOSTPAINT;
                if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    DrawRow(draw->nmcd.hdc, static_cast<int>(draw->nmcd.dwItemSpec));
                    return CDRF_SKIPDEFAULT;
                }
                if (draw->nmcd.dwDrawStage == CDDS_POSTPAINT) {
                    // Below the rows the list view still draws its column edge; paint it over.
                    RECT client;
                    GetClientRect(list_, &client);
                    RECT below = client;
                    const int count = ListView_GetItemCount(list_);
                    RECT last{};
                    if (count > 0 && ListView_GetItemRect(list_, count - 1, &last, LVIR_BOUNDS)) below.top = std::max(below.top, last.bottom);
                    if (below.top < below.bottom) FillRect(draw->nmcd.hdc, &below, backgroundBrush_);
                }
                if (draw->nmcd.dwDrawStage == CDDS_POSTPAINT && entries_.empty()) {
                    // Hint in the empty list.
                    RECT client;
                    GetClientRect(list_, &client);
                    client.top += Scale(48);
                    SetBkMode(draw->nmcd.hdc, TRANSPARENT);
                    SetTextColor(draw->nmcd.hdc, theme_.secondaryText);
                    HGDIOBJ previous = SelectObject(draw->nmcd.hdc, textFont_);
                    DrawTextW(draw->nmcd.hdc,
                              enabled_ ? L"Nothing here yet. Codes you read or create will appear here."
                                       : L"History is turned off. Turn it on in Settings.",
                              -1, &client, DT_CENTER | DT_TOP | DT_WORDBREAK);
                    SelectObject(draw->nmcd.hdc, previous);
                }
                return CDRF_DODEFAULT;
            }
            break;
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
            InvalidateRect(GetDlgItem(window_, kCopyButton), nullptr, FALSE);
            return 0;
        case WM_SETTINGCHANGE:
            RefreshStyle();
            Layout();
            return 0;
        case WM_DPICHANGED: {
            const auto* suggested = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(window_, nullptr, suggested->left, suggested->top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            RefreshStyle();
            Layout();
            return 0;
        }
        case WM_CLOSE:
            Hide();
            return 0;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}

}  // namespace kodo
