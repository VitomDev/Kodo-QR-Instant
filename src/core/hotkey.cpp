#include "core/hotkey.h"

#include <commctrl.h>

namespace kodo {

namespace {

constexpr UINT kSupportedModifiers = MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN;

std::wstring KeyName(UINT key) {
    if ((key >= '0' && key <= '9') || (key >= 'A' && key <= 'Z')) return std::wstring(1, static_cast<wchar_t>(key));
    if (key >= VK_F1 && key <= VK_F24) return L"F" + std::to_wstring(key - VK_F1 + 1);

    // Extended keys need the extended bit or GetKeyNameText names the numpad twin.
    UINT scanCode = MapVirtualKeyW(key, MAPVK_VK_TO_VSC);
    switch (key) {
        case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
        case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_DIVIDE: case VK_SNAPSHOT:
            scanCode |= 0x100;
            break;
    }
    wchar_t name[64]{};
    if (scanCode && GetKeyNameTextW(static_cast<LONG>(scanCode << 16), name, 64) > 0) return name;
    return L"Key " + std::to_wstring(key);
}

}  // namespace

bool Hotkey::IsValid() const {
    // A shortcut without modifiers would steal a key from every application.
    return key != 0 && (modifiers & kSupportedModifiers) != 0 && (modifiers & ~kSupportedModifiers & ~MOD_NOREPEAT) == 0;
}

std::wstring ToString(const Hotkey& hotkey) {
    std::wstring text;
    if (hotkey.modifiers & MOD_CONTROL) text += L"Ctrl + ";
    if (hotkey.modifiers & MOD_ALT) text += L"Alt + ";
    if (hotkey.modifiers & MOD_SHIFT) text += L"Shift + ";
    if (hotkey.modifiers & MOD_WIN) text += L"Win + ";
    return text + KeyName(hotkey.key);
}

DWORD Pack(const Hotkey& hotkey) { return MAKELONG(hotkey.key, hotkey.modifiers); }

Hotkey Unpack(DWORD value) { return {HIWORD(value), LOWORD(value)}; }

Hotkey FromHotkeyControl(WORD value) {
    const BYTE flags = HIBYTE(value);
    Hotkey hotkey{0, LOBYTE(value)};
    if (flags & HOTKEYF_CONTROL) hotkey.modifiers |= MOD_CONTROL;
    if (flags & HOTKEYF_ALT) hotkey.modifiers |= MOD_ALT;
    if (flags & HOTKEYF_SHIFT) hotkey.modifiers |= MOD_SHIFT;
    return hotkey;
}

WORD ToHotkeyControl(const Hotkey& hotkey) {
    BYTE flags = 0;
    if (hotkey.modifiers & MOD_CONTROL) flags |= HOTKEYF_CONTROL;
    if (hotkey.modifiers & MOD_ALT) flags |= HOTKEYF_ALT;
    if (hotkey.modifiers & MOD_SHIFT) flags |= HOTKEYF_SHIFT;
    return MAKEWORD(static_cast<BYTE>(hotkey.key), flags);
}

}  // namespace kodo
