#include "app/generator_window.h"

#include "app/resource.h"
#include "core/clipboard.h"
#include "core/paths.h"
#include "core/payload.h"
#include "core/png.h"
#include "core/text.h"

#include <commdlg.h>

#include <algorithm>
#include <array>

namespace kodo {

namespace {

constexpr wchar_t kClassName[] = L"KodoGenerator";

enum ControlId {
    kModeFirst = 100,  // One chip per Mode, in order
    kTextEdit = 110,
    kWifiSsid,
    kWifiPassword,
    kContactName,
    kContactPhone,
    kContactEmail,
    kContactOrganization,
    kContactWebsite,
    kEmailTo,
    kEmailSubject,
    kEmailBody,
    kSecurityButton = 130,
    kHiddenToggle,
    kCopyButton,
    kSaveButton,
};

constexpr UINT_PTR kCopiedTimer = 1;

// Layout in 96-DPI pixels.
constexpr int kPadding = 24;
constexpr int kLeftWidth = 300;
constexpr int kColumnGap = 28;
constexpr int kPreviewSize = 240;
constexpr int kFieldHeight = 34;
constexpr int kLabelHeight = 22;
constexpr int kFieldGap = 12;
constexpr int kChipHeight = 30;
constexpr int kRadius = 4;

constexpr std::array<const wchar_t*, 4> kModeNames{L"Text", L"Wi-Fi", L"Contact", L"Email"};

struct Field {
    int id;
    const wchar_t* label;
    int multilineHeight;  // 0 for a single line
};

std::vector<Field> FieldsOf(GeneratorWindow::Mode mode) {
    switch (mode) {
        case GeneratorWindow::Mode::Text: return {{kTextEdit, L"Text or link", 200}};
        case GeneratorWindow::Mode::Wifi: return {{kWifiSsid, L"Network name", 0}, {kWifiPassword, L"Password", 0}};
        case GeneratorWindow::Mode::Contact:
            return {{kContactName, L"Name", 0}, {kContactPhone, L"Phone", 0}, {kContactEmail, L"Email", 0},
                    {kContactOrganization, L"Organization", 0}, {kContactWebsite, L"Website", 0}};
        case GeneratorWindow::Mode::Email:
            return {{kEmailTo, L"To", 0}, {kEmailSubject, L"Subject", 0}, {kEmailBody, L"Message", 100}};
    }
    return {};
}

bool IsMultiline(int id) { return id == kTextEdit || id == kEmailBody; }

std::wstring SecurityName(const std::wstring& security) {
    if (security == L"WPA") return L"WPA / WPA2 / WPA3";
    if (security == L"WEP") return L"WEP";
    return L"None (open network)";
}

std::wstring ControlText(HWND window, int id) {
    HWND control = GetDlgItem(window, id);
    std::wstring text(static_cast<size_t>(GetWindowTextLengthW(control)) + 1, L'\0');
    text.resize(static_cast<size_t>(GetWindowTextW(control, text.data(), static_cast<int>(text.size()))));
    return text;
}

}  // namespace

bool GeneratorWindow::Create(HINSTANCE instance) {
    instance_ = instance;
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_KODO));
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);
    window_ = CreateWindowExW(WS_EX_CONTROLPARENT | WS_EX_APPWINDOW, kClassName, L"Create QR code – Kodo",
                              WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN, CW_USEDEFAULT,
                              CW_USEDEFAULT, 100, 100, nullptr, nullptr, instance, this);
    if (!window_) return false;
    CreateControls();
    return true;
}

void GeneratorWindow::Destroy() {
    if (window_) DestroyWindow(window_);
    window_ = nullptr;
    for (HGDIOBJ object : {static_cast<HGDIOBJ>(backgroundBrush_), static_cast<HGDIOBJ>(fieldBrush_),
                           static_cast<HGDIOBJ>(titleFont_), static_cast<HGDIOBJ>(textFont_),
                           static_cast<HGDIOBJ>(smallFont_), static_cast<HGDIOBJ>(iconFont_)}) {
        if (object) DeleteObject(object);
    }
    backgroundBrush_ = fieldBrush_ = nullptr;
    titleFont_ = textFont_ = smallFont_ = iconFont_ = nullptr;
}

