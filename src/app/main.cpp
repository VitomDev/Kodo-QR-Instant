#include "app/tray_app.h"

#include <commctrl.h>
#include <objbase.h>

#include <cwchar>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int) {
    // "--background" is used by the Start with Windows entry: start silently.
    const bool startedAtSignIn = wcsstr(commandLine, L"--background") != nullptr;

    // One instance per user session. A second launch only tells the running
    // one, which explains where Kodo is.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\KodoQRInstant.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND running = FindWindowW(kodo::kTrayWindowClass, nullptr); running && !startedAtSignIn) {
            PostMessageW(running, kodo::WM_APP_ALREADY_RUNNING, 0, 0);
        }
        return 0;
    }

    // PNG encoding (WIC) and the Save dialog are COM.
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) return 1;
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);

    int exitCode = 0;
    {
        kodo::TrayApp app;
        if (app.Initialize(instance, startedAtSignIn)) {
            MSG message;
            while (GetMessageW(&message, nullptr, 0, 0) > 0) {
                if (app.PreTranslate(message)) continue;
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        } else {
            exitCode = 1;
        }
    }
    CoUninitialize();
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return exitCode;
}
