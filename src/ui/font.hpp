// TrueType text via stb_truetype (public domain): UTF-8, kerning, per-size glyph cache.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "ui/canvas.hpp"

namespace ml::ui {

class Font {
public:
    // Takes ownership of the TTF bytes. ok() is false if the data is not a usable font.
    explicit Font(std::string ttf);
    ~Font();
    bool ok() const { return ok_; }

    // Draws at baseline-free top-left (y = top of the line box). Returns the advance width.
    int draw(Canvas &c, int x, int y, int px, const std::string &utf8, Color color);
    int measure(int px, const std::string &utf8);
    int lineHeight(int px);
    // Cuts the text with "..." so it fits maxWidth.
    std::string ellipsize(int px, const std::string &utf8, int maxWidth);
    // Word wrap into at most maxLines lines (last one ellipsized).
    std::vector<std::string> wrap(int px, const std::string &utf8, int maxWidth, int maxLines);

    static uint32_t nextCodepoint(const std::string &s, size_t &i);

private:
    struct Glyph {
        std::vector<uint8_t> bitmap;
        int w = 0, h = 0, xoff = 0, yoff = 0;
        int advance = 0;   // 1/64 px
        int index = 0;
    };
    struct Size {
        float scale = 0;
        int ascent = 0, lineHeight = 0;
        std::unordered_map<uint32_t, Glyph> glyphs;
    };
    std::string ttf_;
    struct Info;
    std::unique_ptr<Info> info_;
    bool ok_ = false;
    std::unordered_map<int, Size> sizes_;
    Size &size(int px);
    const Glyph &glyph(Size &s, uint32_t cp);
};

}  // namespace ml::ui
