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
};

// Recently read and created QR codes, newest first, in a small UTF-8 file
// that stays on this PC. Repeating the newest entry only refreshes its time.
class History {
public:
    static constexpr size_t kMaxEntries = 100;

    explicit History(std::filesystem::path path) : path_(std::move(path)) {}

    std::vector<HistoryEntry> Load() const;
    bool Add(HistoryEntry::Origin origin, const std::wstring& text, int64_t time);
    bool Remove(size_t index);
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
