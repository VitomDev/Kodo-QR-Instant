#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace kodo {

struct HistoryEntry {
    enum class Origin { Read, Created };
    Origin origin = Origin::Read;
    int64_t time = 0;  // Unix seconds
    std::wstring text;
    bool favorite = false;  // Kept at the top and never dropped for space
};

// Recently read and created QR codes in a small UTF-8 file that stays on
// this PC: favorites first, then the rest newest first. Repeating the newest
// entry (or a favorite) only refreshes its time.
class History {
public:
    static constexpr size_t kMaxEntries = 100;  // Not counting favorites

    explicit History(std::filesystem::path path) : path_(std::move(path)) {}

    std::vector<HistoryEntry> Load() const;
    bool Add(HistoryEntry::Origin origin, const std::wstring& text, int64_t time);
    bool Remove(size_t index);
    bool SetFavorite(size_t index, bool favorite);
    // Deletes everything except the favorites.
    bool ClearKeepingFavorites();
    // Deletes the file: nothing is left on disk.
    bool Clear();

private:
    bool Save(const std::vector<HistoryEntry>& entries) const;

    std::filesystem::path path_;
};

int64_t UnixTimeNow();
// "14:05" today, "Yesterday 14:05", otherwise "23 Oct 2026 14:05", in local time.
std::wstring FormatTime(int64_t unixSeconds);

}  // namespace kodo
