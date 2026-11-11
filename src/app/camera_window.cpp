#include "app/camera_window.h"

#include "app/resource.h"
#include "core/qr.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <shellapi.h>

#include <algorithm>
#include <cstring>
#include <memory>

namespace kodo {

namespace {

constexpr wchar_t kClassName[] = L"KodoCamera";

constexpr UINT WM_APP_FRAME = WM_APP + 1;
constexpr UINT WM_APP_FOUND = WM_APP + 2;
constexpr UINT WM_APP_STATUS = WM_APP + 3;  // lParam: std::wstring*, wParam: 1 for an error

enum ControlId { kPrivacyButton = 100, kMirrorButton, kCameraButton };

// Layout in 96-DPI pixels.
constexpr int kPadding = 20;
constexpr int kVideoWidth = 640;
constexpr int kVideoHeight = 360;
constexpr int kButtonHeight = 32;

// Decoding every frame would keep a core busy for nothing.
constexpr DWORD kDecodeInterval = 150;

template <class T>
struct Com {
    T* p = nullptr;
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    ~Com() {
        if (p) p->Release();
    }
    T** operator&() { return &p; }
    T* operator->() const { return p; }
};

std::wstring StringAttribute(IMFActivate* device, REFGUID key) {
    WCHAR* value = nullptr;
    UINT32 length = 0;
    if (FAILED(device->GetAllocatedString(key, &value, &length))) return {};
    std::wstring text(value, length);
    CoTaskMemFree(value);
    return text;
}

// Video capture devices, the system default first. The caller releases them.
std::vector<IMFActivate*> FindCameras() {
    Com<IMFAttributes> query;
    IMFActivate** devices = nullptr;
    UINT32 count = 0;
    std::vector<IMFActivate*> result;
    if (SUCCEEDED(MFCreateAttributes(&query, 1)) &&
        SUCCEEDED(query->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID)) &&
        SUCCEEDED(MFEnumDeviceSources(query.p, &devices, &count))) {
        result.assign(devices, devices + count);
    }
    CoTaskMemFree(devices);
    return result;
}

void PostStatus(HWND window, const std::wstring& status, bool error) {
    PostMessageW(window, WM_APP_STATUS, error ? 1 : 0, reinterpret_cast<LPARAM>(new std::wstring(status)));
}

}  // namespace

bool CameraWindow::Create(HINSTANCE instance) {
    instance_ = instance;
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_KODO));
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);
    window_ = CreateWindowExW(WS_EX_CONTROLPARENT | WS_EX_APPWINDOW, kClassName, L"Read with camera – Kodo",
                              WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN, CW_USEDEFAULT,
                              CW_USEDEFAULT, 100, 100, nullptr, nullptr, instance, this);
    if (!window_) return false;
    for (int id : {kPrivacyButton, kMirrorButton, kCameraButton}) {
        HWND button = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_TABSTOP | BS_OWNERDRAW | (id == kMirrorButton ? WS_VISIBLE : 0),
                                      0, 0, 0, 0, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
        EnableHover(button);
    }
    return true;
}

void CameraWindow::Destroy() {
    Stop();
    if (window_) DestroyWindow(window_);
    window_ = nullptr;
    for (HGDIOBJ object : {static_cast<HGDIOBJ>(backgroundBrush_), static_cast<HGDIOBJ>(titleFont_),
                           static_cast<HGDIOBJ>(textFont_), static_cast<HGDIOBJ>(iconFont_)}) {
        if (object) DeleteObject(object);
    }
    backgroundBrush_ = nullptr;
    titleFont_ = textFont_ = iconFont_ = nullptr;
}

int CameraWindow::Scale(int value) const { return MulDiv(value, GetDpiForWindow(window_), 96); }

