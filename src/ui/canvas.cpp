#include "ui/canvas.hpp"

#include <algorithm>
#include <cmath>

namespace ml::ui {

Color withAlpha(Color c, int a) { return (c & 0x00FFFFFFu) | (uint32_t(std::clamp(a, 0, 255)) << 24); }

Color mix(Color a, Color b, float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    auto ch = [&](int s) {
        float x = float((a >> s) & 0xFF), y = float((b >> s) & 0xFF);
        return uint32_t(x + (y - x) * t + 0.5f) << s;
    };
    return ch(24) | ch(16) | ch(8) | ch(0);
}

Canvas::Canvas(Framebuffer fb) : fb_(fb), clip_{0, 0, fb.width, fb.height} {}

void Canvas::clear(Color c)
{
    for (int y = 0; y < fb_.height; y++) std::fill_n(fb_.pixels + size_t(y) * fb_.stride, fb_.width, c | 0xFF000000u);
}

void Canvas::pushClip(Rect r)
{
    stack_.push_back(clip_);
    int x0 = std::max(clip_.x, r.x), y0 = std::max(clip_.y, r.y);
    int x1 = std::min(clip_.right(), r.right()), y1 = std::min(clip_.bottom(), r.bottom());
    clip_ = {x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
}

void Canvas::popClip()
{
    if (stack_.empty()) return;
    clip_ = stack_.back();
    stack_.pop_back();
}

void Canvas::blendPixel(int x, int y, Color c, int cov)
{
    if (x < clip_.x || y < clip_.y || x >= clip_.right() || y >= clip_.bottom()) return;
    uint32_t a = ((c >> 24) * uint32_t(cov) + 127) / 255;
    if (a == 0) return;
    uint32_t &d = fb_.pixels[size_t(y) * fb_.stride + x];
    if (a >= 255) { d = c | 0xFF000000u; return; }
    uint32_t ia = 255 - a;
    uint32_t rb = (((c & 0xFF00FF) * a + (d & 0xFF00FF) * ia) >> 8) & 0xFF00FF;
    uint32_t g = (((c & 0x00FF00) * a + (d & 0x00FF00) * ia) >> 8) & 0x00FF00;
    d = 0xFF000000u | rb | g;
}

// Rasterises a rounded rectangle with 1-px anti-aliased corners; colorAt(px, py) gives the colour.
template <class F> void Canvas::shape(int x, int y, int w, int h, int radius, F colorAt)
{
    if (w <= 0 || h <= 0) return;
    float r = float(std::min(radius, std::min(w, h) / 2));
    int y0 = std::max(y, clip_.y), y1 = std::min(y + h, clip_.bottom());
    int x0c = std::max(x, clip_.x), x1c = std::min(x + w, clip_.right());
    for (int py = y0; py < y1; py++) {
        float fy = py + 0.5f;
        // vertical distance into a corner zone
        float dy = 0;
        if (fy < y + r) dy = (y + r) - fy;
        else if (fy > y + h - r) dy = fy - (y + h - r);
        for (int px = x0c; px < x1c; px++) {
            float fx = px + 0.5f;
            int cov = 255;
            if (r > 0 && dy > 0) {
                float dx = 0;
                if (fx < x + r) dx = (x + r) - fx;
                else if (fx > x + w - r) dx = fx - (x + w - r);
                if (dx > 0) {
                    float d = std::sqrt(dx * dx + dy * dy);
                    float c = r - d + 0.5f;
                    if (c <= 0) continue;
                    if (c < 1) cov = int(c * 255);
                }
            }
            blendPixel(px, py, colorAt(px, py), cov);
        }
    }
}

void Canvas::fillRect(int x, int y, int w, int h, Color c)
{
    if ((c >> 24) == 255) {
        int x0 = std::max(x, clip_.x), x1 = std::min(x + w, clip_.right());
        int y0 = std::max(y, clip_.y), y1 = std::min(y + h, clip_.bottom());
        for (int py = y0; py < y1; py++)
            if (x1 > x0) std::fill_n(fb_.pixels + size_t(py) * fb_.stride + x0, x1 - x0, c);
        return;
    }
    shape(x, y, w, h, 0, [c](int, int) { return c; });
}

void Canvas::fillRoundRect(int x, int y, int w, int h, int radius, Color c)
{
    shape(x, y, w, h, radius, [c](int, int) { return c; });
}

void Canvas::fillGradient(int x, int y, int w, int h, int radius, Color c0, Color c1, int dir)
{
    float iw = w > 1 ? 1.0f / float(w - 1) : 0, ih = h > 1 ? 1.0f / float(h - 1) : 0;
    shape(x, y, w, h, radius, [&](int px, int py) {
        float t = dir == 0 ? (px - x) * iw : dir == 1 ? (py - y) * ih : 0.5f * ((px - x) * iw + (py - y) * ih);
        return mix(c0, c1, t);
    });
}

void Canvas::strokeRoundRect(int x, int y, int w, int h, int radius, int t, Color c)
{
    // outer shape minus inner shape, computed per pixel
    float ro = float(std::min(radius, std::min(w, h) / 2));
    float ri = std::max(0.0f, ro - t);
    int y0 = std::max(y, clip_.y), y1 = std::min(y + h, clip_.bottom());
    int x0 = std::max(x, clip_.x), x1 = std::min(x + w, clip_.right());
    auto cover = [](float fx, float fy, float bx, float by, float bw, float bh, float r) {
        float dx = std::max({bx + r - fx, 0.0f, fx - (bx + bw - r)});
        float dy = std::max({by + r - fy, 0.0f, fy - (by + bh - r)});
        if (fx < bx || fy < by || fx > bx + bw || fy > by + bh) {
            // outside the box: use distance to the box edge for AA
            float ex = std::max({bx - fx, 0.0f, fx - (bx + bw)});
            float ey = std::max({by - fy, 0.0f, fy - (by + bh)});
            return std::clamp(0.5f - std::sqrt(ex * ex + ey * ey), 0.0f, 1.0f);
        }
        if (dx > 0 && dy > 0) return std::clamp(r - std::sqrt(dx * dx + dy * dy) + 0.5f, 0.0f, 1.0f);
        float edge = std::min({fx - bx, fy - by, bx + bw - fx, by + bh - fy});
        return std::clamp(edge + 0.5f, 0.0f, 1.0f);
    };
    for (int py = y0; py < y1; py++)
        for (int px = x0; px < x1; px++) {
            float fx = px + 0.5f, fy = py + 0.5f;
            float o = cover(fx, fy, float(x), float(y), float(w), float(h), ro);
            float i = cover(fx, fy, float(x + t), float(y + t), float(w - 2 * t), float(h - 2 * t), ri);
            float c2 = o - i;
            if (c2 > 0.003f) blendPixel(px, py, c, int(c2 * 255));
        }
}

void Canvas::fillCircle(float cx, float cy, float r, Color c)
{
    int x0 = int(std::floor(cx - r - 1)), x1 = int(std::ceil(cx + r + 1));
    int y0 = int(std::floor(cy - r - 1)), y1 = int(std::ceil(cy + r + 1));
    for (int py = y0; py < y1; py++)
        for (int px = x0; px < x1; px++) {
            float dx = px + 0.5f - cx, dy = py + 0.5f - cy;
            float cov = std::clamp(r - std::sqrt(dx * dx + dy * dy) + 0.5f, 0.0f, 1.0f);
            if (cov > 0) blendPixel(px, py, c, int(cov * 255));
        }
}

void Canvas::strokeCircle(float cx, float cy, float r, float t, Color c)
{
    int x0 = int(std::floor(cx - r - 1)), x1 = int(std::ceil(cx + r + 1));
    int y0 = int(std::floor(cy - r - 1)), y1 = int(std::ceil(cy + r + 1));
    for (int py = y0; py < y1; py++)
        for (int px = x0; px < x1; px++) {
            float dx = px + 0.5f - cx, dy = py + 0.5f - cy;
            float d = std::sqrt(dx * dx + dy * dy);
            float cov = std::clamp(t * 0.5f - std::fabs(d - (r - t * 0.5f)) + 0.5f, 0.0f, 1.0f);
            if (cov > 0) blendPixel(px, py, c, int(cov * 255));
        }
}

void Canvas::shadow(int x, int y, int w, int h, int radius, int spread, Color c)
{
    // Quadratic falloff from the shape edge outward; cheap and good enough for focus glows.
    int x0 = std::max(x - spread, clip_.x), x1 = std::min(x + w + spread, clip_.right());
    int y0 = std::max(y - spread, clip_.y), y1 = std::min(y + h + spread, clip_.bottom());
    float r = float(std::min(radius, std::min(w, h) / 2));
    for (int py = y0; py < y1; py++)
        for (int px = x0; px < x1; px++) {
            float fx = px + 0.5f, fy = py + 0.5f;
            float dx = std::max({x + r - fx, 0.0f, fx - (x + w - r)});
            float dy = std::max({y + r - fy, 0.0f, fy - (y + h - r)});
            float d = std::sqrt(dx * dx + dy * dy) - r;   // <0 inside
            if (d <= 0) continue;                         // shadow only outside the shape
            float k = 1.0f - d / float(spread);
            if (k <= 0) continue;
            blendPixel(px, py, c, int(k * k * 255));
        }
}

void Canvas::line(float x0, float y0, float x1, float y1, float t, Color c)
{
    float minx = std::min(x0, x1) - t, maxx = std::max(x0, x1) + t;
    float miny = std::min(y0, y1) - t, maxy = std::max(y0, y1) + t;
    float vx = x1 - x0, vy = y1 - y0, len2 = vx * vx + vy * vy;
    for (int py = int(miny); py <= int(maxy); py++)
        for (int px = int(minx); px <= int(maxx); px++) {
            float fx = px + 0.5f - x0, fy = py + 0.5f - y0;
            float u = len2 > 0 ? std::clamp((fx * vx + fy * vy) / len2, 0.0f, 1.0f) : 0;
            float dx = fx - u * vx, dy = fy - u * vy;
            float cov = std::clamp(t * 0.5f - std::sqrt(dx * dx + dy * dy) + 0.5f, 0.0f, 1.0f);
            if (cov > 0) blendPixel(px, py, c, int(cov * 255));
        }
}

void Canvas::fillConvex(const float *xy, int n, Color c)
{
    if (n < 3) return;
    float minx = xy[0], maxx = xy[0], miny = xy[1], maxy = xy[1];
    float area = 0;
    for (int i = 0; i < n; i++) {
        minx = std::min(minx, xy[2 * i]); maxx = std::max(maxx, xy[2 * i]);
        miny = std::min(miny, xy[2 * i + 1]); maxy = std::max(maxy, xy[2 * i + 1]);
        int j = (i + 1) % n;
        area += xy[2 * i] * xy[2 * j + 1] - xy[2 * j] * xy[2 * i + 1];
    }
    float sgn = area < 0 ? -1.0f : 1.0f;
    for (int py = int(std::floor(miny)); py <= int(std::ceil(maxy)); py++)
        for (int px = int(std::floor(minx)); px <= int(std::ceil(maxx)); px++) {
            float fx = px + 0.5f, fy = py + 0.5f, d = 1e9f;
            for (int i = 0; i < n; i++) {
                int j = (i + 1) % n;
                float ex = xy[2 * j] - xy[2 * i], ey = xy[2 * j + 1] - xy[2 * i + 1];
                float len = std::sqrt(ex * ex + ey * ey);
                if (len <= 0) continue;
                float dist = sgn * (ex * (fy - xy[2 * i + 1]) - ey * (fx - xy[2 * i])) / len;   // >0 inside
                d = std::min(d, dist);
            }
            float cov = std::clamp(d + 0.5f, 0.0f, 1.0f);
            if (cov > 0) blendPixel(px, py, c, int(cov * 255));
        }
}

void Canvas::drawImage(const Image &img, int x, int y, int radius)
{
    if (img.empty()) return;
    if (radius <= 0) {   // fast path: straight row copies inside the clip
        int x0 = std::max(x, clip_.x), x1 = std::min(x + img.w, clip_.right());
        int y0 = std::max(y, clip_.y), y1 = std::min(y + img.h, clip_.bottom());
        for (int py = y0; py < y1; py++)
            if (x1 > x0) std::copy_n(img.px.data() + size_t(py - y) * img.w + (x0 - x), x1 - x0, fb_.pixels + size_t(py) * fb_.stride + x0);
        return;
    }
    // rounded: copy the interior rows directly, anti-alias only the corner bands
    int r = std::min(radius, std::min(img.w, img.h) / 2);
    int x0 = std::max(x, clip_.x), x1 = std::min(x + img.w, clip_.right());
    for (int py = std::max(y + r, clip_.y); py < std::min(y + img.h - r, clip_.bottom()); py++)
        if (x1 > x0) std::copy_n(img.px.data() + size_t(py - y) * img.w + (x0 - x), x1 - x0, fb_.pixels + size_t(py) * fb_.stride + x0);
    auto band = [&](int by, int bh) {
        Rect saved = clip_;
        pushClip({x, by, img.w, bh});
        shape(x, y, img.w, img.h, r, [&](int px, int py) { return img.px[size_t(py - y) * img.w + (px - x)]; });
        popClip();
        clip_ = saved;
    };
    band(y, r);
    band(y + img.h - r, r);
}

void Canvas::blendMask(int x, int y, int w, int h, const uint8_t *mask, int stride, Color c)
{
    for (int j = 0; j < h; j++) {
        int py = y + j;
        if (py < clip_.y || py >= clip_.bottom()) continue;
        for (int i = 0; i < w; i++) {
            uint8_t m = mask[j * stride + i];
            if (m) blendPixel(x + i, py, c, m);
        }
    }
}

}  // namespace ml::ui
