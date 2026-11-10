#include "ui/widgets.hpp"

#include <cmath>
#include <cstdio>
#include <unordered_map>

#include "platform/platform.hpp"
#include "ui/theme.hpp"

namespace ml::ui {

uint32_t hashStr(const std::string &s)
{
    uint32_t h = 2166136261u;
    for (unsigned char c : s) { h ^= c; h *= 16777619u; }
    return h;
}

static Color hsv(float h, float s, float v)
{
    h = std::fmod(h, 360.0f);
    float c = v * s, x = c * (1 - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1)), m = v - c;
    float r = 0, g = 0, b = 0;
    if (h < 60) { r = c; g = x; } else if (h < 120) { r = x; g = c; } else if (h < 180) { g = c; b = x; }
    else if (h < 240) { g = x; b = c; } else if (h < 300) { r = x; b = c; } else { r = c; b = x; }
    return rgba(int((r + m) * 255), int((g + m) * 255), int((b + m) * 255));
}

static void renderArt(Canvas &c, Fonts &f, Rect r, int radius, const std::string &seed, const std::string &title, bool showTitle);

void drawGeneratedArt(Canvas &c, Fonts &f, Rect r, int radius, const std::string &seed, const std::string &title,
                      bool showTitle)
{
    // Artwork is expensive to rasterise (gradients, circles, text) and is redrawn every frame, so each
    // (seed, size, title) is rendered once into a cached image and then copied.
    if (r.w <= 0 || r.h <= 0) return;
    static std::unordered_map<std::string, Image> *cache = nullptr;   // created on first use (no static ctors)
    if (!cache) cache = new std::unordered_map<std::string, Image>();
    std::string key = seed + "|" + std::to_string(r.w) + "x" + std::to_string(r.h) + "|" + (showTitle ? title : "");
    auto it = cache->find(key);
    if (it == cache->end()) {
        if (cache->size() > 240) cache->clear();   // bounded memory (~70 MB worst case at poster sizes)
        Image img;
        img.w = r.w;
        img.h = r.h;
        img.px.assign(size_t(r.w) * r.h, 0xFF000000u);
        Canvas ic(img.fb());
        renderArt(ic, f, {0, 0, r.w, r.h}, 0, seed, title, showTitle);
        it = cache->emplace(key, std::move(img)).first;
    }
    c.drawImage(it->second, r.x, r.y, radius);
}

static void renderArt(Canvas &c, Fonts &f, Rect r, int radius, const std::string &seed, const std::string &title, bool showTitle)
{
    uint32_t h = hashStr(seed);
    // hues stay in the cool night range (blue/violet/teal/magenta) to match the identity
    float hue = 190.0f + float(h % 140);
    Color a = hsv(hue, 0.65f, 0.55f), b = hsv(hue + 35.0f, 0.75f, 0.16f);
    c.fillGradient(r.x, r.y, r.w, r.h, radius, a, b, 2);
    c.pushClip(r);
    // decorative orbits / moons
    float cx = r.x + r.w * (0.25f + float((h >> 8) % 50) / 100.0f);
    float cy = r.y + r.h * (0.18f + float((h >> 16) % 30) / 100.0f);
    float rad = std::min(r.w, r.h) * (0.22f + float((h >> 4) % 12) / 100.0f);
    c.fillCircle(cx, cy, rad, rgba(255, 255, 255, 34));
    c.fillCircle(cx + rad * 0.35f, cy - rad * 0.2f, rad * 0.9f, withAlpha(a, 140));
    c.strokeCircle(cx, cy, rad * 1.6f, 2.0f, rgba(255, 255, 255, 24));
    c.strokeCircle(cx, cy, rad * 2.3f, 1.5f, rgba(255, 255, 255, 14));
    for (int i = 0; i < 18; i++) {   // stars
        uint32_t s = h * (i + 7) * 2654435761u;
        c.fillCircle(r.x + float(s % uint32_t(std::max(1, r.w))), r.y + float((s >> 12) % uint32_t(std::max(1, r.h / 2))),
                     1.0f + float((s >> 24) % 3) * 0.5f, rgba(255, 255, 255, 90 + int((s >> 20) % 100)));
    }
    c.fillGradient(r.x, r.y + r.h / 2, r.w, r.h / 2, 0, rgba(0, 0, 0, 0), rgba(4, 4, 12, 220), 1);
    if (showTitle && f.bold) {
        int px = std::max(18, std::min(40, r.w / 7));
        auto lines = f.bold->wrap(px, title, r.w - 32, 3);
        int lh = f.bold->lineHeight(px);
        int y = r.bottom() - 18 - lh * int(lines.size());
        for (const auto &l : lines) { f.bold->draw(c, r.x + 16, y, px, l, rgba(255, 255, 255, 235)); y += lh; }
    }
    c.popClip();
}

