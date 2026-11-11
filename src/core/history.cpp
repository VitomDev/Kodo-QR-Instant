#include "core/history.h"

#include "core/text.h"

#include <windows.h>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace kodo {

namespace {

// One entry per line: "R|C <tab> unix time <tab> text", with backslash
// escapes for backslashes, tabs and line breaks inside the text.
std::wstring Escape(const std::wstring& text) {
    std::wstring result;
    for (wchar_t c : text) {
        switch (c) {
            case L'\\': result += L"\\\\"; break;
            case L'\t': result += L"\\t"; break;
            case L'\n': result += L"\\n"; break;
            case L'\r': result += L"\\r"; break;
            default: result += c;
        }
    }
    return result;
}

std::wstring Unescape(std::wstring_view text) {
    std::wstring result;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != L'\\' || i + 1 == text.size()) {
            result += text[i];
            continue;
        }
        switch (text[++i]) {
            case L't': result += L'\t'; break;
            case L'n': result += L'\n'; break;
            case L'r': result += L'\r'; break;
            default: result += text[i];
        }
    }
    return result;
}

SYSTEMTIME ToLocalTime(int64_t unixSeconds) {
    // FILETIME counts 100 ns intervals since 1601.
    const uint64_t ticks = static_cast<uint64_t>(unixSeconds + 11644473600LL) * 10000000ULL;
    const FILETIME utc{static_cast<DWORD>(ticks), static_cast<DWORD>(ticks >> 32)};
    SYSTEMTIME utcTime{}, local{};
    FileTimeToSystemTime(&utc, &utcTime);
    SystemTimeToTzSpecificLocalTime(nullptr, &utcTime, &local);
    return local;
}

}  // namespace

std::vector<HistoryEntry> History::Load() const {
    std::vector<HistoryEntry> entries;
    std::ifstream file(path_, std::ios::binary);
    std::string line;
    while (entries.size() < 4 * kMaxEntries && std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::wstring wide = FromUtf8(line);
        const size_t first = wide.find(L'\t');
        const size_t second = first == std::wstring::npos ? first : wide.find(L'\t', first + 1);
        // "R" or "C", followed by "*" for a favorite.
        if (second == std::wstring::npos || first < 1 || first > 2 || (wide[0] != L'R' && wide[0] != L'C') ||
            (first == 2 && wide[1] != L'*')) {
            continue;
        }
        HistoryEntry entry;
        entry.favorite = first == 2;
        entry.origin = wide[0] == L'R' ? HistoryEntry::Origin::Read : HistoryEntry::Origin::Created;
        entry.time = _wtoi64(wide.substr(first + 1, second - first - 1).c_str());
        entry.text = Unescape(std::wstring_view(wide).substr(second + 1));
        if (!entry.text.empty()) entries.push_back(std::move(entry));
    }
    std::stable_partition(entries.begin(), entries.end(), [](const HistoryEntry& e) { return e.favorite; });
    return entries;
}

bool History::Save(const std::vector<HistoryEntry>& entries) const {
    std::error_code error;
    std::filesystem::create_directories(path_.parent_path(), error);
    // Write a temporary file first so a crash never leaves half a history.
    std::filesystem::path temporary = path_;
    temporary += L".tmp";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        for (const auto& entry : entries) {
            std::wostringstream line;
            line << (entry.origin == HistoryEntry::Origin::Read ? L'R' : L'C') << (entry.favorite ? L"*" : L"") << L'\t'
                 << entry.time << L'\t' << Escape(entry.text);
            file << ToUtf8(line.str()) << '\n';
        }
        if (!file.good()) return false;
    }
    return MoveFileExW(temporary.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE;
}

bool History::Add(HistoryEntry::Origin origin, const std::wstring& text, int64_t time) {
    if (!HasVisibleText(text)) return false;
    std::vector<HistoryEntry> entries = Load();
    const auto firstRecent = std::find_if(entries.begin(), entries.end(), [](const HistoryEntry& e) { return !e.favorite; });
    const auto favorite = std::find_if(entries.begin(), firstRecent, [&](const HistoryEntry& e) { return e.text == text; });
    if (favorite != firstRecent) {
        favorite->time = time;
    } else if (firstRecent != entries.end() && firstRecent->text == text && firstRecent->origin == origin) {
        firstRecent->time = time;
    } else {
        const size_t favorites = static_cast<size_t>(firstRecent - entries.begin());
        entries.insert(entries.begin() + static_cast<std::ptrdiff_t>(favorites), HistoryEntry{origin, time, text});
        if (entries.size() > favorites + kMaxEntries) entries.resize(favorites + kMaxEntries);
    }
    return Save(entries);
}

bool History::Remove(size_t index) {
    std::vector<HistoryEntry> entries = Load();
    if (index >= entries.size()) return false;
    entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(index));
    return entries.empty() ? Clear() : Save(entries);
}

bool History::SetFavorite(size_t index, bool favorite) {
    std::vector<HistoryEntry> entries = Load();
    if (index >= entries.size()) return false;
    entries[index].favorite = favorite;
    std::stable_partition(entries.begin(), entries.end(), [](const HistoryEntry& e) { return e.favorite; });
    return Save(entries);
}

bool History::ClearKeepingFavorites() {
    std::vector<HistoryEntry> entries = Load();
    std::erase_if(entries, [](const HistoryEntry& e) { return !e.favorite; });
    return entries.empty() ? Clear() : Save(entries);
}

bool History::Clear() {
    std::error_code error;
    std::filesystem::remove(path_, error);
    return !error;
}

int64_t UnixTimeNow() {
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    const uint64_t ticks = (static_cast<uint64_t>(now.dwHighDateTime) << 32) | now.dwLowDateTime;
    return static_cast<int64_t>(ticks / 10000000ULL) - 11644473600LL;
}

std::wstring FormatTime(int64_t unixSeconds) {
    static constexpr const wchar_t* kMonths[] = {L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun",
                                                 L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec"};
    const SYSTEMTIME when = ToLocalTime(unixSeconds);
    const SYSTEMTIME today = ToLocalTime(UnixTimeNow());
    const SYSTEMTIME yesterday = ToLocalTime(UnixTimeNow() - 24 * 3600);
    const auto sameDay = [](const SYSTEMTIME& a, const SYSTEMTIME& b) {
        return a.wYear == b.wYear && a.wMonth == b.wMonth && a.wDay == b.wDay;
    };
    wchar_t clock[8];
    swprintf_s(clock, L"%02u:%02u", when.wHour, when.wMinute);
    if (sameDay(when, today)) return clock;
    if (sameDay(when, yesterday)) return std::wstring(L"Yesterday ") + clock;
    wchar_t date[32];
    swprintf_s(date, L"%u %s %u ", when.wDay, kMonths[(when.wMonth + 11) % 12], when.wYear);
    return date + std::wstring(clock);
}

}  // namespace kodo
