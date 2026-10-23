#pragma once

#include "core/settings.h"

#include <windows.h>

#include <functional>
#include <string>

namespace kodo {

// Applies confirmed settings (registers shortcuts, saves, sets autostart).
// Returns false with a message when they cannot be used, e.g. a shortcut is
// taken by another application.
using ApplySettings = std::function<bool(const Settings& settings, bool autostart, std::wstring& error)>;

// The small Settings window. Modal; a second request focuses the open one.
// `clearHistory` deletes the history right away, without waiting for Save.
void ShowSettingsDialog(HINSTANCE instance, HWND owner, const Settings& current, const ApplySettings& apply,
                        const std::function<void()>& clearHistory);

}  // namespace kodo