void drawIcon(Canvas &c, Icon icon, float x, float y, float s, Color col)
{
    float t = std::max(2.0f, s * 0.09f);   // stroke
    float h = s / 2;
    switch (icon) {
    case Icon::Home: {
        float roof[] = {x - h, y - h * 0.05f, x, y - h * 0.95f, x + h, y - h * 0.05f};
        c.line(roof[0], roof[1], roof[2], roof[3], t, col);
        c.line(roof[2], roof[3], roof[4], roof[5], t, col);
        c.strokeRoundRect(int(x - h * 0.7f), int(y - h * 0.15f), int(h * 1.4f), int(h * 1.05f), 3, int(t), col);
        break;
    }
    case Icon::Music:
        c.fillCircle(x - h * 0.45f, y + h * 0.55f, h * 0.32f, col);
        c.fillCircle(x + h * 0.55f, y + h * 0.35f, h * 0.32f, col);
        c.line(x - h * 0.17f, y + h * 0.55f, x - h * 0.17f, y - h * 0.8f, t, col);
        c.line(x + h * 0.83f, y + h * 0.35f, x + h * 0.83f, y - h * 0.95f, t, col);
        c.line(x - h * 0.17f, y - h * 0.8f, x + h * 0.83f, y - h * 0.95f, t * 1.6f, col);
        break;
    case Icon::Film:
        c.strokeRoundRect(int(x - h * 0.9f), int(y - h * 0.65f), int(h * 1.8f), int(h * 1.3f), 4, int(t), col);
        for (int i = 0; i < 4; i++) {
            float px = x - h * 0.62f + i * h * 0.41f;
            c.fillRect(int(px), int(y - h * 0.5f), int(t), int(t), col);
            c.fillRect(int(px), int(y + h * 0.42f), int(t), int(t), col);
        }
        break;
    case Icon::Series:
        c.strokeRoundRect(int(x - h * 0.9f), int(y - h * 0.55f), int(h * 1.8f), int(h * 1.15f), 5, int(t), col);
        c.line(x - h * 0.45f, y + h * 0.85f, x + h * 0.45f, y + h * 0.85f, t, col);
        break;
    case Icon::Live:
        c.fillCircle(x, y + h * 0.1f, h * 0.18f, col);
        c.strokeCircle(x, y + h * 0.1f, h * 0.55f, t, col);
        c.strokeCircle(x, y + h * 0.1f, h * 0.92f, t, withAlpha(col, int((col >> 24) * 0.6f)));
        break;
    case Icon::Library: {
        float bm[] = {x - h * 0.6f, y - h * 0.9f, x + h * 0.6f, y - h * 0.9f, x + h * 0.6f, y + h * 0.9f,
                      x, y + h * 0.45f, x - h * 0.6f, y + h * 0.9f};
        for (int i = 0; i < 5; i++) {
            int j = (i + 1) % 5;
            c.line(bm[2 * i], bm[2 * i + 1], bm[2 * j], bm[2 * j + 1], t, col);
        }
        break;
    }
    case Icon::Sources:
        c.strokeRoundRect(int(x - h * 0.55f), int(y - h * 0.3f), int(h * 1.1f), int(h * 0.9f), 6, int(t), col);
        c.line(x - h * 0.25f, y - h * 0.3f, x - h * 0.25f, y - h * 0.85f, t, col);
        c.line(x + h * 0.25f, y - h * 0.3f, x + h * 0.25f, y - h * 0.85f, t, col);
        c.line(x, y + h * 0.6f, x, y + h * 0.95f, t, col);
        break;
    case Icon::Settings:
        for (int i = 0; i < 8; i++) {
            float a = float(i) * 3.14159f / 4;
            c.line(x + std::cos(a) * h * 0.55f, y + std::sin(a) * h * 0.55f, x + std::cos(a) * h * 0.92f,
                   y + std::sin(a) * h * 0.92f, t * 1.6f, col);
        }
        c.strokeCircle(x, y, h * 0.62f, t, col);
        c.strokeCircle(x, y, h * 0.25f, t, col);
        break;
    case Icon::Search:
        c.strokeCircle(x - h * 0.15f, y - h * 0.15f, h * 0.6f, t, col);
        c.line(x + h * 0.3f, y + h * 0.3f, x + h * 0.85f, y + h * 0.85f, t * 1.3f, col);
        break;
    case Icon::Play: {
        float p[] = {x - h * 0.55f, y - h * 0.75f, x + h * 0.75f, y, x - h * 0.55f, y + h * 0.75f};
        c.fillConvex(p, 3, col);
        break;
    }
    case Icon::Pause:
        c.fillRoundRect(int(x - h * 0.6f), int(y - h * 0.7f), int(h * 0.42f), int(h * 1.4f), 2, col);
        c.fillRoundRect(int(x + h * 0.18f), int(y - h * 0.7f), int(h * 0.42f), int(h * 1.4f), 2, col);
        break;
    case Icon::Heart:
    case Icon::HeartFilled: {
        if (icon == Icon::HeartFilled) {
            c.fillCircle(x - h * 0.38f, y - h * 0.22f, h * 0.42f, col);
            c.fillCircle(x + h * 0.38f, y - h * 0.22f, h * 0.42f, col);
            float p[] = {x - h * 0.78f, y - h * 0.08f, x + h * 0.78f, y - h * 0.08f, x, y + h * 0.82f};
            c.fillConvex(p, 3, col);
        } else {
            c.strokeCircle(x - h * 0.38f, y - h * 0.22f, h * 0.42f, t, col);
            c.strokeCircle(x + h * 0.38f, y - h * 0.22f, h * 0.42f, t, col);
            c.line(x - h * 0.76f, y - h * 0.05f, x, y + h * 0.82f, t, col);
            c.line(x + h * 0.76f, y - h * 0.05f, x, y + h * 0.82f, t, col);
        }
        break;
    }
    case Icon::Shuffle:
        c.line(x - h * 0.9f, y - h * 0.5f, x + h * 0.7f, y + h * 0.5f, t, col);
        c.line(x - h * 0.9f, y + h * 0.5f, x + h * 0.7f, y - h * 0.5f, t, col);
        c.line(x + h * 0.4f, y - h * 0.75f, x + h * 0.8f, y - h * 0.5f, t, col);
        c.line(x + h * 0.4f, y + h * 0.75f, x + h * 0.8f, y + h * 0.5f, t, col);
        break;
    case Icon::Repeat:
        c.strokeRoundRect(int(x - h * 0.85f), int(y - h * 0.5f), int(h * 1.7f), int(h * 1.0f), int(h * 0.4f), int(t), col);
        c.line(x + h * 0.1f, y - h * 0.75f, x + h * 0.4f, y - h * 0.5f, t, col);
        break;
    case Icon::Next:
    case Icon::Prev: {
        float d = icon == Icon::Next ? 1.0f : -1.0f;
        float p[] = {x - d * h * 0.6f, y - h * 0.6f, x + d * h * 0.3f, y, x - d * h * 0.6f, y + h * 0.6f};
        c.fillConvex(p, 3, col);
        c.fillRect(int(x + d * h * 0.42f - t / 2), int(y - h * 0.6f), int(t), int(h * 1.2f), col);
        break;
    }
    case Icon::Plus:
        c.line(x - h * 0.7f, y, x + h * 0.7f, y, t * 1.2f, col);
        c.line(x, y - h * 0.7f, x, y + h * 0.7f, t * 1.2f, col);
        break;
    case Icon::Volume: {
        float p[] = {x - h * 0.85f, y - h * 0.3f, x - h * 0.4f, y - h * 0.3f, x + h * 0.05f, y - h * 0.75f,
                     x + h * 0.05f, y + h * 0.75f, x - h * 0.4f, y + h * 0.3f, x - h * 0.85f, y + h * 0.3f};
        c.fillConvex(p, 6, col);
        c.strokeCircle(x + h * 0.05f, y, h * 0.5f, t, withAlpha(col, 160));
        break;
    }
    case Icon::Info:
        c.strokeCircle(x, y, h * 0.9f, t, col);
        c.fillCircle(x, y - h * 0.42f, t * 0.8f, col);
        c.line(x, y - h * 0.1f, x, y + h * 0.5f, t * 1.1f, col);
        break;
    case Icon::Warning: {
        float p[] = {x, y - h * 0.9f, x + h * 0.95f, y + h * 0.75f, x - h * 0.95f, y + h * 0.75f};
        c.fillConvex(p, 3, col);
        c.line(x, y - h * 0.35f, x, y + h * 0.2f, t * 1.1f, theme::Bg);
        c.fillCircle(x, y + h * 0.48f, t * 0.7f, theme::Bg);
        break;
    }
    }
}