int GeneratorWindow::Scale(int value) const { return MulDiv(value, GetDpiForWindow(window_), 96); }

void GeneratorWindow::CreateControls() {
    const auto add = [&](const wchar_t* type, DWORD style, int id) {
        HWND control = CreateWindowExW(0, type, L"", WS_CHILD | WS_TABSTOP | style, 0, 0, 0, 0, window_,
                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
        if (wcscmp(type, L"BUTTON") == 0) EnableHover(control);
        return control;
    };
    for (int i = 0; i < static_cast<int>(kModeNames.size()); ++i) add(L"BUTTON", BS_OWNERDRAW | WS_VISIBLE, kModeFirst + i);
    for (int id = kTextEdit; id <= kEmailBody; ++id) {
        const DWORD style = IsMultiline(id) ? ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN : ES_AUTOHSCROLL;
        HWND edit = add(L"EDIT", style, id);
        SendMessageW(edit, EM_SETLIMITTEXT, 4000, 0);
    }
    add(L"BUTTON", BS_OWNERDRAW, kSecurityButton);
    add(L"BUTTON", BS_OWNERDRAW, kHiddenToggle);
    add(L"BUTTON", BS_OWNERDRAW | WS_VISIBLE, kCopyButton);
    add(L"BUTTON", BS_OWNERDRAW | WS_VISIBLE, kSaveButton);
}

void GeneratorWindow::RefreshStyle() {
    theme_ = Theme::Current();
    for (HBRUSH brush : {backgroundBrush_, fieldBrush_}) {
        if (brush) DeleteObject(brush);
    }
    backgroundBrush_ = CreateSolidBrush(theme_.background);
    fieldBrush_ = CreateSolidBrush(theme_.pressed);
    for (HFONT font : {titleFont_, textFont_, smallFont_, iconFont_}) {
        if (font) DeleteObject(font);
    }
    const UINT dpi = GetDpiForWindow(window_);
    titleFont_ = CreateUiFont(dpi, 16, FW_SEMIBOLD);
    textFont_ = CreateUiFont(dpi, 10);
    smallFont_ = CreateUiFont(dpi, 9);
    iconFont_ = CreateIconFont(dpi, 10);
    for (int id = kTextEdit; id <= kEmailBody; ++id) {
        HWND edit = GetDlgItem(window_, id);
        SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(textFont_), FALSE);
        SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
    }
    ApplyWindowStyle(window_, theme_);
}

