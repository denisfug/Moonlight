// Software renderer for a 0xAARRGGBB framebuffer: anti-aliased rounded shapes, gradients,
// clipping. Everything the UI draws goes through here (portable, tested on PC).
#pragma once
#include <cstdint>
#include <vector>

#include "platform/platform.hpp"

namespace ml::ui {

using Color = uint32_t;   // 0xAARRGGBB

constexpr Color rgba(int r, int g, int b, int a = 255)
{
    return (uint32_t(a) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
}
Color withAlpha(Color c, int a);   // replaces alpha (0..255)
Color mix(Color a, Color b, float t);

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    int right() const { return x + w; }
    int bottom() const { return y + h; }
};

// Owned 0xAARRGGBB pixel buffer (cached artwork, backgrounds).
struct Image {
    int w = 0, h = 0;
    std::vector<uint32_t> px;
    Framebuffer fb() { return {px.data(), w, h, w}; }
    bool empty() const { return px.empty(); }
};

class Canvas {
public:
    explicit Canvas(Framebuffer fb);
    int width() const { return fb_.width; }
    int height() const { return fb_.height; }

    void clear(Color c);
    void pushClip(Rect r);   // intersected with the current clip
    void popClip();

    void fillRect(int x, int y, int w, int h, Color c);
    void fillRoundRect(int x, int y, int w, int h, int radius, Color c);
    // Gradient from c0 to c1; angle: 0 = left->right, 1 = top->bottom, 2 = diagonal.
    void fillGradient(int x, int y, int w, int h, int radius, Color c0, Color c1, int dir);
    void strokeRoundRect(int x, int y, int w, int h, int radius, int thickness, Color c);
    void fillCircle(float cx, float cy, float r, Color c);
    void strokeCircle(float cx, float cy, float r, float thickness, Color c);
    // Soft drop shadow / glow around a rounded rect.
    void shadow(int x, int y, int w, int h, int radius, int spread, Color c);
    void line(float x0, float y0, float x1, float y1, float thickness, Color c);
    // Convex polygon (points in either winding order), anti-aliased edges.
    void fillConvex(const float *xy, int points, Color c);
    // Coverage-mask blit (used by text): mask is w*h alpha values.
    void blendMask(int x, int y, int w, int h, const uint8_t *mask, int maskStride, Color c);

    void blendPixel(int x, int y, Color c, int coverage /*0..255*/);
    // Draws an image with optional rounded corners (opaque images are copied, not blended).
    void drawImage(const Image &img, int x, int y, int radius = 0);

private:
    Framebuffer fb_;
    Rect clip_;
    std::vector<Rect> stack_;
    template <class F> void shape(int x, int y, int w, int h, int radius, F colorAt);
};

}  // namespace ml::ui
