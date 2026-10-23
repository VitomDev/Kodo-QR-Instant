#include "core/settings.h"

namespace kodo {

namespace {

bool ReadDword(HKEY key, const wchar_t* name, DWORD& value) {
    DWORD size = sizeof(value);
    return RegGetValueW(key, nullptr, name, RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS;
}

bool WriteDword(HKEY key, const wchar_t* name, DWORD value) {
    return RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value)) == ERROR_SUCCESS;
}

}  // namespace

Settings Settings::Load(const wchar_t* keyPath) {
    Settings settings;
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, keyPath, 0, KEY_READ, &key) != ERROR_SUCCESS) return settings;

    DWORD value = 0;
    if (ReadDword(key, L"ReadHotkey", value) && Unpack(value).IsValid()) settings.readHotkey = Unpack(value);
    if (ReadDword(key, L"CreateHotkey", value) && Unpack(value).IsValid()) settings.createHotkey = Unpack(value);
    // Two equal shortcuts could never both register; fall back to the defaults.
    if (settings.readHotkey == settings.createHotkey) settings = Settings{};
    if (ReadDword(key, L"KeepHistory", value)) settings.keepHistory = value != 0;

    RegCloseKey(key);
    return settings;
}

bool Settings::Save(const wchar_t* keyPath) const {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, keyPath, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    const bool saved = WriteDword(key, L"ReadHotkey", Pack(readHotkey)) && WriteDword(key, L"CreateHotkey", Pack(createHotkey)) &&
                       WriteDword(key, L"KeepHistory", keepHistory ? 1 : 0);
    RegCloseKey(key);
    return saved;
}

bool ConsumeFirstRun(const wchar_t* keyPath) {
    DWORD shown = 0;
    DWORD size = sizeof(shown);
    if (RegGetValueW(HKEY_CURRENT_USER, keyPath, L"FirstRunDone", RRF_RT_REG_DWORD, nullptr, &shown, &size) == ERROR_SUCCESS &&
        shown) {
        return false;
    }
    shown = 1;
    RegSetKeyValueW(HKEY_CURRENT_USER, keyPath, L"FirstRunDone", REG_DWORD, &shown, sizeof(shown));
    return true;
}

std::wstring AutostartCommand(const std::wstring& exePath) { return L"\"" + exePath + L"\" --background"; }

bool IsAutostartEnabled(const wchar_t* valueName) {
    if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, valueName, RRF_RT_REG_SZ, nullptr, nullptr, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    // Task Manager's "Disable" keeps the Run value and flags it here instead:
    // an odd first byte means disabled.
    BYTE state[12]{};
    DWORD size = sizeof(state);
    if (RegGetValueW(HKEY_CURRENT_USER, kStartupApprovedKey, valueName, RRF_RT_REG_BINARY, nullptr, state, &size) ==
            ERROR_SUCCESS &&
        size > 0) {
        return (state[0] & 1) == 0;
    }
    return true;
}

bool SetAutostartEnabled(bool enabled, const std::wstring& exePath, const wchar_t* valueName) {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    LSTATUS status;
    if (enabled) {
        // Setting one named value can never create a duplicate entry.
        const std::wstring command = AutostartCommand(exePath);
        status = RegSetValueExW(key, valueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                                static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        status = RegDeleteValueW(key, valueName);
        if (status == ERROR_FILE_NOT_FOUND) status = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    // Drop any Task Manager override so the Run value alone decides.
    RegDeleteKeyValueW(HKEY_CURRENT_USER, kStartupApprovedKey, valueName);
    return status == ERROR_SUCCESS;
}

}  // namespace kodo
