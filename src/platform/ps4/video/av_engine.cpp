// Video engine on libSceAvPlayer (system decoder: MP4 H.264/AAC, hardware accelerated).
// EXPERIMENTAL: written from the documented ABI (avplayer_api.h); every step reports its return code
// through status() so the first console tests tell us exactly where it stops.
#include <orbis/Sysmodule.h>
#include <orbis/libkernel.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "platform/ps4/ps4_platform.hpp"
#include "platform/ps4/video/avplayer_api.h"
#include "player/nv12.hpp"

namespace ml::ps4 {

namespace {

// ---- memory callbacks ---------------------------------------------------------------------------
void *avAlloc(void *, uint32_t align, uint32_t size)
{
    if (align < 16) align = 16;
    void *p = nullptr;
    if (posix_memalign(&p, align, size) != 0) return nullptr;
    return p;
}
void avFree(void *, void *mem) { std::free(mem); }

// Decoder output must live in direct (GPU-visible) memory.
struct TexAlloc {
    void *addr;
    off_t off;
    size_t len;
};
TexAlloc g_tex[64];   // plain array: no static constructor

void *avAllocTex(void *, uint32_t align, uint32_t size)
{
    const size_t page = 64 * 1024;
    size_t a = align < page ? page : align;
    size_t len = (size + page - 1) / page * page;
    off_t off = 0;
    if (sceKernelAllocateDirectMemory(0, off_t(sceKernelGetDirectMemorySize()), len, a, 3, &off) < 0) return nullptr;
    void *addr = nullptr;
    if (sceKernelMapDirectMemory(&addr, len, 0x33, 0, off, a) < 0) { sceKernelReleaseDirectMemory(off, len); return nullptr; }
    for (auto &t : g_tex)
        if (!t.addr) { t = {addr, off, len}; return addr; }
    sceKernelMunmap(addr, len);
    sceKernelReleaseDirectMemory(off, len);
    return nullptr;
}
void avFreeTex(void *, void *mem)
{
    for (auto &t : g_tex)
        if (t.addr == mem) {
            sceKernelMunmap(t.addr, t.len);
            sceKernelReleaseDirectMemory(t.off, t.len);
            t = {nullptr, 0, 0};
            return;
        }
}

std::string hex(int v)
{
    char b[16];
    std::snprintf(b, sizeof b, "0x%08X", unsigned(v));
    return b;
}

class AvEngine : public VideoEngine {
public:
    explicit AvEngine(Ps4Platform &p) : plat_(p) {}
    ~AvEngine() override { stop(); }

    bool open(const std::string &location, std::string *error) override
    {
        stop();
        MlAvInitData init;
        std::memset(&init, 0, sizeof init);
        init.memory = {this, avAlloc, avFree, avAllocTex, avFreeTex};
        init.event = {this, &AvEngine::onEvent};
        init.debugLevel = 0;
        init.basePriority = 700;
        init.numOutputVideoFramebuffers = 2;
        init.autoStart = true;
        init.defaultLanguage = "por";
        h_ = sceAvPlayerInit(&init);
        if (!h_) { log("sceAvPlayerInit falhou"); if (error) *error = status_; return false; }
        int r = sceAvPlayerAddSource(h_, location.c_str());
        if (r < 0) {
            log("AddSource " + hex(r));
            if (error) *error = "o player do sistema recusou o arquivo (" + hex(r) + ")";
            sceAvPlayerClose(h_);
            h_ = nullptr;
            return false;
        }
        log("fonte adicionada, aguardando o decodificador");
        quit_ = false;
        audioRunning_ = true;
        if (!plat_.startThread(&AvEngine::audioMain, this, "ml_av_audio")) audioRunning_ = false;
        return true;
    }

    void pause() override { if (h_ && sceAvPlayerPause(h_) >= 0) paused_ = true; }
    void resume() override { if (h_ && sceAvPlayerResume(h_) >= 0) paused_ = false; }

    void stop() override
    {
        if (!h_) return;
        quit_ = true;
        for (int i = 0; i < 300 && audioRunning_.load(); i++) sceKernelUsleep(5000);
        sceAvPlayerStop(h_);
        sceAvPlayerClose(h_);
        h_ = nullptr;
        paused_ = false;
    }

    bool seek(int64_t ms) override
    {
        if (!h_) return false;
        if (ms < 0) ms = 0;
        return sceAvPlayerJumpToTime(h_, uint64_t(ms)) >= 0;
    }

    int64_t positionMs() override { return h_ ? int64_t(sceAvPlayerCurrentTime(h_)) : 0; }