void GeneratorWindow::Layout() {
    const auto scale = [&](int value) { return Scale(value); };
    const int padding = scale(kPadding);
    const int left = padding;
    const int leftWidth = scale(kLeftWidth);
    const auto place = [&](int id, int x, int y, int w, int h) {
        SetWindowPos(GetDlgItem(window_, id), nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    };
    labels_.clear();
    fieldFrames_.clear();

    int y = padding;
    labels_.push_back({{left, y, left + leftWidth, y + scale(32)}, L"Create QR code"});
    y += scale(32) + scale(14);
    const int contentTop = y;

    // Template chips.
    int x = left;
    for (int i = 0; i < static_cast<int>(kModeNames.size()); ++i) {
        const int width = scale(28) + TextWidth(window_, textFont_, kModeNames[i]);
        place(kModeFirst + i, x, y, width, scale(kChipHeight));
        x += width + scale(8);
    }
    y += scale(kChipHeight) + scale(18);

    // Fields of the current template; the others are hidden.
    const std::vector<Field> fields = FieldsOf(mode_);
    for (int id = kTextEdit; id <= kEmailBody; ++id) {
        const bool visible = std::any_of(fields.begin(), fields.end(), [id](const Field& f) { return f.id == id; });
        ShowWindow(GetDlgItem(window_, id), visible ? SW_SHOW : SW_HIDE);
    }
    const int inset = scale(10);
    const auto frame = [&](int height) {
        fieldFrames_.push_back({left, y, left + leftWidth, y + height});
    };
    for (const auto& field : fields) {
        labels_.push_back({{left, y, left + leftWidth, y + scale(kLabelHeight)}, field.label});
        y += scale(kLabelHeight) + scale(2);
        const int height = field.multilineHeight ? scale(field.multilineHeight) : scale(kFieldHeight);
        frame(height);
        if (field.multilineHeight) {
            place(field.id, left + inset, y + scale(7), leftWidth - inset - scale(4), height - scale(12));
        } else {
            // Single-line edits are as tall as their font; center them in the frame.
            const int editHeight = scale(20);
            place(field.id, left + inset, y + (height - editHeight) / 2, leftWidth - 2 * inset, editHeight);
        }
        y += height + scale(kFieldGap);
    }
    const bool wifi = mode_ == Mode::Wifi;
    ShowWindow(GetDlgItem(window_, kSecurityButton), wifi ? SW_SHOW : SW_HIDE);
    ShowWindow(GetDlgItem(window_, kHiddenToggle), wifi ? SW_SHOW : SW_HIDE);
    if (wifi) {
        labels_.push_back({{left, y, left + leftWidth, y + scale(kLabelHeight)}, L"Security"});
        y += scale(kLabelHeight) + scale(2);
        place(kSecurityButton, left, y, leftWidth, scale(kFieldHeight));
        y += scale(kFieldHeight) + scale(kFieldGap);
        labels_.push_back({{left, y, left + leftWidth - scale(52), y + scale(kFieldHeight)}, L"Hidden network"});
        place(kHiddenToggle, left + leftWidth - scale(44), y + scale(7), scale(44), scale(20));
        y += scale(kFieldHeight) + scale(kFieldGap);
        EnableWindow(GetDlgItem(window_, kWifiPassword), !security_.empty());
    }
    const int leftBottom = y - scale(kFieldGap);

    // Preview column.
    const int right = left + leftWidth + scale(kColumnGap);
    const int previewSize = scale(kPreviewSize);
    preview_ = {right, contentTop, right + previewSize, contentTop + previewSize};
    y = preview_.bottom + scale(8);
    statusRect_ = {right, y, right + previewSize, y + scale(36)};
    y = statusRect_.bottom + scale(8);
    const int buttonWidth = (previewSize - scale(8)) / 2;
    place(kCopyButton, right, y, buttonWidth, scale(kFieldHeight));
    place(kSaveButton, right + buttonWidth + scale(8), y, buttonWidth, scale(kFieldHeight));
    const int rightBottom = y + scale(kFieldHeight);

    RECT client{0, 0, preview_.right + padding, std::max(leftBottom, rightBottom) + padding};
    AdjustWindowRectExForDpi(&client, GetWindowLongW(window_, GWL_STYLE), FALSE, GetWindowLongW(window_, GWL_EXSTYLE),
                             GetDpiForWindow(window_));
    SetWindowPos(window_, nullptr, 0, 0, client.right - client.left, client.bottom - client.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(window_, nullptr, TRUE);
}

void GeneratorWindow::Paint() {
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(window_, &paint);
    RECT client;
    GetClientRect(window_, &client);
    // Compose off screen: the preview and fields repaint on every keystroke.
    HDC buffer = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
    HGDIOBJ previousBitmap = SelectObject(buffer, bitmap);
    FillRect(buffer, &client, backgroundBrush_);
    SetBkMode(buffer, TRANSPARENT);
    const int radius = Scale(kRadius);

    HGDIOBJ previousFont = SelectObject(buffer, titleFont_);
    for (size_t i = 0; i < labels_.size(); ++i) {
        SelectObject(buffer, i == 0 ? titleFont_ : textFont_);
        SetTextColor(buffer, i == 0 ? theme_.text : theme_.secondaryText);
        DrawTextW(buffer, labels_[i].text.c_str(), -1, &labels_[i].rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    // Input frames around the borderless edits; the focused one in accent.
    const std::vector<Field> fields = FieldsOf(mode_);
    HWND focus = GetFocus();
    for (size_t i = 0; i < fieldFrames_.size() && i < fields.size(); ++i) {
        HWND edit = GetDlgItem(window_, fields[i].id);
        const bool focused = focus == edit;
        const bool enabled = IsWindowEnabled(edit);
        FillRounded(buffer, fieldFrames_[i], enabled ? theme_.pressed : theme_.background, radius);
        OutlineRounded(buffer, fieldFrames_[i], focused ? theme_.accent : theme_.divider, radius, focused ? 2 : 1);
    }

    // The code on a white card, whatever the theme: scanners need dark on light.
    FillRounded(buffer, preview_, RGB(255, 255, 255), Scale(8));
    if (!theme_.dark) OutlineRounded(buffer, preview_, theme_.divider, Scale(8));
    if (code_) {
        const int inner = preview_.right - preview_.left - Scale(16);
        const BgraImage image = RenderQrCode(*code_, std::max(1, ModuleSizeFor(*code_, inner)));
        BITMAPINFO info{};
        info.bmiHeader = {sizeof(BITMAPINFOHEADER), image.width, -image.height, 1, 32, BI_RGB};
        const int x = (preview_.left + preview_.right - image.width) / 2;
        const int y = (preview_.top + preview_.bottom - image.height) / 2;
        SetDIBitsToDevice(buffer, x, y, image.width, image.height, 0, 0, 0, image.height, image.pixels.data(), &info,
                          DIB_RGB_COLORS);
    } else {
        HFONT bigIcon = CreateIconFont(GetDpiForWindow(window_), 40);
        SelectObject(buffer, bigIcon);
        SetTextColor(buffer, RGB(200, 200, 200));
        RECT iconRect = preview_;
        DrawTextW(buffer, icons::kQrCode, -1, &iconRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(buffer, textFont_);
        DeleteObject(bigIcon);
    }

    if (!status_.empty()) {
        SelectObject(buffer, smallFont_);
        SetTextColor(buffer, statusIsError_ ? RGB(232, 72, 85) : theme_.secondaryText);
        RECT rect = statusRect_;
        DrawTextW(buffer, status_.c_str(), -1, &rect, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
    }

    BitBlt(dc, 0, 0, client.right, client.bottom, buffer, 0, 0, SRCCOPY);
    SelectObject(buffer, previousFont);
    SelectObject(buffer, previousBitmap);
    DeleteObject(bitmap);
    DeleteDC(buffer);
    EndPaint(window_, &paint);
}

void GeneratorWindow::Draw(const DRAWITEMSTRUCT& item) {
    HDC dc = item.hDC;
    const RECT& rect = item.rcItem;
    const int id = static_cast<int>(item.CtlID);
    const ButtonFonts fonts{textFont_, iconFont_};
    const int radius = Scale(kRadius);
    const bool hot = IsHot(item.hwndItem);

    if (id >= kModeFirst && id < kModeFirst + static_cast<int>(kModeNames.size())) {
        const bool selected = static_cast<int>(mode_) == id - kModeFirst;
        FillRect(dc, &rect, backgroundBrush_);
        const int pill = (rect.bottom - rect.top) / 2;
        if (selected) {
            FillRounded(dc, rect, theme_.accent, pill);
        } else {
            if (hot) FillRounded(dc, rect, theme_.hover, pill);
            OutlineRounded(dc, rect, theme_.divider, pill);
        }
        SetBkMode(dc, TRANSPARENT);
        HGDIOBJ previous = SelectObject(dc, textFont_);
        SetTextColor(dc, selected ? theme_.accentText : theme_.text);
        RECT text = rect;
        DrawTextW(dc, kModeNames[id - kModeFirst], -1, &text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, previous);
        if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT)) {
            RECT focus = rect;
            InflateRect(&focus, -3, -3);
            DrawFocusRect(dc, &focus);
        }
    } else if (id == kSecurityButton) {
        FillRect(dc, &rect, backgroundBrush_);
        FillRounded(dc, rect, hot ? theme_.hover : theme_.pressed, radius);
        OutlineRounded(dc, rect, theme_.divider, radius);
        SetBkMode(dc, TRANSPARENT);
        RECT inner{rect.left + Scale(10), rect.top, rect.right - Scale(12), rect.bottom};
        HGDIOBJ previous = SelectObject(dc, textFont_);
        SetTextColor(dc, theme_.text);
        DrawTextW(dc, SecurityName(security_).c_str(), -1, &inner, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, iconFont_);
        SetTextColor(dc, theme_.secondaryText);
        DrawTextW(dc, icons::kChevronDown, -1, &inner, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, previous);
        if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT)) {
            RECT focus = rect;
            InflateRect(&focus, -3, -3);
            DrawFocusRect(dc, &focus);
        }
    } else if (id == kHiddenToggle) {
        // Switch: a pill with a knob, accent when on.
        FillRect(dc, &rect, backgroundBrush_);
        const int height = rect.bottom - rect.top;
        if (hidden_) {
            FillRounded(dc, rect, theme_.accent, height / 2);
        } else {
            if (hot) FillRounded(dc, rect, theme_.hover, height / 2);
            OutlineRounded(dc, rect, theme_.secondaryText, height / 2);
        }
        const int knob = Scale(12);
        const int knobX = hidden_ ? rect.right - Scale(4) - knob : rect.left + Scale(4);
        RECT knobRect{knobX, rect.top + (height - knob) / 2, knobX + knob, rect.top + (height + knob) / 2};
        FillRounded(dc, knobRect, hidden_ ? theme_.accentText : theme_.secondaryText, knob / 2);
    } else if (id == kCopyButton) {
        DrawButton(item, theme_, fonts, ButtonStyle::Primary, copied_ ? icons::kCheck : icons::kPicture,
                   copied_ ? L"Copied" : L"Copy image", radius);
    } else if (id == kSaveButton) {
        DrawButton(item, theme_, fonts, ButtonStyle::Standard, icons::kSave, L"Save PNG", radius);
    }
}

void GeneratorWindow::SetMode(Mode mode) {
    mode_ = mode;
    for (int i = 0; i < static_cast<int>(kModeNames.size()); ++i) InvalidateRect(GetDlgItem(window_, kModeFirst + i), nullptr, FALSE);
    Layout();
    UpdateCode();
}

void GeneratorWindow::ShowSecurityMenu() {
    static constexpr const wchar_t* kValues[] = {L"WPA", L"WEP", L""};
    HMENU menu = CreatePopupMenu();
    for (int i = 0; i < 3; ++i) {
        AppendMenuW(menu, MF_STRING | (security_ == kValues[i] ? MF_CHECKED : 0), i + 1, SecurityName(kValues[i]).c_str());
    }
    RECT button;
    GetWindowRect(GetDlgItem(window_, kSecurityButton), &button);
    const int chosen = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, button.left, button.bottom, window_, nullptr);
    DestroyMenu(menu);
    if (chosen > 0) {
        security_ = kValues[chosen - 1];
        Layout();
        UpdateCode();
    }
}

std::wstring GeneratorWindow::Payload() const {
    const auto text = [&](int id) { return ControlText(window_, id); };
    switch (mode_) {
        case Mode::Text:
            return ToUnixLineBreaks(text(kTextEdit));
        case Mode::Wifi:
            if (text(kWifiSsid).empty()) return {};
            return BuildWifi({text(kWifiSsid), security_.empty() ? L"" : text(kWifiPassword), security_, hidden_});
        case Mode::Contact: {
            Contact contact{text(kContactName), text(kContactPhone), text(kContactEmail), text(kContactOrganization), text(kContactWebsite)};
            if (!HasVisibleText(contact.name + contact.phone + contact.email + contact.organization + contact.website)) return {};
            return BuildContact(contact);
        }
        case Mode::Email:
            if (!HasVisibleText(text(kEmailTo))) return {};
            return BuildEmail({text(kEmailTo), text(kEmailSubject), text(kEmailBody)});
    }
    return {};
}

void GeneratorWindow::UpdateCode() {
    const std::wstring payload = Payload();
    code_ = HasVisibleText(payload) ? EncodeQrCode(payload) : std::nullopt;
    if (code_) {
        SetStatus(std::to_wstring(code_->size) + L" × " + std::to_wstring(code_->size) + L" modules", false);
    } else if (HasVisibleText(payload)) {
        SetStatus(L"Too much text for one QR code. Shorten it a little.", true);
    } else {
        static constexpr const wchar_t* kHints[] = {L"Type or paste text to create a code.", L"Enter the network name.",
                                                    L"Fill in at least one field.", L"Enter an email address."};
        SetStatus(kHints[static_cast<int>(mode_)], false);
    }
    EnableWindow(GetDlgItem(window_, kCopyButton), code_.has_value());
    EnableWindow(GetDlgItem(window_, kSaveButton), code_.has_value());
    InvalidateRect(window_, &preview_, FALSE);
}

void GeneratorWindow::SetStatus(const std::wstring& status, bool error) {
    status_ = status;
    statusIsError_ = error;
    InvalidateRect(window_, &statusRect_, FALSE);
}

void GeneratorWindow::CopyImage() {
    if (!code_) return;
    // Large enough to stay sharp when pasted into documents or chats.
    const BgraImage image = RenderQrCode(*code_, std::max(4, ModuleSizeFor(*code_, 600)));
    if (!SetClipboardImage(window_, image)) {
        SetStatus(L"Couldn't access the clipboard. Try again.", true);
        return;
    }
    copied_ = true;
    InvalidateRect(GetDlgItem(window_, kCopyButton), nullptr, FALSE);
    SetTimer(window_, kCopiedTimer, 1500, nullptr);
    if (onCreated) onCreated(Payload());
}

void GeneratorWindow::SavePng() {
    if (!code_) return;
    wchar_t file[MAX_PATH] = L"QR code.png";
    const std::wstring folder = PicturesDirectory().wstring();
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner = window_;
    dialog.lpstrFilter = L"PNG image (*.png)\0*.png\0";
    dialog.lpstrFile = file;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrInitialDir = folder.empty() ? nullptr : folder.c_str();
    dialog.lpstrDefExt = L"png";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&dialog)) return;
    const BgraImage image = RenderQrCode(*code_, std::max(4, ModuleSizeFor(*code_, 1024)));
    if (!kodo::SavePng(image, file)) {
        SetStatus(L"Couldn't save the image there. Try another folder.", true);
        return;
    }
    SetStatus(L"Saved as " + std::filesystem::path(file).filename().wstring(), false);
    if (onCreated) onCreated(Payload());
}

