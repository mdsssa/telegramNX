#include "ui/font.h"

#include <algorithm>
#include <cstring>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

namespace tgnx::ui {

std::vector<char32_t> decodeUtf8(std::string_view s) {
    std::vector<char32_t> out;
    out.reserve(s.size());
    const auto* p = reinterpret_cast<const unsigned char*>(s.data());
    size_t n = s.size();
    for (size_t i = 0; i < n;) {
        uint32_t cp = p[i];
        if (cp < 0x80) {
            i += 1;
        } else if ((cp & 0xE0) == 0xC0 && i + 1 < n) {
            cp = ((cp & 0x1F) << 6) | (p[i + 1] & 0x3F);
            i += 2;
        } else if ((cp & 0xF0) == 0xE0 && i + 2 < n) {
            cp = ((cp & 0x0F) << 12) | ((p[i + 1] & 0x3F) << 6) | (p[i + 2] & 0x3F);
            i += 3;
        } else if ((cp & 0xF8) == 0xF0 && i + 3 < n) {
            cp = ((cp & 0x07) << 18) | ((p[i + 1] & 0x3F) << 12) | ((p[i + 2] & 0x3F) << 6) |
                 (p[i + 3] & 0x3F);
            i += 4;
        } else {
            cp = 0xFFFD;
            i += 1;
        }
        out.push_back(char32_t(cp));
    }
    return out;
}

bool Font::load(const std::string& ttf, float pixelHeight) {
    data_.assign(ttf.begin(), ttf.end());
    if (data_.empty()) return false;
    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, data_.data(), stbtt_GetFontOffsetForIndex(data_.data(), 0)))
        return false;
    scale_ = stbtt_ScaleForPixelHeight(&info, pixelHeight);
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
    ascent_ = ascent * scale_;
    descent_ = descent * scale_;
    lineHeight_ = (ascent - descent + lineGap) * scale_;

    atlas_.assign(size_t(atlasW_) * atlasH_, 0);
    penX_ = 1;
    penY_ = 1;
    rowH_ = 0;

    for (char32_t cp = 32; cp < 127; ++cp) addGlyph(cp);
    for (char32_t cp = 0x0400; cp <= 0x045F; ++cp) addGlyph(cp);
    for (char32_t cp : {0x00A0, 0x00AB, 0x00BB, 0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D,
                        0x2026, 0x2116, 0x20BD, 0x2190, 0x2191, 0x2192, 0x2193, 0x2605})
        addGlyph(cp);
    loaded_ = true;
    return true;
}

void Font::addGlyph(char32_t cp) {
    // stbtt_fontinfo is local to load; re-create it for every glyph is costly,
    // so use the public static helpers through a tiny local info.
    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, data_.data(), stbtt_GetFontOffsetForIndex(data_.data(), 0))) return;
    if (!stbtt_FindGlyphIndex(&info, int(cp))) return;

    int adv = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(&info, int(cp), &adv, &lsb);
    int x0, y0, x1, y1;
    stbtt_GetCodepointBitmapBox(&info, int(cp), scale_, scale_, &x0, &y0, &x1, &y1);
    int w = x1 - x0;
    int h = y1 - y0;
    Glyph g;
    g.advance = adv * scale_;
    g.x0 = float(x0);
    g.y0 = float(y0);
    g.x1 = float(x1);
    g.y1 = float(y1);
    if (w <= 0 || h <= 0) {
        glyphs_[cp] = g;
        return;
    }
    if (penX_ + w + 1 >= atlasW_) {
        penX_ = 1;
        penY_ += rowH_ + 1;
        rowH_ = 0;
    }
    if (penY_ + h + 1 >= atlasH_) return;
    std::vector<uint8_t> bitmap(size_t(w) * h);
    stbtt_MakeCodepointBitmap(&info, bitmap.data(), w, h, w, scale_, scale_, int(cp));
    for (int row = 0; row < h; ++row) {
        std::memcpy(&atlas_[size_t(penY_ + row) * atlasW_ + penX_], &bitmap[size_t(row) * w], w);
    }
    g.ax = penX_;
    g.ay = penY_;
    g.aw = w;
    g.ah = h;
    glyphs_[cp] = g;
    penX_ += w + 1;
    rowH_ = std::max(rowH_, h);
}

float Font::measureChar(char32_t cp) const {
    auto it = glyphs_.find(cp);
    if (it == glyphs_.end()) return 0;
    return it->second.advance;
}

float Font::measure(std::string_view utf8) const {
    float w = 0;
    for (char32_t cp : decodeUtf8(utf8)) w += measureChar(cp);
    return w;
}

void Font::draw(Canvas& canvas, float x, float baselineY, std::string_view utf8, Color color) const {
    if (!loaded_) return;
    uint8_t colorAlpha = uint8_t(color >> 24);
    float penX = x;
    for (char32_t cp : decodeUtf8(utf8)) {
        auto it = glyphs_.find(cp);
        if (it == glyphs_.end()) {
            it = glyphs_.find(U'?');
            if (it == glyphs_.end()) continue;
        }
        const Glyph& g = it->second;
        if (g.aw > 0 && g.ah > 0) {
            int dx = int(penX + g.x0);
            int dy = int(baselineY + g.y0);
            for (int row = 0; row < g.ah; ++row) {
                const uint8_t* src = &atlas_[size_t(g.ay + row) * atlasW_ + g.ax];
                for (int col = 0; col < g.aw; ++col) {
                    uint8_t a = src[col];
                    if (!a) continue;
                    uint8_t alpha = uint8_t((a * colorAlpha) / 255);
                    canvas.blendPixel(dx + col, dy + row, color, alpha);
                }
            }
        }
        penX += g.advance;
    }
}

std::string Font::ellipsize(std::string_view utf8, float maxWidth) const {
    if (measure(utf8) <= maxWidth) return std::string(utf8);
    std::string out;
    float w = 0;
    float ellipsisW = measure("\xE2\x80\xA6");
    for (char32_t cp : decodeUtf8(utf8)) {
        float cw = measureChar(cp);
        if (w + cw + ellipsisW > maxWidth) break;
        // Re-encode the code point.
        if (cp < 0x80) {
            out.push_back(char(cp));
        } else if (cp < 0x800) {
            out.push_back(char(0xC0 | (cp >> 6)));
            out.push_back(char(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(char(0xE0 | (cp >> 12)));
            out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(char(0x80 | (cp & 0x3F)));
        }
        w += cw;
    }
    out += "\xE2\x80\xA6";
    return out;
}

}  // namespace tgnx::ui
