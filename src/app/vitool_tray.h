// Icono del área de notificaciones que se puede agrupar dentro del de Vitool Box.
// vitool_tray::NotifyIcon sustituye a Shell_NotifyIconW: si Vitool Box tiene
// activado "Agrupar los iconos", el icono se muestra dentro del suyo; si no, o
// si Vitool Box se cierra, vuelve al área de notificaciones como siempre.
// Es el mismo archivo en Vitool Box y en todas las apps.
#pragma once

#include <windows.h>
#include <shellapi.h>

namespace vitool_tray {

inline constexpr wchar_t kHostClass[] = L"VitomDev.VitoolBox.TrayGroup";
inline constexpr wchar_t kChangedMessage[] = L"VitomDev.VitoolBox.TrayGroupChanged";
inline constexpr ULONG_PTR kCopyDataId = 0x56544731;

struct IconMessage {
    DWORD command;
    DWORD processId;
    UINT64 window;
    UINT id;
    UINT callbackMessage;
    UINT64 icon;
    UINT64 balloonIcon;
    UINT flags;
    DWORD infoFlags;
    wchar_t tip[128];
    wchar_t infoTitle[64];
    wchar_t info[256];
};

namespace detail {

struct State {
    NOTIFYICONDATAW data{};
    UINT addFlags = 0;
    bool added = false;
    bool grouped = false;
    UINT changedMessage = 0;
    HANDLE hostProcess = nullptr;
    HANDLE hostWait = nullptr;
};

inline State& Get() {
    static State state;
    return state;
}

inline UINT ChangedMessage() {
    auto& state = Get();
    if (!state.changedMessage) state.changedMessage = RegisterWindowMessageW(kChangedMessage);
    return state.changedMessage;
}

inline void StopWatching() {
    auto& state = Get();
    if (state.hostWait) UnregisterWaitEx(state.hostWait, INVALID_HANDLE_VALUE);
    if (state.hostProcess) CloseHandle(state.hostProcess);
    state.hostWait = state.hostProcess = nullptr;
}

// Si Vitool Box se cierra de golpe (sin avisar), el icono tiene que volver.
inline void WatchHost(HWND host, HWND window) {
    auto& state = Get();
    if (state.hostWait) return;
    DWORD processId = 0;
    GetWindowThreadProcessId(host, &processId);
    state.hostProcess = OpenProcess(SYNCHRONIZE, FALSE, processId);
    if (!state.hostProcess) return;
    RegisterWaitForSingleObject(&state.hostWait, state.hostProcess,
        [](PVOID window, BOOLEAN) { PostMessageW(static_cast<HWND>(window), ChangedMessage(), 0, 0); },
        window, INFINITE, WT_EXECUTEONLYONCE);
}

inline bool SendToHost(DWORD command, const NOTIFYICONDATAW& data, UINT flags) {
    HWND host = FindWindowW(kHostClass, nullptr);
    if (!host) return false;
    IconMessage message{};
    message.command = command;
    message.processId = GetCurrentProcessId();
    message.window = reinterpret_cast<UINT64>(data.hWnd);
    message.id = data.uID;
    message.callbackMessage = Get().data.uCallbackMessage;
    message.icon = reinterpret_cast<UINT64>(Get().data.hIcon);
    message.flags = flags;
    wcsncpy_s(message.tip, Get().data.szTip, _TRUNCATE);
    if (flags & NIF_INFO) {
        message.infoFlags = data.dwInfoFlags;
        message.balloonIcon = reinterpret_cast<UINT64>(data.hBalloonIcon);
        wcsncpy_s(message.infoTitle, data.szInfoTitle, _TRUNCATE);
        wcsncpy_s(message.info, data.szInfo, _TRUNCATE);
    }
    COPYDATASTRUCT copy{kCopyDataId, sizeof(message), &message};
    DWORD_PTR result = 0;
    if (!SendMessageTimeoutW(host, WM_COPYDATA, reinterpret_cast<WPARAM>(data.hWnd), reinterpret_cast<LPARAM>(&copy),
                             SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &result) || result != 1) {
        return false;
    }
    if (command == NIM_ADD) WatchHost(host, data.hWnd);
    return true;
}

inline void Ungroup() {
    auto& state = Get();
    StopWatching();
    if (!state.grouped) return;
    state.grouped = false;
    NOTIFYICONDATAW data = state.data;
    data.uFlags = state.addFlags;
    Shell_NotifyIconW(NIM_ADD, &data);
    if (data.uVersion) Shell_NotifyIconW(NIM_SETVERSION, &data);
}

inline void Group() {
    auto& state = Get();
    if (state.grouped || !SendToHost(NIM_ADD, state.data, state.addFlags)) return;
    NOTIFYICONDATAW data = state.data;
    Shell_NotifyIconW(NIM_DELETE, &data);
    state.grouped = true;
}

}  // namespace detail

inline BOOL NotifyIcon(DWORD command, NOTIFYICONDATAW* data) {
    auto& state = detail::Get();
    switch (command) {
        case NIM_ADD: {
            const UINT version = state.data.uVersion;
            state.data = *data;
            state.data.uVersion = version;
            state.addFlags = data->uFlags & ~NIF_INFO;
            state.added = true;
            ChangeWindowMessageFilterEx(data->hWnd, detail::ChangedMessage(), MSGFLT_ALLOW, nullptr);
            ChangeWindowMessageFilterEx(data->hWnd, data->uCallbackMessage, MSGFLT_ALLOW, nullptr);
            state.grouped = detail::SendToHost(NIM_ADD, state.data, state.addFlags);
            return state.grouped ? TRUE : Shell_NotifyIconW(NIM_ADD, data);
        }
        case NIM_SETVERSION:
            state.data.uVersion = data->uVersion;
            return state.grouped ? TRUE : Shell_NotifyIconW(command, data);
        case NIM_MODIFY:
            if (data->uFlags & NIF_ICON) state.data.hIcon = data->hIcon;
            if (data->uFlags & NIF_TIP) wcsncpy_s(state.data.szTip, data->szTip, _TRUNCATE);
            if (data->uFlags & NIF_MESSAGE) state.data.uCallbackMessage = data->uCallbackMessage;
            // Al volver al área de notificaciones hay que reponer también lo que llegó después del alta.
            state.addFlags |= data->uFlags & (NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_SHOWTIP);
            if (state.grouped) {
                if (detail::SendToHost(NIM_MODIFY, *data, data->uFlags)) return TRUE;
                detail::Ungroup();
            }
            return Shell_NotifyIconW(command, data);
        case NIM_DELETE: {
            const bool grouped = state.grouped;
            state.added = state.grouped = false;
            detail::StopWatching();
            if (grouped && detail::SendToHost(NIM_DELETE, *data, 0)) return TRUE;
            return Shell_NotifyIconW(command, data);
        }
    }
    return Shell_NotifyIconW(command, data);
}

// Va al principio del procedimiento de la ventana del icono: true si era
// Vitool Box activando o desactivando la agrupación.
inline bool HandleMessage(UINT message, WPARAM wParam) {
    if (message != detail::ChangedMessage()) return false;
    if (detail::Get().added) {
        if (wParam) detail::Group();
        else detail::Ungroup();
    }
    return true;
}

}  // namespace vitool_tray