void GeneratorWindow::Show(Mode mode, const std::optional<std::wstring>& text) {
    const bool firstShow = !IsWindowVisible(window_) && labels_.empty();
    if (text) SetDlgItemTextW(window_, kTextEdit, ToWindowsLineBreaks(*text).c_str());
    RefreshStyle();
    SetMode(mode);
    if (firstShow) {
        // Open on the monitor with the mouse, roughly centered.
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
    }
    ShowWindow(window_, IsIconic(window_) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(window_);
    HWND first = GetDlgItem(window_, FieldsOf(mode_).front().id);
    SetFocus(first);
    SendMessageW(first, EM_SETSEL, static_cast<WPARAM>(-1), -1);
    SendMessageW(window_, WM_CHANGEUISTATE, MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS), 0);
}

void GeneratorWindow::Hide() {
    if (window_) ShowWindow(window_, SW_HIDE);
}

LRESULT CALLBACK GeneratorWindow::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* self = static_cast<GeneratorWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<GeneratorWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    return self ? self->HandleMessage(message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT GeneratorWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            const int code = HIWORD(wParam);
            if (id >= kTextEdit && id <= kEmailBody) {
                if (code == EN_CHANGE) UpdateCode();
                if (code == EN_SETFOCUS || code == EN_KILLFOCUS) InvalidateRect(window_, nullptr, FALSE);
            } else if (id >= kModeFirst && id < kModeFirst + static_cast<int>(kModeNames.size())) {
                SetMode(static_cast<Mode>(id - kModeFirst));
            } else if (id == kSecurityButton) {
                ShowSecurityMenu();
            } else if (id == kHiddenToggle) {
                hidden_ = !hidden_;
                InvalidateRect(GetDlgItem(window_, id), nullptr, FALSE);
                UpdateCode();
            } else if (id == kCopyButton) {
                CopyImage();
            } else if (id == kSaveButton) {
                SavePng();
            } else if (id == IDCANCEL) {
                Hide();
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
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            // Read-only or disabled edits come through WM_CTLCOLORSTATIC.
            const bool enabled = IsWindowEnabled(reinterpret_cast<HWND>(lParam));
            SetBkColor(reinterpret_cast<HDC>(wParam), enabled ? theme_.pressed : theme_.background);
            SetTextColor(reinterpret_cast<HDC>(wParam), enabled ? theme_.text : theme_.secondaryText);
            return reinterpret_cast<LRESULT>(enabled ? fieldBrush_ : backgroundBrush_);
        }
        case WM_TIMER:
            KillTimer(window_, wParam);
            copied_ = false;
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
