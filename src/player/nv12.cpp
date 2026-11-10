#include "player/nv12.hpp"

#include <algorithm>
#include <vector>

namespace ml {

bool nv12ToArgb(const Nv12Frame &s, Framebuffer d)
{
    if (!s.data || s.width <= 0 || s.height <= 0 || s.pitch < s.width || s.alignedHeight < s.height || !d.pixels) return false;
    // fit inside the destination keeping the aspect ratio
    int64_t outW = d.width, outH = int64_t(d.width) * s.height / s.width;
    if (outH > d.height) { outH = d.height; outW = int64_t(d.height) * s.width / s.height; }
    int x0 = int((d.width - outW) / 2), y0 = int((d.height - outH) / 2);
    const uint8_t *yPlane = s.data, *uvPlane = s.data + size_t(s.pitch) * s.alignedHeight;
    // column mapping is the same for every row
    std::vector<int> srcX(static_cast<size_t>(outW));
    for (int64_t x = 0; x < outW; x++) srcX[size_t(x)] = int(x * s.width / outW);
    for (int y = 0; y < d.height; y++) {
        uint32_t *row = d.pixels + size_t(y) * d.stride;
        if (y < y0 || y >= y0 + outH) { std::fill_n(row, d.width, 0xFF000000u); continue; }
        int sy = int(int64_t(y - y0) * s.height / outH);
        const uint8_t *yr = yPlane + size_t(sy) * s.pitch;
        const uint8_t *uvr = uvPlane + size_t(sy / 2) * s.pitch;
        std::fill_n(row, x0, 0xFF000000u);
        std::fill_n(row + x0 + outW, d.width - x0 - outW, 0xFF000000u);
        uint32_t *o = row + x0;
        for (int64_t x = 0; x < outW; x++) {
            int sx = srcX[size_t(x)];
            int Y = (int(yr[sx]) - 16) * 298;
            int U = int(uvr[sx & ~1]) - 128, V = int(uvr[(sx & ~1) + 1]) - 128;
            // BT.709 limited range, fixed point (x256)
            int r = (Y + 459 * V + 128) >> 8;
            int g = (Y - 55 * U - 136 * V + 128) >> 8;
            int b = (Y + 541 * U + 128) >> 8;
            r = r < 0 ? 0 : (r > 255 ? 255 : r);
            g = g < 0 ? 0 : (g > 255 ? 255 : g);
            b = b < 0 ? 0 : (b > 255 ? 255 : b);
            o[x] = 0xFF000000u | uint32_t(r) << 16 | uint32_t(g) << 8 | uint32_t(b);
        }
    }
    return true;
}

}  // namespace ml
