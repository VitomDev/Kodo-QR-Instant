#include "app/scan_effect.h"

#include "app/theme.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace kodo {

namespace {

constexpr wchar_t kClassName[] = L"KodoScanEffect";
constexpr UINT_PTR kFrameTimer = 1;
constexpr float kDuration = 900;  // Milliseconds
constexpr float kSweepEnd = 0.45f;

float Clamp01(float value) { return std::clamp(value, 0.0f, 1.0f); }
float EaseOut(float t) { return 1 - (1 - t) * (1 - t); }

// Signed distance from a point to a rounded rectangle (negative inside).
float RoundedRectDistance(float x, float y, const RECT& r, float radius) {
    const float cx = (r.left + r.right) / 2.0f, cy = (r.top + r.bottom) / 2.0f;
    const float hx = (r.right - r.left) / 2.0f - radius, hy = (r.bottom - r.top) / 2.0f - radius;
    const float qx = std::abs(x - cx) - hx, qy = std::abs(y - cy) - hy;
    const float outside = std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f));
    return outside + std::min(std::max(qx, qy), 0.0f) - radius;
}

// Adds light of `alpha` to a premultiplied pixel, towards white by `white`.
void Add(uint32_t& pixel, COLORREF color, float alpha, float white) {
    if (alpha <= 0.003f) return;
    alpha = std::min(alpha, 1.0f);
    const float r = GetRValue(color) + (255 - GetRValue(color)) * white;
    const float g = GetGValue(color) + (255 - GetGValue(color)) * white;
    const float b = GetBValue(color) + (255 - GetBValue(color)) * white;
    const float a0 = (pixel >> 24) / 255.0f;
    const float keep = 1 - alpha;
    const auto channel = [&](int shift, float value) {
        return static_cast<uint32_t>(std::min(255.0f, ((pixel >> shift) & 0xFF) * keep + value * alpha)) << shift;
    };
    const uint32_t a = static_cast<uint32_t>(std::min(255.0f, (a0 * keep + alpha) * 255));
    pixel = (a << 24) | channel(16, r) | channel(8, g) | channel(0, b);
}

}  // namespace

bool ScanEffect::Create(HINSTANCE instance) {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);
    window_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClassName,
                              L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, this);
    return window_ != nullptr;
}

void ScanEffect::Destroy() {
    if (window_) DestroyWindow(window_);
    window_ = nullptr;
}

