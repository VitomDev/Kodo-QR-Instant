#include "core/geometry.h"

#include <algorithm>

namespace kodo {

RECT SelectionRect(POINT start, POINT end, const RECT& bounds) {
    RECT rect{std::min(start.x, end.x), std::min(start.y, end.y), std::max(start.x, end.x) + 1,
              std::max(start.y, end.y) + 1};
    rect.left = std::clamp(rect.left, bounds.left, bounds.right);
    rect.top = std::clamp(rect.top, bounds.top, bounds.bottom);
    rect.right = std::clamp(rect.right, rect.left, bounds.right);
    rect.bottom = std::clamp(rect.bottom, rect.top, bounds.bottom);
    return rect;
}

bool IsUsableSelection(const RECT& rect) {
    return rect.right - rect.left >= kMinSelectionSize && rect.bottom - rect.top >= kMinSelectionSize;
}

POINT ScreenToSnapshot(POINT screen, const RECT& virtualScreen) {
    return {screen.x - virtualScreen.left, screen.y - virtualScreen.top};
}

RECT SnapshotToScreen(const RECT& rect, const RECT& virtualScreen) {
    return {rect.left + virtualScreen.left, rect.top + virtualScreen.top,
            rect.right + virtualScreen.left, rect.bottom + virtualScreen.top};
}

}  // namespace kodo
