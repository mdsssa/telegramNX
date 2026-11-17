// Small software canvas used by the UI on both Switch and the host.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tgnx::ui {

using Color = uint32_t;  // 0xAARRGGBB

constexpr Color rgb(uint8_t r, uint8_t g, uint8_t b) {
    return 0xFF000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
}
constexpr Color withAlpha(Color c, uint8_t a) { return (c & 0x00FFFFFFu) | (uint32_t(a) << 24); }
Color mix(Color a, Color b, float t);

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    Rect inset(int d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
    int right() const { return x + w; }
    int bottom() const { return y + h; }
};

class Canvas {
public:
    Canvas() = default;
    Canvas(int width, int height) { resize(width, height); }

    void resize(int width, int height);
    int width() const { return width_; }
    int height() const { return height_; }
    const uint32_t* pixels() const { return pixels_.data(); }
    uint32_t* pixels() { return pixels_.data(); }

    void clear(Color color);
    void fillRect(const Rect& r, Color color);
    void fillRoundRect(const Rect& r, int radius, Color color);
    void strokeRoundRect(const Rect& r, int radius, Color color, int thickness = 1);
    void fillCircle(int cx, int cy, int radius, Color color);
    void line(int x0, int y0, int x1, int y1, Color color);
    void blendPixel(int x, int y, Color color, uint8_t alpha);

    void setClip(const Rect& r);
    void resetClip();
    Rect clip() const { return clip_; }

private:
    int width_ = 0;
    int height_ = 0;
    std::vector<uint32_t> pixels_;
    Rect clip_;
};

bool saveBmp(const std::string& path, const Canvas& canvas);

}  // namespace tgnx::ui
