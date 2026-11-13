// NV12 (Y plane + interleaved UV, BT.709 limited range) -> 0xAARRGGBB, scaled to fit with letterboxing.
// Portable so it is unit-tested on PC; used by the PS4 video engine for decoder output.
#pragma once
#include <cstdint>

#include "platform/platform.hpp"

namespace ml {

struct Nv12Frame {
    const uint8_t *data = nullptr;   // Y plane at data, UV plane at data + pitch * alignedHeight
    int width = 0, height = 0;       // visible size
    int pitch = 0;                   // bytes per row (Y and UV)
    int alignedHeight = 0;           // rows of the Y plane in memory (often height rounded up to 16)
};

// Writes the whole destination (bars are black). Returns false for an unusable frame.
bool nv12ToArgb(const Nv12Frame &src, Framebuffer dst);

}  // namespace ml