static const char *pillLabel(uint32_t b)
{
    switch (b) {
    case BtnL1: return "L1";
    case BtnR1: return "R1";
    case BtnL2: return "L2";
    case BtnR2: return "R2";
    case BtnOptions: return "OPTIONS";
    case BtnR3: return "R3";
    case BtnL3: return "L3";
    case BtnL1 | BtnR1: return "L1/R1";
    case BtnL2 | BtnR2: return "L2/R2";
    case BtnUp | BtnDown | BtnLeft | BtnRight: return "D-PAD";
    default: return "?";
    }
}

static bool isShapeButton(uint32_t b) { return b == BtnCross || b == BtnCircle || b == BtnSquare || b == BtnTriangle; }

static int glyphWidth(Fonts &f, uint32_t b, int s)
{
    return isShapeButton(b) ? s : f.bold->measure(int(s * 0.45f), pillLabel(b)) + s / 2;
}

int drawButtonGlyph(Canvas &c, Fonts &f, uint32_t b, int x, int y, int s)
{
    float cx = x + s / 2.0f, cy = y + s / 2.0f, h = s / 2.0f;
    Color ring = rgba(255, 255, 255, 60), fg = theme::Text;
    auto disc = [&] { c.fillCircle(cx, cy, h, rgba(255, 255, 255, 24)); c.strokeCircle(cx, cy, h, 1.5f, ring); };
    switch (b) {
    case BtnCross:
        disc();
        c.line(cx - h * 0.38f, cy - h * 0.38f, cx + h * 0.38f, cy + h * 0.38f, 2.2f, rgba(140, 170, 255));
        c.line(cx - h * 0.38f, cy + h * 0.38f, cx + h * 0.38f, cy - h * 0.38f, 2.2f, rgba(140, 170, 255));
        return s;
    case BtnCircle:
        disc();
        c.strokeCircle(cx, cy, h * 0.45f, 2.2f, rgba(255, 120, 140));
        return s;
    case BtnSquare:
        disc();
        c.strokeRoundRect(int(cx - h * 0.38f), int(cy - h * 0.38f), int(h * 0.76f), int(h * 0.76f), 1, 2, rgba(240, 140, 220));
        return s;
    case BtnTriangle: {
        disc();
        float t0x = cx, t0y = cy - h * 0.45f, t1x = cx + h * 0.45f, t1y = cy + h * 0.32f, t2x = cx - h * 0.45f;
        c.line(t0x, t0y, t1x, t1y, 2.2f, rgba(110, 230, 200));
        c.line(t1x, t1y, t2x, t1y, 2.2f, rgba(110, 230, 200));
        c.line(t2x, t1y, t0x, t0y, 2.2f, rgba(110, 230, 200));
        return s;
    }
    default: {
        const char *label = pillLabel(b);
        int px = int(s * 0.45f);
        int w = glyphWidth(f, b, s);
        c.fillRoundRect(x, y + s / 8, w, s - s / 4, (s - s / 4) / 2, rgba(255, 255, 255, 24));
        c.strokeRoundRect(x, y + s / 8, w, s - s / 4, (s - s / 4) / 2, 1, ring);
        f.bold->draw(c, x + s / 4, y + (s - f.bold->lineHeight(px)) / 2, px, label, fg);
        return w;
    }
    }
}