void ScanEffect::Play(const RECT& area) {
    // Move onto the target monitor first so the sizes use its DPI.
    MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(MonitorFromRect(&area, MONITOR_DEFAULTTONEAREST), &info);
    SetWindowPos(window_, nullptr, info.rcMonitor.left, info.rcMonitor.top, 1, 1, SWP_NOZORDER | SWP_NOACTIVATE);
    scale_ = GetDpiForWindow(window_) / 96.0f;
    margin_ = static_cast<int>(80 * scale_);
    width_ = area.right - area.left + 2 * margin_;
    height_ = area.bottom - area.top + 2 * margin_;
    area_ = {margin_, margin_, width_ - margin_, height_ - margin_};
    pixels_.assign(static_cast<size_t>(width_) * height_, 0);
    accent_ = Theme::Current().accent;

    // Sparks start on the frame and fly outwards.
    std::mt19937 random(GetTickCount());
    std::uniform_real_distribution<float> unit(0, 1);
    sparks_.clear();
    const float w = static_cast<float>(area_.right - area_.left), h = static_cast<float>(area_.bottom - area_.top);
    for (int i = 0; i < 64; ++i) {
        const float along = unit(random) * 2 * (w + h);
        float x, y, nx, ny;
        if (along < w) { x = area_.left + along; y = static_cast<float>(area_.top); nx = 0; ny = -1; }
        else if (along < w + h) { x = static_cast<float>(area_.right); y = area_.top + along - w; nx = 1; ny = 0; }
        else if (along < 2 * w + h) { x = area_.right - (along - w - h); y = static_cast<float>(area_.bottom); nx = 0; ny = 1; }
        else { x = static_cast<float>(area_.left); y = area_.bottom - (along - 2 * w - h); nx = -1; ny = 0; }
        const float speed = (25 + unit(random) * 45) * scale_;
        const float spread = (unit(random) - 0.5f) * 0.9f;
        sparks_.push_back({x, y, (nx + ny * spread) * speed, (ny - nx * spread) * speed, (2.0f + unit(random) * 2.5f) * scale_});
    }

    SetWindowPos(window_, HWND_TOPMOST, area.left - margin_, area.top - margin_, width_, height_, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    start_ = GetTickCount64();
    Frame();
    SetTimer(window_, kFrameTimer, 15, nullptr);
}

void ScanEffect::Render(float t) {
    std::fill(pixels_.begin(), pixels_.end(), 0);
    const float radius = 10 * scale_;
    const float fade = 1 - Clamp01((t - 0.65f) / 0.35f);
    // The frame lights up as the sweep passes, then everything fades.
    const float frame = Clamp01(t / 0.15f) * fade;
    const float sweep = EaseOut(Clamp01(t / kSweepEnd));
    const float lineY = area_.top + (area_.bottom - area_.top) * sweep;
    const float tail = std::max(24.0f * scale_, (area_.bottom - area_.top) * 0.35f);
    const float sweepAlpha = t < kSweepEnd ? 1.0f : Clamp01(1 - (t - kSweepEnd) / 0.12f);
    const float glow = 7 * scale_;

    // Only the band around the frame and the rows under the sweep are
    // touched, so large selections stay cheap.
    const int band = static_cast<int>(4 * glow + radius) + 2;
    for (int y = 0; y < height_; ++y) {
        uint32_t* row = &pixels_[static_cast<size_t>(y) * width_];
        const bool nearTopOrBottom = y < area_.top + band || y >= area_.bottom - band;
        for (int x = 0; x < width_; ++x) {
            if (!nearTopOrBottom && x == area_.left + band) x = std::max(x, static_cast<int>(area_.right) - band);
            const float d = RoundedRectDistance(x + 0.5f, y + 0.5f, area_, radius);
            const float edge = std::abs(d);
            if (edge < 4 * glow) {
                // Bright 2 px line; the halo spreads outwards and barely inside, over the code.
                const float core = Clamp01(1.5f * scale_ - edge + 1);
                const float halo = d > 0 ? std::exp(-edge / glow) * 0.6f : std::exp(-edge / (glow * 0.35f)) * 0.3f;
                Add(row[x], accent_, (halo + core) * frame, core * 0.6f);
            }
        }
        if (sweepAlpha <= 0 || y < area_.top || y >= area_.bottom) continue;
        const float above = lineY - (y + 0.5f);
        const bool line = std::abs(above) < 1.5f * scale_;
        if (!line && (above < 0 || above >= tail)) continue;
        const float k = 1 - above / tail;
        for (int x = area_.left; x < area_.right; ++x) {
            if (RoundedRectDistance(x + 0.5f, y + 0.5f, area_, radius) >= 0) continue;
            if (above >= 0 && above < tail) Add(row[x], accent_, k * k * 0.32f * sweepAlpha, 0.15f);
            if (line) Add(row[x], accent_, 0.95f * sweepAlpha, 0.7f);
        }
    }

    // Sparks appear when the sweep reaches the bottom.
    const float sparkTime = (t - kSweepEnd * 0.8f) * kDuration / 1000;
    if (sparkTime > 0) {
        const float life = Clamp01(1 - sparkTime / 0.5f);
        for (const Spark& spark : sparks_) {
            const float sx = spark.x + spark.vx * sparkTime * 3, sy = spark.y + spark.vy * sparkTime * 3;
            const int r = static_cast<int>(std::ceil(spark.size * 2));
            for (int dy = -r; dy <= r; ++dy) {
                const int py = static_cast<int>(sy) + dy;
                if (py < 0 || py >= height_) continue;
                for (int dx = -r; dx <= r; ++dx) {
                    const int px = static_cast<int>(sx) + dx;
                    if (px < 0 || px >= width_) continue;
                    const float dist = std::hypot(px + 0.5f - sx, py + 0.5f - sy);
                    // White-hot centre fading to the accent color.
                    const float k = Clamp01(1 - dist / (spark.size * 2));
                    Add(pixels_[static_cast<size_t>(py) * width_ + px], accent_, k * k * 1.4f * life, k);
                }
            }
        }
    }
}

void ScanEffect::Frame() {
    const float t = (GetTickCount64() - start_) / kDuration;
    if (t >= 1) {
        KillTimer(window_, kFrameTimer);
        ShowWindow(window_, SW_HIDE);
        pixels_.clear();
        pixels_.shrink_to_fit();
        return;
    }
    Render(t);

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader = {sizeof(BITMAPINFOHEADER), width_, -height_, 1, 32, BI_RGB};
    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(memory, &bitmapInfo, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bitmap) {
        std::copy(pixels_.begin(), pixels_.end(), static_cast<uint32_t*>(bits));
        HGDIOBJ previous = SelectObject(memory, bitmap);
        POINT source{0, 0};
        SIZE size{width_, height_};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        UpdateLayeredWindow(window_, screen, nullptr, &size, memory, &source, 0, &blend, ULW_ALPHA);
        SelectObject(memory, previous);
        DeleteObject(bitmap);
    }
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
}

LRESULT CALLBACK ScanEffect::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
    }
    auto* self = reinterpret_cast<ScanEffect*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (self && message == WM_TIMER && wParam == kFrameTimer) {
        self->Frame();
        return 0;
    }
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return DefWindowProcW(window, message, wParam, lParam);
}

}  // namespace kodo
