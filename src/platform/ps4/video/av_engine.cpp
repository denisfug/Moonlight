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

#include "net/range_reader.hpp"
#include "player/sniff.hpp"
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
        // http(s): the system player would use the system TLS, which refuses modern sites on older
        // firmware (0x8095F00C). It reads through our client instead (file-replacement callbacks + Range).
        std::string source = location;
        std::string lower = location.substr(0, 8);
        for (auto &ch : lower) ch = char(ch >= 'A' && ch <= 'Z' ? ch + 32 : ch);
        bool remote = lower.compare(0, 7, "http://") == 0 || lower.compare(0, 8, "https://") == 0;
        if (remote && location.find(".m3u8") == std::string::npos) {
            log("conectando ao servidor do video");
            reader_.reset(new RangeReader(&plat_.web(), location));
            std::string e = reader_->open();
            if (!e.empty()) {
                reader_.reset();
                log("rede: " + e);
                if (error) *error = "nao abriu o video pela internet: " + e;
                return false;
            }
            // Look at the first bytes: formats the system player cannot open get a clear message.
            uint8_t head[256] = {0};
            long hn = reader_->readAt(0, head, sizeof head);
            fmt_ = hn > 0 ? sniffContainer(head, size_t(hn)) : "";
            std::string problem = containerProblem(fmt_);
            if (!problem.empty()) {
                reader_.reset();
                log(problem);
                if (error) *error = problem;
                return false;
            }
            fetchQuit_ = false;
            fetchRunning_ = true;
            if (!plat_.startThread(&AvEngine::fetchMain, this, "ml_av_fetch")) {
                fetchRunning_ = false;
                reader_.reset();
                if (error) *error = "nao iniciou a leitura do video";
                return false;
            }
            init.file = {this, &AvEngine::fOpen, &AvEngine::fClose, &AvEngine::fRead, &AvEngine::fSize};
            source = "/ml_stream/video" + (fmt_ == "mp4" ? std::string(".mp4") : extensionOf(location));
            log("formato " + (fmt_.empty() ? std::string("desconhecido") : fmt_) + ", " +
                std::to_string(reader_->size() / (1024 * 1024)) + " MB, abrindo no player do PS4");
        }
        h_ = sceAvPlayerInit(&init);
        if (!h_) { stopFetch(); reader_.reset(); log("sceAvPlayerInit falhou", true); if (error) *error = "sceAvPlayerInit falhou"; return false; }
        int r = sceAvPlayerAddSource(h_, source.c_str());
        if (r < 0) {
            log("AddSource " + hex(r) + (unsigned(r) == 0x806A0002u ? " (o player do PS4 recusou o arquivo" +
                                             (fmt_.empty() ? std::string(")") : ", formato " + fmt_ + ")") : ""));
            if (error)
                *error = "o player do PS4 recusou o arquivo (" + hex(r) + (fmt_.empty() ? std::string("") : ", formato " + fmt_) +
                         ")" + (fmt_ == "mp4" ? ". O MP4 pode usar um codec que o PS4 nao decodifica (ex. HEVC/x265)." : "");
            sceAvPlayerClose(h_);
            h_ = nullptr;
            stopFetch();
            reader_.reset();
            traceStep("video recusado", true);
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
        stopFetch();
        reader_.reset();
        traceStep("video fechado", true);
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

    std::string status() override
    {
        while (statusLock_.test_and_set(std::memory_order_acquire)) {}
        std::string s = status_;
        statusLock_.clear(std::memory_order_release);
        return s;
    }

private:
    Ps4Platform &plat_;
    MlAvPlayerHandle h_ = nullptr;
    std::atomic<bool> quit_{false}, audioRunning_{false};
    bool paused_ = false, gotFrame_ = false;
    int64_t duration_ = 0;
    std::string status_;
    std::unique_ptr<RangeReader> reader_;
    std::string fmt_;

    static std::string extensionOf(const std::string &url)
    {
        std::string path = url.substr(0, url.find_first_of("?#"));
        size_t dot = path.rfind('.'), slash = path.rfind('/');
        if (dot == std::string::npos || (slash != std::string::npos && dot < slash) || path.size() - dot > 5) return ".mp4";
        std::string ext = path.substr(dot);
        for (auto &ch : ext) ch = char(ch >= 'A' && ch <= 'Z' ? ch + 32 : ch);
        return ext;
    }

    // File-replacement callbacks (called from the player's own threads). The network work (and a TLS
    // handshake when the player jumps, e.g. to an index at the end of the file) runs on our own fetch
    // thread with a known 1 MiB stack: the player's threads may have small stacks (suspected cause of
    // the crash when starting a video in 01.14).
    static int32_t fOpen(void *, const char *) { return 0; }
    static int32_t fClose(void *) { return 0; }
    static int32_t fRead(void *obj, uint8_t *buf, uint64_t pos, uint32_t len)
    {
        auto *self = static_cast<AvEngine *>(obj);
        if (!self->reader_ || !self->fetchRunning_.load()) return -1;
        while (self->fetchLock_.test_and_set(std::memory_order_acquire)) sceKernelUsleep(200);   // one read at a time
        self->jobBuf_ = buf;
        self->jobPos_ = pos;
        self->jobLen_ = len;
        self->jobDone_.store(false);
        self->jobReady_.store(true, std::memory_order_release);
        while (!self->jobDone_.load(std::memory_order_acquire)) {
            if (!self->fetchRunning_.load()) { self->fetchLock_.clear(); return -1; }
            sceKernelUsleep(500);
        }
        long n = self->jobResult_;
        self->fetchLock_.clear(std::memory_order_release);
        if (n < 0) self->log("rede: " + self->reader_->lastError());
        return int32_t(n);
    }

    static void fetchMain(void *p) { static_cast<AvEngine *>(p)->fetchLoop(); }
    void fetchLoop()
    {
        while (!fetchQuit_.load()) {
            if (!jobReady_.load(std::memory_order_acquire)) { sceKernelUsleep(500); continue; }
            jobReady_.store(false);
            jobResult_ = reader_ ? reader_->readAt(jobPos_, jobBuf_, jobLen_) : -1;
            jobDone_.store(true, std::memory_order_release);
        }
        fetchRunning_ = false;
    }
    std::atomic_flag fetchLock_ = ATOMIC_FLAG_INIT;
    std::atomic<bool> fetchRunning_{false}, fetchQuit_{false}, jobReady_{false}, jobDone_{false};
    uint8_t *jobBuf_ = nullptr;
    uint64_t jobPos_ = 0;
    uint32_t jobLen_ = 0;
    long jobResult_ = 0;

    void stopFetch()
    {
        if (!fetchRunning_.load()) return;
        fetchQuit_ = true;
        for (int i = 0; i < 4000 && fetchRunning_.load(); i++) sceKernelUsleep(5000);   // a read can take its timeout
    }
    static uint64_t fSize(void *obj)
    {
        auto *self = static_cast<AvEngine *>(obj);
        return self->reader_ && self->reader_->size() > 0 ? uint64_t(self->reader_->size()) : 0;
    }

    std::atomic_flag statusLock_ = ATOMIC_FLAG_INIT;
    // Called from the UI thread and from the player's callback threads.
    void log(const std::string &s, bool settled = false)
    {
        while (statusLock_.test_and_set(std::memory_order_acquire)) {}
        bool changed = status_ != s;
        status_ = s;
        statusLock_.clear(std::memory_order_release);
        // Recorded as the "last step" so that, if the console closes the app (CE-34878-0), the next
        // start shows where the player was.
        if (changed) traceStep(("video: " + s).c_str(), settled);
    }

    static void onEvent(void *obj, int32_t ev, int32_t, void *)
    {
        auto *self = static_cast<AvEngine *>(obj);
        switch (ev) {
        case MlAvStateReady: self->log("pronto"); break;
        case MlAvStatePlay: self->log("tocando", true); break;
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
