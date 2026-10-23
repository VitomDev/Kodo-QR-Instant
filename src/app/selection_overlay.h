#pragma once

#include "app/screen_capture.h"

#include <windows.h>

#include <optional>

namespace kodo {

// Shows the frozen screen dimmed over all monitors and lets the user drag a
// rectangle. Returns the selection in snapshot coordinates, or nothing when
// canceled (Escape, right click, switching away or a display change).
// Runs a modal message loop on the calling (UI) thread.
std::optional<RECT> SelectScreenRegion(HINSTANCE instance, const ScreenSnapshot& snapshot);

}  // namespace kodo
