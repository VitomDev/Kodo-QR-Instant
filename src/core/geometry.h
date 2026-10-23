#pragma once

#include <windows.h>

namespace kodo {

// Selections smaller than this in either direction are treated as accidental
// clicks and ignored.
constexpr int kMinSelectionSize = 4;

// The rectangle spanned by two drag points, in any drag direction, clamped to
// `bounds`. `right` and `bottom` are exclusive and include the end pixel.
RECT SelectionRect(POINT start, POINT end, const RECT& bounds);

bool IsUsableSelection(const RECT& rect);

// The snapshot of the virtual screen starts at the virtual screen origin, which
// is negative when monitors sit left of or above the primary one.
POINT ScreenToSnapshot(POINT screen, const RECT& virtualScreen);
RECT SnapshotToScreen(const RECT& rect, const RECT& virtualScreen);

}  // namespace kodo