void drawHints(Canvas &c, Fonts &f, const std::vector<std::pair<uint32_t, std::string>> &hints, int xRight, int y)
{
    const int s = 34, px = 22;
    int x = xRight;
    for (auto it = hints.rbegin(); it != hints.rend(); ++it) {
        int tw = f.regular->measure(px, it->second);
        x -= tw;
        f.regular->draw(c, x, y + (s - f.regular->lineHeight(px)) / 2, px, it->second, theme::TextDim);
        x -= 10;
        int gw = glyphWidth(f, it->first, s);
        x -= gw;
        drawButtonGlyph(c, f, it->first, x, y, s);
        x -= 34;
    }
}

void drawProgress(Canvas &c, int x, int y, int w, int h, double frac, Color fg, Color bg)
{
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    c.fillRoundRect(x, y, w, h, h / 2, bg);
    int fw = int(w * frac);
    if (fw > 0) c.fillGradient(x, y, std::max(fw, h), h, h / 2, theme::Violet, fg, 0);
}

void drawFocusRing(Canvas &c, Rect r, int radius)
{
    c.shadow(r.x, r.y, r.w, r.h, radius, 26, withAlpha(theme::Glow, 110));
    c.strokeRoundRect(r.x - 4, r.y - 4, r.w + 8, r.h + 8, radius + 4, 3, rgba(196, 181, 253));
}