    int64_t durationMs() override
    {
        if (!h_) return 0;
        if (duration_ > 0) return duration_;
        int n = sceAvPlayerStreamCount(h_);
        for (int i = 0; i < n; i++) {
            MlAvStreamInfo si;
            std::memset(&si, 0, sizeof si);
            if (sceAvPlayerGetStreamInfo(h_, uint32_t(i), &si) >= 0 && si.duration > 0) duration_ = int64_t(si.duration);
        }
        return duration_;
    }

    bool paused() override { return paused_; }
    bool finished() override { return !h_ || (gotFrame_ && !sceAvPlayerIsActive(h_)); }

    bool updateFrame(Framebuffer fb) override
    {
        if (!h_) return false;
        MlAvFrameInfoEx f;
        std::memset(&f, 0, sizeof f);
        if (!sceAvPlayerGetVideoDataEx(h_, &f) || !f.data) return false;
        const MlAvVideoEx &v = f.details.video;
        Nv12Frame src;
        src.data = static_cast<const uint8_t *>(f.data);
        src.pitch = int(v.pitch ? v.pitch : v.width);
        src.alignedHeight = int(v.height);
        src.width = int(v.width) - int(v.cropLeft + v.cropRight);
        src.height = int(v.height) - int(v.cropTop + v.cropBottom);
        if (!gotFrame_) {
            gotFrame_ = true;
            log("video " + std::to_string(src.width) + "x" + std::to_string(src.height) + " pitch " + std::to_string(src.pitch));
        }
        return nv12ToArgb(src, fb);
    }

    std::string status() override { return status_; }

private:
    Ps4Platform &plat_;
    MlAvPlayerHandle h_ = nullptr;
    std::atomic<bool> quit_{false}, audioRunning_{false};
    bool paused_ = false, gotFrame_ = false;
    int64_t duration_ = 0;
    std::string status_;

    void log(const std::string &s) { status_ = s; }

    static void onEvent(void *obj, int32_t ev, int32_t, void *)
    {
        auto *self = static_cast<AvEngine *>(obj);
        switch (ev) {
        case MlAvStateReady: self->log("pronto"); break;
        case MlAvStatePlay: self->log("tocando"); break;
        case MlAvStateBuffering: self->log("carregando..."); break;
        case MlAvStateStop: self->log("parado"); break;
        case MlAvEncryption:
        case MlAvDrmError: self->log("conteudo protegido (DRM): nao suportado"); break;
        case MlAvWarning: self->log("aviso do decodificador"); break;
        default: break;
        }
    }

    static void audioMain(void *p) { static_cast<AvEngine *>(p)->audioLoop(); }

    // Pulls decoded PCM and feeds a 48 kHz stereo port in 1024-frame blocks.
    void audioLoop()
    {
        std::unique_ptr<AudioOut> out = plat_.makeAudioOut();
        bool ok = out && out->open();
        const size_t block = ok ? size_t(out->framesPerBlock()) : 1024;
        std::vector<int16_t> pending;
        pending.reserve(block * 4);
        while (!quit_.load()) {
            MlAvFrameInfo a;
            std::memset(&a, 0, sizeof a);
            if (!h_ || !sceAvPlayerGetAudioData(h_, &a) || !a.data || a.details.audio.size == 0) {
                sceKernelUsleep(2000);
                continue;
            }
            const MlAvAudio &d = a.details.audio;
            if (d.sampleRate != 48000) { log("audio em " + std::to_string(d.sampleRate) + " Hz: ainda nao suportado"); continue; }
            const auto *pcm = reinterpret_cast<const int16_t *>(a.data);
            size_t ch = d.channelCount ? d.channelCount : 2;
            size_t frames = d.size / (2 * ch);
            for (size_t i = 0; i < frames; i++) {   // downmix to stereo: first two channels
                pending.push_back(pcm[i * ch]);
                pending.push_back(pcm[i * ch + (ch > 1 ? 1 : 0)]);
            }
            while (ok && pending.size() >= block * 2) {
                out->write(pending.data());
                pending.erase(pending.begin(), pending.begin() + long(block * 2));
            }
        }
        if (out) out->close();
        audioRunning_ = false;
    }
};

}  // namespace

std::unique_ptr<VideoEngine> Ps4Platform::makeVideoEngine()
{
    if (sceSysmoduleLoadModule(ORBIS_SYSMODULE_AV_PLAYER) < 0) return nullptr;
    return std::unique_ptr<VideoEngine>(new AvEngine(*this));
}

}  // namespace ml::ps4
