// VideoOut: two 1920x1080 linear A8R8G8B8 buffers in direct memory, flip on vsync.
// Same sequence as the OpenOrbis graphics sample; the UI renders straight into the back buffer.
#include <orbis/VideoOut.h>
#include <orbis/libkernel.h>

#include "platform/ps4/ps4_platform.hpp"

namespace ml::ps4 {

static constexpr int kW = 1920, kH = 1080;
static constexpr size_t kFrameBytes = size_t(kW) * kH * 4;

bool VideoOut::init(std::string *err)
{
    handle_ = sceVideoOutOpen(ORBIS_VIDEO_USER_MAIN, ORBIS_VIDEO_OUT_BUS_MAIN, 0, nullptr);
    if (handle_ < 0) { *err = "sceVideoOutOpen falhou (" + std::to_string(handle_) + ")"; return false; }
    auto *q = new OrbisKernelEqueue;
    if (sceKernelCreateEqueue(q, "ml_flip") < 0) { *err = "sceKernelCreateEqueue falhou"; return false; }
    flipQueue_ = q;
    sceVideoOutAddFlipEvent(*q, handle_, nullptr);

    const size_t align = 0x200000, size = (2 * kFrameBytes + align - 1) / align * align;
    off_t off = 0;
    void *mem = nullptr;
    if (sceKernelAllocateDirectMemory(0, off_t(sceKernelGetDirectMemorySize()), size, align, 3, &off) < 0) {
        *err = "sem memoria de video";
        return false;
    }
    if (sceKernelMapDirectMemory(&mem, size, 0x33, 0, off, align) < 0) { *err = "mapeamento de video falhou"; return false; }
    buf_[0] = static_cast<uint32_t *>(mem);
    buf_[1] = reinterpret_cast<uint32_t *>(static_cast<char *>(mem) + kFrameBytes);

    OrbisVideoOutBufferAttribute attr;
    // 0x80000000 = A8R8G8B8 sRGB, tiling 1 = linear (as in the OpenOrbis samples)
    sceVideoOutSetBufferAttribute(&attr, 0x80000000, 1, 0, kW, kH, kW);
    void *bufs[2] = {buf_[0], buf_[1]};
    if (sceVideoOutRegisterBuffers(handle_, 0, bufs, 2, &attr) != 0) { *err = "sceVideoOutRegisterBuffers falhou"; return false; }
    sceVideoOutSetFlipRate(handle_, 0);
    return true;
}

Framebuffer VideoOut::backBuffer() { return {buf_[cur_], kW, kH, kW}; }

void VideoOut::present()
{
    if (handle_ < 0) return;
    sceVideoOutSubmitFlip(handle_, cur_, ORBIS_VIDEO_OUT_FLIP_VSYNC, frame_);
    auto *q = static_cast<OrbisKernelEqueue *>(flipQueue_);
    for (int guard = 0; guard < 10; guard++) {   // wait for this flip (bounded)
        OrbisVideoOutFlipStatus st;
        sceVideoOutGetFlipStatus(handle_, &st);
        if (st.flipArg == frame_) break;
        OrbisKernelEvent ev;
        int n = 0;
        if (sceKernelWaitEqueue(*q, &ev, 1, &n, nullptr) != 0) break;
    }
    frame_++;
    cur_ ^= 1;
}

}  // namespace ml::ps4
