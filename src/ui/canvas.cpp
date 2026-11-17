#include "ui/canvas.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace tgnx::ui {

Color mix(Color a, Color b, float t) {
    auto ca = [](Color c, int shift) { return int((c >> shift) & 0xFF); };
    int r = int(ca(a, 16) * (1 - t) + ca(b, 16) * t);
    int g = int(ca(a, 8) * (1 - t) + ca(b, 8) * t);
    int bl = int(ca(a, 0) * (1 - t) + ca(b, 0) * t);
    return 0xFF000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(bl);
}

void Canvas::resize(int width, int height) {
    width_ = width;
    height_ = height;
    pixels_.assign(size_t(width) * size_t(height), 0xFF000000u);
    clip_ = {0, 0, width, height};
}

void Canvas::clear(Color color) {
    std::fill(pixels_.begin(), pixels_.end(), color);
}

void Canvas::setClip(const Rect& r) {
    int x0 = std::max(0, r.x);
    int y0 = std::max(0, r.y);
    int x1 = std::min(width_, r.right());
    int y1 = std::min(height_, r.bottom());
    clip_ = {x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
}

void Canvas::resetClip() { clip_ = {0, 0, width_, height_}; }

void Canvas::blendPixel(int x, int y, Color color, uint8_t alpha) {
    if (x < clip_.x || y < clip_.y || x >= clip_.right() || y >= clip_.bottom()) return;
    if (alpha == 0) return;
    uint32_t* dst = &pixels_[size_t(y) * width_ + x];
    uint32_t src = color;
    int sa = alpha;
    if (sa >= 255) {
        *dst = src | 0xFF000000u;
        return;
    }
    int dr = int((*dst >> 16) & 0xFF);
    int dg = int((*dst >> 8) & 0xFF);
    int db = int(*dst & 0xFF);
    int sr = int((src >> 16) & 0xFF);
    int sg = int((src >> 8) & 0xFF);
    int sb = int(src & 0xFF);
    int r = (sr * sa + dr * (255 - sa)) / 255;
    int g = (sg * sa + dg * (255 - sa)) / 255;
    int b = (sb * sa + db * (255 - sa)) / 255;
    *dst = 0xFF000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
}

void Canvas::fillRect(const Rect& r, Color color) {
    int x0 = std::max(clip_.x, r.x);
    int y0 = std::max(clip_.y, r.y);
    int x1 = std::min(clip_.right(), r.right());
    int y1 = std::min(clip_.bottom(), r.bottom());
    if (x0 >= x1 || y0 >= y1) return;
    uint8_t alpha = uint8_t(color >> 24);
    if (alpha == 0) return;
    if (alpha == 255) {
        uint32_t c = color | 0xFF000000u;
        for (int y = y0; y < y1; ++y) {
            uint32_t* row = &pixels_[size_t(y) * width_];
            std::fill(row + x0, row + x1, c);
        }
    } else {
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) blendPixel(x, y, color, alpha);
        }
    }
}

void Canvas::fillCircle(int cx, int cy, int radius, Color color) {
    if (radius <= 0) return;
    int r2 = radius * radius;
    for (int dy = -radius; dy <= radius; ++dy) {
        int span = int(std::sqrt(double(r2 - dy * dy)));
        fillRect({cx - span, cy + dy, span * 2 + 1, 1}, color);
    }
}

void Canvas::fillRoundRect(const Rect& r, int radius, Color color) {
    radius = std::min(radius, std::min(r.w, r.h) / 2);
    if (radius <= 0) {
        fillRect(r, color);
        return;
    }
    fillRect({r.x, r.y + radius, r.w, r.h - 2 * radius}, color);
    fillRect({r.x + radius, r.y, r.w - 2 * radius, radius}, color);
    fillRect({r.x + radius, r.y + r.h - radius, r.w - 2 * radius, radius}, color);
    for (int dy = 0; dy < radius; ++dy) {
        int dx = int(std::sqrt(double(radius * radius - (radius - 1 - dy) * (radius - 1 - dy))));
        int inset = radius - dx;
        fillRect({r.x + inset, r.y + dy, r.w - 2 * inset, 1}, color);
        fillRect({r.x + inset, r.y + r.h - 1 - dy, r.w - 2 * inset, 1}, color);
    }
}

void Canvas::strokeRoundRect(const Rect& r, int radius, Color color, int thickness) {
    for (int i = 0; i < thickness; ++i) {
        Rect rr = r.inset(i);
        int rad = std::max(0, radius - i);
        fillRoundRect({rr.x, rr.y, rr.w, 1}, rad, color);
        fillRoundRect({rr.x, rr.y + rr.h - 1, rr.w, 1}, rad, color);
        fillRoundRect({rr.x, rr.y + rad, 1, rr.h - 2 * rad}, rad, color);
        fillRoundRect({rr.x + rr.w - 1, rr.y + rad, 1, rr.h - 2 * rad}, rad, color);
    }
}

void Canvas::line(int x0, int y0, int x1, int y1, Color color) {
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        blendPixel(x0, y0, color, uint8_t(color >> 24));
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

#pragma pack(push, 1)
struct BmpHeader {
    uint16_t type = 0x4D42;
    uint32_t size = 0;
    uint16_t reserved1 = 0;
    uint16_t reserved2 = 0;
    uint32_t offBits = 54;
    uint32_t biSize = 40;
    int32_t width = 0;
    int32_t height = 0;
    uint16_t planes = 1;
    uint16_t bitCount = 24;
    uint32_t compression = 0;
    uint32_t imageSize = 0;
    int32_t xppm = 2835;
    int32_t yppm = 2835;
    uint32_t clrUsed = 0;
    uint32_t clrImportant = 0;
};
#pragma pack(pop)

bool saveBmp(const std::string& path, const Canvas& canvas) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    int rowBytes = (canvas.width() * 3 + 3) & ~3;
    BmpHeader h;
    h.width = canvas.width();
    h.height = canvas.height();
    h.size = 54 + uint32_t(rowBytes * canvas.height());
    h.imageSize = uint32_t(rowBytes * canvas.height());
    std::fwrite(&h, sizeof(h), 1, f);
    std::vector<uint8_t> row(rowBytes, 0);
    for (int y = canvas.height() - 1; y >= 0; --y) {
        const uint32_t* src = canvas.pixels() + size_t(y) * canvas.width();
        for (int x = 0; x < canvas.width(); ++x) {
            Color c = src[x];
            row[x * 3 + 0] = uint8_t(c & 0xFF);
            row[x * 3 + 1] = uint8_t((c >> 8) & 0xFF);
            row[x * 3 + 2] = uint8_t((c >> 16) & 0xFF);
        }
        std::fwrite(row.data(), row.size(), 1, f);
    }
    std::fclose(f);
    return true;
}

}  // namespace tgnx::ui
