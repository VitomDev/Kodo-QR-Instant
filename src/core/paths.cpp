#include "core/paths.h"

#include <windows.h>
#include <shlobj.h>

namespace kodo {

namespace {

std::filesystem::path KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR folder = nullptr;
    std::filesystem::path path;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &folder))) path = folder;
    CoTaskMemFree(folder);
    return path;
}

}  // namespace

std::filesystem::path ExecutablePath() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length < path.size()) {
            path.resize(length);
            return path;
        }
        path.resize(path.size() * 2);
    }
}

std::filesystem::path DataDirectory() {
    const std::filesystem::path localAppData = KnownFolder(FOLDERID_LocalAppData);
    return localAppData.empty() ? localAppData : localAppData / L"Kodo QR Instant";
}

std::filesystem::path HistoryPath() { return DataDirectory() / L"history.txt"; }

std::filesystem::path PicturesDirectory() { return KnownFolder(FOLDERID_Pictures); }

}  // namespace kodo
