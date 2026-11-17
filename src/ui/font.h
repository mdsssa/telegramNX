// Bitmap font atlas backed by stb_truetype, with Cyrillic coverage.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ui/canvas.h"

namespace tgnx::ui {

class Font {
public:
    Font() = default;
    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;

    bool load(const std::string& ttf, float pixelHeight);
    bool loaded() const { return loaded_; }

    float lineHeight() const { return lineHeight_; }
    float ascent() const { return ascent_; }
    float measure(std::string_view utf8) const;
    float measureChar(char32_t cp) const;
    void draw(Canvas& canvas, float x, float baselineY, std::string_view utf8, Color color) const;
    std::string ellipsize(std::string_view utf8, float maxWidth) const;

private:
    struct Glyph {
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // offsets from pen/baseline
        float advance = 0;
        int ax = 0, ay = 0, aw = 0, ah = 0;  // atlas rectangle
    };

    void addGlyph(char32_t cp);

    std::vector<uint8_t> data_;
    std::vector<uint8_t> atlas_;
    std::unordered_map<char32_t, Glyph> glyphs_;
    int atlasW_ = 1024, atlasH_ = 1024;
    int penX_ = 1, penY_ = 1, rowH_ = 0;
    float scale_ = 1;
    float ascent_ = 0, descent_ = 0, lineHeight_ = 0;
    bool loaded_ = false;
};

std::vector<char32_t> decodeUtf8(std::string_view s);

}  // namespace tgnx::ui