void CameraWindow::RefreshStyle() {
    theme_ = Theme::Current();
    if (backgroundBrush_) DeleteObject(backgroundBrush_);
    backgroundBrush_ = CreateSolidBrush(theme_.background);
    for (HFONT font : {titleFont_, textFont_, iconFont_}) {
        if (font) DeleteObject(font);
    }
    const UINT dpi = GetDpiForWindow(window_);
    titleFont_ = CreateUiFont(dpi, 16, FW_SEMIBOLD);
    textFont_ = CreateUiFont(dpi, 10);
    iconFont_ = CreateIconFont(dpi, 10);
    ApplyWindowStyle(window_, theme_);
}

void CameraWindow::Layout() {
    const int padding = Scale(kPadding);
    int y = padding;
    titleRect_ = {padding, y, padding + Scale(kVideoWidth), y + Scale(32)};
    // Options on the title row: the camera list only when there is a choice.
    const ButtonFonts optionFonts{textFont_, iconFont_};
    const UINT optionDpi = GetDpiForWindow(window_);
    const int mirrorWidth = ButtonWidth(window_, optionFonts, icons::kCheck, L"Mirror", optionDpi);
    int right = padding + Scale(kVideoWidth);
    SetWindowPos(GetDlgItem(window_, kMirrorButton), nullptr, right - mirrorWidth, y, mirrorWidth, Scale(kButtonHeight),
                 SWP_NOZORDER | SWP_NOACTIVATE);
    right -= mirrorWidth + Scale(8);
    HWND cameraButton = GetDlgItem(window_, kCameraButton);
    ShowWindow(cameraButton, devices_.size() > 1 ? SW_SHOW : SW_HIDE);
    if (devices_.size() > 1) {
        const int cameraWidth = std::min(Scale(260), ButtonWidth(window_, optionFonts, icons::kChevronDown, CameraName().c_str(), optionDpi));
        SetWindowPos(cameraButton, nullptr, right - cameraWidth, y, cameraWidth, Scale(kButtonHeight), SWP_NOZORDER | SWP_NOACTIVATE);
    }
    y += Scale(32) + Scale(12);
    video_ = {padding, y, padding + Scale(kVideoWidth), y + Scale(kVideoHeight)};
    y = video_.bottom + Scale(12);
    statusRect_ = {padding, y, video_.right, y + Scale(kButtonHeight)};
    const ButtonFonts fonts{textFont_, iconFont_};
    const int buttonWidth = ButtonWidth(window_, fonts, nullptr, L"Camera settings", GetDpiForWindow(window_));
    SetWindowPos(GetDlgItem(window_, kPrivacyButton), nullptr, video_.right - buttonWidth, y, buttonWidth, Scale(kButtonHeight),
                 SWP_NOZORDER | SWP_NOACTIVATE);
    y += Scale(kButtonHeight) + padding;

    RECT frame{0, 0, video_.right + padding, y};
    AdjustWindowRectExForDpi(&frame, GetWindowLongW(window_, GWL_STYLE), FALSE, GetWindowLongW(window_, GWL_EXSTYLE),
                             GetDpiForWindow(window_));
    SetWindowPos(window_, nullptr, 0, 0, frame.right - frame.left, frame.bottom - frame.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    laidOut_ = true;
    InvalidateRect(window_, nullptr, TRUE);
}

void CameraWindow::Paint() {
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(window_, &paint);
    RECT client;
    GetClientRect(window_, &client);
    HDC buffer = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
    HGDIOBJ previousBitmap = SelectObject(buffer, bitmap);
    FillRect(buffer, &client, backgroundBrush_);
    SetBkMode(buffer, TRANSPARENT);

    HGDIOBJ previousFont = SelectObject(buffer, titleFont_);
    SetTextColor(buffer, theme_.text);
    DrawTextW(buffer, L"Read with camera", -1, &titleRect_, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // Black video area; the frame keeps its proportions and, mirrored like a
    // selfie view, moving the phone feels natural.
    FillRounded(buffer, video_, RGB(0, 0, 0), Scale(8));
    {
        std::lock_guard lock(frameLock_);
        if (!frame_.empty()) {
            const int boxWidth = video_.right - video_.left, boxHeight = video_.bottom - video_.top;
            const double scale = std::min(static_cast<double>(boxWidth) / frame_.width, static_cast<double>(boxHeight) / frame_.height);
            const int width = static_cast<int>(frame_.width * scale), height = static_cast<int>(frame_.height * scale);
            const int x = video_.left + (boxWidth - width) / 2, y = video_.top + (boxHeight - height) / 2;
            BITMAPINFO info{};
            info.bmiHeader = {sizeof(BITMAPINFOHEADER), frame_.width, -frame_.height, 1, 32, BI_RGB};
            SetStretchBltMode(buffer, HALFTONE);
            StretchDIBits(buffer, mirror_ ? x + width - 1 : x, y, mirror_ ? -width : width, height, 0, 0, frame_.width,
                          frame_.height, frame_.pixels.data(), &info, DIB_RGB_COLORS, SRCCOPY);
        }
        if (flash_) OutlineRounded(buffer, video_, RGB(16, 185, 129), Scale(8), Scale(5));
    }

    SelectObject(buffer, textFont_);
    SetTextColor(buffer, statusIsError_ ? RGB(232, 72, 85) : theme_.secondaryText);
    RECT status = statusRect_;
    if (IsWindowVisible(GetDlgItem(window_, kPrivacyButton))) status.right -= Scale(180);
    DrawTextW(buffer, status_.c_str(), -1, &status, DT_LEFT | DT_VCENTER | DT_WORDBREAK | DT_NOPREFIX);

    BitBlt(dc, 0, 0, client.right, client.bottom, buffer, 0, 0, SRCCOPY);
    SelectObject(buffer, previousFont);
    SelectObject(buffer, previousBitmap);
    DeleteObject(bitmap);
    DeleteDC(buffer);
    EndPaint(window_, &paint);
}

std::wstring CameraWindow::CameraName() const {
    for (const auto& device : devices_) {
        if (device.id == camera_) return device.name;
    }
    return devices_.empty() ? L"" : devices_.front().name;
}

void CameraWindow::Draw(const DRAWITEMSTRUCT& item) {
    const ButtonFonts fonts{textFont_, iconFont_};
    switch (item.CtlID) {
        case kPrivacyButton:
            DrawButton(item, theme_, fonts, ButtonStyle::Standard, nullptr, L"Camera settings", Scale(4));
            break;
        case kMirrorButton:
            DrawButton(item, theme_, fonts, ButtonStyle::Standard, mirror_ ? icons::kCheck : nullptr, L"Mirror", Scale(4), theme_.accent);
            break;
        case kCameraButton:
            DrawButton(item, theme_, fonts, ButtonStyle::Standard, icons::kChevronDown, CameraName().c_str(), Scale(4));
            break;
    }
}

void CameraWindow::SetOptions(const std::wstring& camera, bool mirror) {
    camera_ = camera;
    mirror_ = mirror;
}

void CameraWindow::ShowCameraMenu() {
    HMENU menu = CreatePopupMenu();
    const std::wstring current = CameraName();
    for (size_t i = 0; i < devices_.size(); ++i) {
        AppendMenuW(menu, MF_STRING | (devices_[i].name == current ? MF_CHECKED : 0), i + 1, devices_[i].name.c_str());
    }
    RECT button;
    GetWindowRect(GetDlgItem(window_, kCameraButton), &button);
    const int chosen = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_TOPALIGN, button.right, button.bottom, window_, nullptr);
    DestroyMenu(menu);
    if (chosen <= 0 || devices_[chosen - 1].id == camera_) return;
    camera_ = devices_[chosen - 1].id;
    if (onOptionsChanged) onOptionsChanged(camera_, mirror_);
    Layout();
    Start();
}

void CameraWindow::SetStatus(const std::wstring& status, bool error) {
    status_ = status;
    statusIsError_ = error;
    // Windows' privacy settings are the usual reason a camera cannot be opened.
    ShowWindow(GetDlgItem(window_, kPrivacyButton), error ? SW_SHOW : SW_HIDE);
    InvalidateRect(window_, &statusRect_, FALSE);
}

// Runs on the worker thread: opens the default camera and reads frames
// until a code is found or the window closes.
void CameraWindow::Capture() {
    HWND window = window_;
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return;
    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) {
        PostStatus(window, L"The camera could not be started.", true);
        CoUninitialize();
        return;
    }
    const std::wstring wanted = camera_;
    [&] {
        std::vector<IMFActivate*> devices = FindCameras();
        if (devices.empty()) {
            PostStatus(window, L"No camera was found on this PC.", true);
            return;
        }
        // The chosen camera if it is still connected, otherwise the default one.
        IMFActivate* device = devices.front();
        for (IMFActivate* candidate : devices) {
            if (StringAttribute(candidate, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK) == wanted) device = candidate;
        }
        Com<IMFMediaSource> source;
        const HRESULT activated = device->ActivateObject(IID_PPV_ARGS(&source));
        for (IMFActivate* candidate : devices) candidate->Release();
        if (FAILED(activated)) {
            PostStatus(window,
                       activated == E_ACCESSDENIED ? L"Windows is blocking the camera. Allow desktop apps to use it in Camera settings."
                                                   : L"The camera is not available. Another app may be using it.",
                       true);
            return;
        }

        Com<IMFAttributes> options;
        Com<IMFSourceReader> reader;
        Com<IMFMediaType> type;
        MFCreateAttributes(&options, 1);
        options->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
        if (FAILED(MFCreateSourceReaderFromMediaSource(source.p, options.p, &reader)) || FAILED(MFCreateMediaType(&type)) ||
            FAILED(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) || FAILED(type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32)) ||
            FAILED(reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, type.p))) {
            source->Shutdown();
            PostStatus(window, L"The camera could not be started.", true);
            return;
        }
        Com<IMFMediaType> current;
        reader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &current);
        UINT32 width = 0, height = 0;
        MFGetAttributeSize(current.p, MF_MT_FRAME_SIZE, &width, &height);
        // A negative stride means the rows come bottom-up.
        INT32 stride = static_cast<INT32>(width * 4);
        current->GetUINT32(MF_MT_DEFAULT_STRIDE, reinterpret_cast<UINT32*>(&stride));
        PostStatus(window, L"Hold the QR code up to the camera.", false);

        DWORD lastDecode = 0;
        while (!stop_) {
            DWORD flags = 0;
            Com<IMFSample> sample;
            const HRESULT read = reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, nullptr, &flags, nullptr, &sample);
            if (FAILED(read) || (flags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_ENDOFSTREAM))) {
                if (!stop_) PostStatus(window, L"The camera stopped. Close this window and try again.", true);
                break;
            }
            if (!sample.p) continue;
            Com<IMFMediaBuffer> buffer;
            BYTE* data = nullptr;
            DWORD length = 0;
            if (FAILED(sample->ConvertToContiguousBuffer(&buffer)) || FAILED(buffer->Lock(&data, nullptr, &length))) continue;
            BgraImage image;
            image.width = static_cast<int>(width);
            image.height = static_cast<int>(height);
            image.pixels.resize(static_cast<size_t>(width) * height * 4);
            const size_t rowBytes = static_cast<size_t>(width) * 4;
            if (length >= rowBytes * height) {
                for (UINT32 row = 0; row < height; ++row) {
                    const BYTE* from = stride >= 0 ? data + static_cast<size_t>(row) * stride
                                                     : data + static_cast<size_t>(height - 1 - row) * rowBytes;
                    std::memcpy(&image.pixels[row * rowBytes], from, rowBytes);
                }
            }
            buffer->Unlock();
            for (size_t i = 3; i < image.pixels.size(); i += 4) image.pixels[i] = 255;

            std::vector<std::wstring> texts;
            if (GetTickCount() - lastDecode >= kDecodeInterval) {
                lastDecode = GetTickCount();
                texts = DecodeQrCodes(image);
            }
            const bool hit = !texts.empty();
            {
                std::lock_guard lock(frameLock_);
                frame_ = std::move(image);
                if (hit) found_ = std::move(texts);
            }
            PostMessageW(window, hit ? WM_APP_FOUND : WM_APP_FRAME, 0, 0);
            if (hit) break;
        }
        source->Shutdown();
    }();
    MFShutdown();
    CoUninitialize();
}