std::string formatTime(int64_t ms)
{
    if (ms < 0) ms = 0;
    int64_t s = ms / 1000;
    char b[32];
    if (s >= 3600) std::snprintf(b, sizeof b, "%lld:%02lld:%02lld", (long long)(s / 3600), (long long)(s / 60 % 60), (long long)(s % 60));
    else std::snprintf(b, sizeof b, "%lld:%02lld", (long long)(s / 60), (long long)(s % 60));
    return b;
}

// ---------------------------------------------------------------- keyboard
void Keyboard::open(std::string title, std::string initial, bool urlMode)
{
    title_ = std::move(title);
    text_ = std::move(initial);
    urlMode_ = urlMode;
    active_ = true;
    upper_ = false;
    row_ = col_ = 0;
}

std::vector<std::vector<std::string>> Keyboard::rows() const
{
    auto split = [](const char *s) {
        std::vector<std::string> v;
        for (const char *p = s; *p; p++) v.push_back(std::string(1, *p));
        return v;
    };
    std::vector<std::vector<std::string>> r;
    r.push_back(split("1234567890"));
    r.push_back(split(upper_ ? "QWERTYUIOP" : "qwertyuiop"));
    r.push_back(split(upper_ ? "ASDFGHJKL-" : "asdfghjkl-"));
    r.push_back(split(upper_ ? "ZXCVBNM_.," : "zxcvbnm_.,"));
    if (urlMode_) {
        r.push_back(split(":/?&=%#@~+"));
        r.push_back({"https://", ".com", ".com.br", ".app", "/manifest.json"});
    }
    r.push_back({"Maiusc", "Espaco", "Apagar", "OK"});
    return r;
}

