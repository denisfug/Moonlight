#include "ui/font.hpp"

#include <cmath>

#include "stb/stb_truetype.h"

namespace ml::ui {

struct Font::Info {
    stbtt_fontinfo f;
};

Font::Font(std::string ttf) : ttf_(std::move(ttf)), info_(new Info)
{
    const auto *d = reinterpret_cast<const unsigned char *>(ttf_.data());
    ok_ = !ttf_.empty() && stbtt_InitFont(&info_->f, d, stbtt_GetFontOffsetForIndex(d, 0)) != 0;
}

Font::~Font() = default;

uint32_t Font::nextCodepoint(const std::string &s, size_t &i)
{
    auto b = [&](size_t k) { return static_cast<unsigned char>(s[k]); };
    unsigned char c = b(i);
    if (c < 0x80) { i += 1; return c; }
    int n = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : 0;
    if (n == 0) { i += 1; return 0xFFFD; }   // stray continuation byte
    uint32_t cp = c & (0x3F >> n);
    for (int k = 1; k <= n; k++) {
        if (i + k >= s.size() || (b(i + k) & 0xC0) != 0x80) { i += 1; return 0xFFFD; }
        cp = (cp << 6) | (b(i + k) & 0x3F);
    }
    i += n + 1;
    return cp;
}

Font::Size &Font::size(int px)
{
    auto it = sizes_.find(px);
    if (it != sizes_.end()) return it->second;
    Size s;
    if (ok_) {
        s.scale = stbtt_ScaleForPixelHeight(&info_->f, float(px));
        int asc, desc, gap;
        stbtt_GetFontVMetrics(&info_->f, &asc, &desc, &gap);
        s.ascent = int(std::lround(asc * s.scale));
        s.lineHeight = int(std::lround((asc - desc + gap) * s.scale));
    }
    return sizes_.emplace(px, std::move(s)).first->second;
}

const Font::Glyph &Font::glyph(Size &s, uint32_t cp)
{
    auto it = s.glyphs.find(cp);
    if (it != s.glyphs.end()) return it->second;
    Glyph g;
    if (ok_) {
        g.index = stbtt_FindGlyphIndex(&info_->f, int(cp));
        if (g.index == 0 && cp != ' ') g.index = stbtt_FindGlyphIndex(&info_->f, '?');
        int adv, lsb;
        stbtt_GetGlyphHMetrics(&info_->f, g.index, &adv, &lsb);
        g.advance = int(std::lround(adv * s.scale * 64));
        int x0, y0, x1, y1;
        stbtt_GetGlyphBitmapBox(&info_->f, g.index, s.scale, s.scale, &x0, &y0, &x1, &y1);
        g.w = x1 - x0;
        g.h = y1 - y0;
        g.xoff = x0;
        g.yoff = y0;
        if (g.w > 0 && g.h > 0) {
            g.bitmap.resize(size_t(g.w) * g.h);
            stbtt_MakeGlyphBitmap(&info_->f, g.bitmap.data(), g.w, g.h, g.w, s.scale, s.scale, g.index);
        }
    }
    return s.glyphs.emplace(cp, std::move(g)).first->second;
}

int Font::draw(Canvas &c, int x, int y, int px, const std::string &text, Color color)
{
    Size &s = size(px);
    int pen = x * 64, prev = 0;
    for (size_t i = 0; i < text.size();) {
        uint32_t cp = nextCodepoint(text, i);
        const Glyph &g = glyph(s, cp);
        if (prev && ok_) pen += int(std::lround(stbtt_GetGlyphKernAdvance(&info_->f, prev, g.index) * s.scale * 64));
        if (!g.bitmap.empty()) c.blendMask((pen >> 6) + g.xoff, y + s.ascent + g.yoff, g.w, g.h, g.bitmap.data(), g.w, color);
        pen += g.advance;
        prev = g.index;
    }
    return (pen >> 6) - x;
}

int Font::measure(int px, const std::string &text)
{
    Size &s = size(px);
    int pen = 0, prev = 0;
    for (size_t i = 0; i < text.size();) {
        const Glyph &g = glyph(s, nextCodepoint(text, i));
        if (prev && ok_) pen += int(std::lround(stbtt_GetGlyphKernAdvance(&info_->f, prev, g.index) * s.scale * 64));
        pen += g.advance;
        prev = g.index;
    }
    return pen >> 6;
}

int Font::lineHeight(int px) { return size(px).lineHeight; }

std::string Font::ellipsize(int px, const std::string &text, int maxWidth)
{
    if (measure(px, text) <= maxWidth) return text;
    const std::string dots = "...";
    int dw = measure(px, dots);
    std::string out;
    for (size_t i = 0; i < text.size();) {
        size_t st = i;
        nextCodepoint(text, i);
        std::string next = out + text.substr(st, i - st);
        if (measure(px, next) + dw > maxWidth) break;
        out = next;
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out + dots;
}

std::vector<std::string> Font::wrap(int px, const std::string &text, int maxWidth, int maxLines)
{
    std::vector<std::string> lines;
    std::string cur;
    size_t i = 0;
    while (i <= text.size()) {
        size_t sp = text.find_first_of(" \n", i);
        std::string word = text.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
        std::string cand = cur.empty() ? word : cur + " " + word;
        if (!cur.empty() && measure(px, cand) > maxWidth) {
            lines.push_back(cur);
            cur = word;
        } else {
            cur = cand;
        }
        if (sp != std::string::npos && text[sp] == '\n') { lines.push_back(cur); cur.clear(); }
        if (sp == std::string::npos) break;
        i = sp + 1;
        if (int(lines.size()) >= maxLines) break;
    }
    if (!cur.empty() && int(lines.size()) < maxLines) lines.push_back(cur);
    if (int(lines.size()) > maxLines) lines.resize(size_t(maxLines));
    // if text was cut, ellipsize the last line
    bool cut = i < text.size() && int(lines.size()) == maxLines;
    if (!lines.empty()) {
        std::string &last = lines.back();
        if (cut || measure(px, last) > maxWidth) last = ellipsize(px, last + (cut ? " ..." : ""), maxWidth);
    }
    return lines;
}

}  // namespace ml::ui