void CameraWindow::Start() {
    Stop();
    stop_ = false;
    {
        std::lock_guard lock(frameLock_);
        frame_ = {};
        found_.clear();
    }
    SetStatus(L"Starting the camera…", false);
    worker_ = std::thread([this] { Capture(); });
}

void CameraWindow::Stop() {
    stop_ = true;
    if (worker_.joinable()) worker_.join();
    std::lock_guard lock(frameLock_);
    // Nothing the camera saw is kept once the window closes.
    Wipe(frame_.pixels);
    frame_ = {};
}

void CameraWindow::Show() {
    // Cameras come and go (USB); list them each time the window opens.
    devices_.clear();
    if (SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) {
        for (IMFActivate* device : FindCameras()) {
            devices_.push_back({StringAttribute(device, MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME),
                                StringAttribute(device, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK)});
            device->Release();
        }
        MFShutdown();
    }
    if (!laidOut_) {
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
    } else {
        RefreshStyle();
        Layout();
    }
    if (!worker_.joinable() || !IsWindowVisible(window_)) Start();
    ShowWindow(window_, IsIconic(window_) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(window_);
}

void CameraWindow::Hide() {
    if (!window_) return;
    ShowWindow(window_, SW_HIDE);
    Stop();
}

LRESULT CALLBACK CameraWindow::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* self = static_cast<CameraWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<CameraWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    return self ? self->HandleMessage(message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT CameraWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_APP_FRAME:
            InvalidateRect(window_, &video_, FALSE);
            return 0;
        case WM_APP_FOUND: {
            std::vector<std::wstring> texts;
            {
                std::lock_guard lock(frameLock_);
                texts = std::move(found_);
                found_.clear();
            }
            if (texts.empty()) return 0;
            // A green frame for a moment confirms the read before the window closes.
            pending_ = std::move(texts);
            flash_ = true;
            InvalidateRect(window_, &video_, FALSE);
            SetTimer(window_, 1, 350, nullptr);
            return 0;
        }
        case WM_TIMER: {
            KillTimer(window_, wParam);
            flash_ = false;
            RECT bounds;
            GetWindowRect(window_, &bounds);
            Hide();
            if (onFound) onFound(std::move(pending_), bounds);
            pending_.clear();
            return 0;
        }
        case WM_APP_STATUS: {
            std::unique_ptr<std::wstring> status(reinterpret_cast<std::wstring*>(lParam));
            if (IsWindowVisible(window_)) SetStatus(*status, wParam != 0);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == kMirrorButton) {
                mirror_ = !mirror_;
                InvalidateRect(window_, nullptr, FALSE);
                InvalidateRect(GetDlgItem(window_, kMirrorButton), nullptr, FALSE);
                if (onOptionsChanged) onOptionsChanged(camera_, mirror_);
            } else if (LOWORD(wParam) == kCameraButton) {
                ShowCameraMenu();
            } else if (LOWORD(wParam) == kPrivacyButton) {
                ShellExecuteW(nullptr, L"open", L"ms-settings:privacy-webcam", nullptr, nullptr, SW_SHOWNORMAL);
            } else if (LOWORD(wParam) == IDCANCEL) {
                Hide();
            }
            return 0;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) Hide();
            return 0;
        case WM_DRAWITEM:
            Draw(*reinterpret_cast<const DRAWITEMSTRUCT*>(lParam));
            return TRUE;
        case WM_PAINT:
            Paint();
            return 0;
        case WM_ERASEBKGND:
            return 1;
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