Keyboard::Result Keyboard::handle(uint32_t p)
{
    auto rs = rows();
    if (p & BtnUp) row_ = (row_ + int(rs.size()) - 1) % int(rs.size());
    if (p & BtnDown) row_ = (row_ + 1) % int(rs.size());
    int n = int(rs[size_t(row_)].size());
    if (col_ >= n) col_ = n - 1;
    if (p & BtnLeft) col_ = (col_ + n - 1) % n;
    if (p & BtnRight) col_ = (col_ + 1) % n;
    if (p & BtnSquare) {
        if (!text_.empty()) {
            size_t i = text_.size() - 1;
            while (i > 0 && (static_cast<unsigned char>(text_[i]) & 0xC0) == 0x80) i--;
            text_.erase(i);
        }
    }
    if (p & BtnTriangle) text_ += ' ';
    if (p & BtnL1) upper_ = !upper_;
    if (p & BtnCircle) { active_ = false; return Result::Cancelled; }
    if (p & BtnOptions) { active_ = false; return Result::Done; }
    if (p & BtnCross) {
        const std::string &k = rs[size_t(row_)][size_t(col_)];
        if (k == "OK") { active_ = false; return Result::Done; }
        if (k == "Apagar") return handle(BtnSquare);
        if (k == "Espaco") text_ += ' ';
        else if (k == "Maiusc") upper_ = !upper_;
        else if (text_.size() < 2048) text_ += k;
    }
    return Result::Editing;
}

void Keyboard::draw(Canvas &c, Fonts &f)
{
    if (!active_) return;
    c.fillRect(0, 0, c.width(), c.height(), rgba(0, 0, 0, 170));
    auto rs = rows();
    const int key = 92, gap = 12;
    int kw = 10 * key + 9 * gap;
    int panelW = kw + 120, panelH = 260 + int(rs.size()) * (key + gap);
    int px = (c.width() - panelW) / 2, py = (c.height() - panelH) / 2;
    c.shadow(px, py, panelW, panelH, 28, 40, rgba(0, 0, 0, 160));
    c.fillRoundRect(px, py, panelW, panelH, 28, theme::Surface);
    c.strokeRoundRect(px, py, panelW, panelH, 28, 1, theme::Line);
    f.bold->draw(c, px + 60, py + 36, 34, title_, theme::Text);
    // text field
    int fx = px + 60, fy = py + 96, fw = kw, fh = 76;
    c.fillRoundRect(fx, fy, fw, fh, 16, theme::Bg);
    c.strokeRoundRect(fx, fy, fw, fh, 16, 2, theme::Violet);
    std::string shown = text_;
    while (f.regular->measure(32, shown + "|") > fw - 40 && !shown.empty()) shown.erase(0, 1);
    int tw = f.regular->draw(c, fx + 20, fy + (fh - f.regular->lineHeight(32)) / 2, 32, shown, theme::Text);
    c.fillRect(fx + 22 + tw, fy + 18, 3, fh - 36, theme::Glow);
    int y = fy + fh + 36;
    for (size_t r = 0; r < rs.size(); r++) {
        int n = int(rs[r].size());
        int w = n == 10 ? key : (kw - gap * (n - 1)) / n;
        int x = fx;
        for (int i = 0; i < n; i++) {
            bool foc = int(r) == row_ && i == col_;
            if (foc) c.fillGradient(x, y, w, key, 14, theme::Violet, theme::Blue, 2);
            else c.fillRoundRect(x, y, w, key, 14, theme::SurfaceHi);
            int ts = rs[r][size_t(i)].size() > 1 ? 26 : 36;
            Font *fn = foc ? f.bold : f.regular;
            int lw = fn->measure(ts, rs[r][size_t(i)]);
            fn->draw(c, x + (w - lw) / 2, y + (key - fn->lineHeight(ts)) / 2, ts, rs[r][size_t(i)], theme::Text);
            x += w + gap;
        }
        y += key + gap;
    }
    drawHints(c, f, {{BtnSquare, "Apagar"}, {BtnTriangle, "Espaco"}, {BtnL1, "Maiusc"}, {BtnOptions, "Confirmar"}, {BtnCircle, "Cancelar"}},
              px + panelW - 60, py + panelH - 62);
}

}  // namespace ml::ui
