// Video engine on libSceAvPlayer (system decoder: MP4 H.264/AAC, hardware accelerated).
// EXPERIMENTAL: written from the documented ABI (avplayer_api.h); every step reports its return code
// through status() so the first console tests tell us exactly where it stops.
#include <orbis/Sysmodule.h>
#include <orbis/libkernel.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cstdio>
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

// Decoder output buffers: NV12 frames that the system decoder fills and the CPU reads for conversion.
//
// Memory type history:
//   01.13-01.23: sceKernelAllocateDirectMemory (GPU, type=3, prot=0x33) — 0 frames produced.
//                At the time: basePriority=700 (low), numOutputVideoFramebuffers=2.
//   01.24-01.28: posix_memalign — also 0 frames. These versions never actually ran on the console
//                (closed on startup due to toolchain bugs before the video test ran).
//   01.29-01.30: Back to GPU-visible memory. Awaiting console test.
//   01.31+: posix_memalign by default (ML_AV_GPU_TEX=1 to test GPU path).
//           Rationale: PS4 IPTV Player (OpenOrbis, confirmed playing video on console) uses
//           posix_memalign for allocateTexture with basePriority=160, numOutputVideoFrameBuffers=4.
//           avplayer_api.h comment "must be GPU-visible memory" came from shadPS4 emulator docs
//           and may not reflect the real PS4 ABI requirement. The IPTV Player is empirical evidence.
//
// ML_AV_GPU_TEX (opt-in): use sceKernelAllocateDirectMemory instead of posix_memalign.
//   Build with -DML_AV_GPU_TEX=1 to test whether GPU memory makes a difference after posix_memalign
//   produces frames (or still 0 frames).
struct TexBlock {
    off_t  dmemOffset = 0;   // physical offset; 0 when using posix_memalign path
    void  *vaddr      = nullptr;
    size_t mappedSize = 0;
};
static constexpr size_t kTexAlign = 0x200000;   // 2 MiB, same as video_out.cpp

static TexBlock texAlloc(uint32_t align, uint32_t size)
{
    TexBlock b;
#ifdef ML_AV_GPU_TEX
    // GPU-visible path (opt-in via -DML_AV_GPU_TEX=1): type=3, prot=0x33, align≥2 MiB.
    // Identical to video_out.cpp parameters (confirmed working on console for display output).
    if (size_t(align) < kTexAlign) align = uint32_t(kTexAlign);
    b.mappedSize = (size_t(size) + size_t(align) - 1u) / size_t(align) * size_t(align);
    if (sceKernelAllocateDirectMemory(0, off_t(sceKernelGetDirectMemorySize()),
                                      b.mappedSize, size_t(align), 3, &b.dmemOffset) < 0) {
        b.dmemOffset = 0;
        return b;
    }
    if (sceKernelMapDirectMemory(&b.vaddr, b.mappedSize, 0x33, 0, b.dmemOffset, size_t(align)) < 0) {
        sceKernelReleaseDirectMemory(b.dmemOffset, b.mappedSize);
        b.vaddr = nullptr;
        b.dmemOffset = 0;
    }
    return b;
#else
    // posix_memalign path (default, 01.31+): same as PS4 IPTV Player (confirmed playing video on console).
    // dmemOffset stays 0 so texFreeBlock knows to call free(), not Munmap/ReleaseDirectMemory.
    if (align < 64) align = 64;
    b.mappedSize = size_t(size);
    if (posix_memalign(&b.vaddr, size_t(align), b.mappedSize) != 0) b.vaddr = nullptr;
    return b;
#endif
}

static void texFreeBlock(TexBlock &b)
{
    if (b.vaddr) {
#ifdef ML_AV_TEX_MALLOC
        std::free(b.vaddr);
#else
        // sceKernelMunmap(void* addr, size_t len) — exact match
        sceKernelMunmap(b.vaddr, b.mappedSize);
        if (b.dmemOffset) {
            // sceKernelReleaseDirectMemory(off_t start, size_t len) — exact match
            sceKernelReleaseDirectMemory(b.dmemOffset, b.mappedSize);
            b.dmemOffset = 0;
        }
#endif
        b.vaddr = nullptr;
    }
}

// Per-engine table of live texture allocations (small: sceAvPlayer asks for 4 buffers × ~8 MB each).
// NO destructor: the PS4 binary has no .init_array / __cxa_guard. Caller MUST call releaseAll()
// explicitly (AvEngine::stop() does this) before the AvEngine is destroyed.
struct TexTable {
    static constexpr int kMax = 16;
    TexBlock blocks[kMax];
    int      count = 0;

    void *alloc(uint32_t align, uint32_t size)
    {
        if (count >= kMax) return nullptr;
        TexBlock b = texAlloc(align, size);
        if (!b.vaddr) return nullptr;
        blocks[count++] = b;
        return b.vaddr;
    }
    void free(void *p)
    {
        for (int i = 0; i < count; i++) {
            if (blocks[i].vaddr == p) {
                texFreeBlock(blocks[i]);
                blocks[i] = blocks[--count];
                return;
            }
        }
    }
    void releaseAll()
    {
        for (int i = 0; i < count; i++) texFreeBlock(blocks[i]);
        count = 0;
    }
};

class AvEngine;
AvEngine *g_logEngine = nullptr;   // engine receiving the player's internal log (plain pointer)

std::string hex(int v)
{
    char b[16];
    std::snprintf(b, sizeof b, "0x%08X", unsigned(v));
    return b;
}

class AvEngine : public VideoEngine {
public:
    explicit AvEngine(Ps4Platform &p) : plat_(p)
    {
        for (auto &e : events_) e = 0;
        // TexTable has no constructor — zero-initialized by member init below.
    }
    ~AvEngine() override
    {
        stop();
        if (g_logEngine == this) g_logEngine = nullptr;
    }

    void setRequestHeaders(const std::vector<std::pair<std::string, std::string>> &h) override { headers_ = h; }

    bool open(const std::string &location, std::string *error) override
    {
        stop();
        MlAvInitData init;
        std::memset(&init, 0, sizeof init);
        init.memory = {this, &AvEngine::alloc, &AvEngine::free_, &AvEngine::allocTex, &AvEngine::freeTex};
        init.event = {this, &AvEngine::onEvent};
        init.debugLevel = 0;
        init.basePriority = 160;   // 01.24: was 700 (low). The working reference uses 160 (higher) so the
        init.numOutputVideoFramebuffers = 4;   // decoder threads are not starved; and 4 buffers, not 2.
        init.autoStart = true;
        init.defaultLanguage = "por";
        // http(s): the system player would use the system TLS, which refuses modern sites on older
        // firmware (0x8095F00C). It reads through our client instead (file-replacement callbacks + Range).
        std::string source = location;
        std::string lower = location.substr(0, 8);
        for (auto &ch : lower) ch = char(ch >= 'A' && ch <= 'Z' ? ch + 32 : ch);
        bool remote = lower.compare(0, 7, "http://") == 0 || lower.compare(0, 8, "https://") == 0;
        bool isHls  = location.find(".m3u8") != std::string::npos;
        if (remote && isHls) {
            // HLS: o player do PS4 resolve os segmentos diretamente pela rede do sistema.
            // Nao usamos RangeReader; a URL .m3u8 vai direto para sceAvPlayerAddSourceEx.
            log("stream HLS detectado, o player do PS4 vai buscar os segmentos diretamente");
        }
        if (remote && !isHls) {
            log("conectando ao servidor do video");
            reader_.reset(new RangeReader(&plat_.web(), location, headers_));
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
        if (internalLog_) {
            g_logEngine = this;
            int lr = sceAvPlayerSetLogCallback(&AvEngine::onSysLog, this);
            log("log interno do player: " + (lr < 0 ? "recusado " + hex(lr) : std::string("ligado")));
        }
        log("iniciando o player do sistema (sceAvPlayerInit)");
        h_ = sceAvPlayerInit(&init);
        if (!h_) { stopFetch(); reader_.reset(); log("sceAvPlayerInit falhou", true); if (error) *error = "sceAvPlayerInit falhou"; return false; }
        // HLS streams (.m3u8) require sceAvPlayerAddSourceEx with MlAvSourceHLS.
        // Using sceAvPlayerAddSource for HLS always returns an error on PS4.
        // Reference: PS4 IPTV Player player.cpp (confirmed working on console).
        int r;
        if (isHls) {
            log("entregando o stream HLS ao player (sceAvPlayerAddSourceEx)");
            r = sceAvPlayerAddSourceEx(h_, MlAvSourceHLS, source.c_str());
        } else {
            log("entregando o arquivo ao player (sceAvPlayerAddSource)");
            r = sceAvPlayerAddSource(h_, source.c_str());
        }
        if (r < 0) {
            log((isHls ? "AddSourceEx(HLS) " : "AddSource ") + hex(r) +
                (unsigned(r) == 0x806A0002u ? " (o player do PS4 recusou o arquivo" +
                                             (fmt_.empty() ? std::string(")") : ", formato " + fmt_ + ")") : ""));
            if (error) {
                if (isHls)
                    *error = "o player do PS4 recusou o stream HLS (" + hex(r) + "). Verifique se o URL .m3u8 e acessivel e usa H.264.";
                else
                    *error = "o player do PS4 recusou o arquivo (" + hex(r) + (fmt_.empty() ? std::string("") : ", formato " + fmt_) +
                             ")" + (fmt_ == "mp4" ? ". O MP4 pode usar um codec que o PS4 nao decodifica (ex. HEVC/x265)." : "");
            }
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
        // Release GPU-visible texture buffers after the player is closed (it must not hold references).
        texTable_.releaseAll();
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
        frames_++;
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

    void setInternalLog(bool on) override { internalLog_ = on; }

    std::string diagnostics() override
    {
        while (statusLock_.test_and_set(std::memory_order_acquire)) {}
        std::string d = "passos do player:";
        for (const auto &j : journal_) d += "\n  - " + j;
        std::vector<std::string> sys = sysLog_;
        statusLock_.clear(std::memory_order_release);
#ifdef ML_AV_GPU_TEX
        d += "\nmemoria de video (GPU direta sceKernelAllocateDirectMemory, ML_AV_GPU_TEX): ";
#else
        d += "\nmemoria de video (posix_memalign, padrao 01.31, como IPTV Player): ";
#endif
        d += std::to_string(texOk_.load()) + " blocos ok, " +
             std::to_string(texFail_.load()) + " recusados, " + std::to_string(texFallback_.load()) + " fallback-malloc (" +
             std::to_string(texBytes_.load() / (1024 * 1024)) + " MB)";
        d += "\nmemoria comum: " + std::to_string(memFail_.load()) + " pedidos recusados";
        d += "\nleituras pelo app: " + std::to_string(reads_.load()) + " (" + std::to_string(readBytes_.load() / 1024) + " KB), " +
             std::to_string(readFails_.load()) + " com erro";
        d += "\nquadros de video: " + std::to_string(frames_.load()) + ", blocos de audio: " + std::to_string(audioBlocks_.load());
        d += "\neventos do player:";
        for (int i = 0; i < 16; i++) if (events_[i].load()) d += " " + hex(eventIds_[i]) + "x" + std::to_string(events_[i].load());
        if (internalLog_) {
            d += "\nmensagens internas do player (" + std::to_string(sys.size()) + "):";
            for (const auto &l : sys) d += "\n  " + l;
        }
        return d;
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
    std::vector<std::pair<std::string, std::string>> headers_;
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
        self->reads_++;
        if (n < 0) self->readFails_++;
        else self->readBytes_ += n;
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

    // GPU-visible texture allocation table (NV12 decode buffers, up to 16 blocks × ~8 MB each).
    // Freed automatically by TexTable destructor when AvEngine is destroyed.
    TexTable texTable_;

    // ---- diagnostics counters (callbacks run on the player's threads)
    std::vector<std::string> journal_, sysLog_;   // guarded by statusLock_
    bool internalLog_ = false;
    std::atomic<int> texOk_{0}, texFail_{0}, texFallback_{0}, memFail_{0}, reads_{0}, readFails_{0}, frames_{0}, audioBlocks_{0};
    std::atomic<long long> texBytes_{0}, readBytes_{0};
    int32_t eventIds_[16] = {0};
    std::atomic<int> events_[16];
    std::atomic<int> eventSlots_{0};

    void countEvent(int32_t ev)
    {
        int n = eventSlots_.load();
        for (int i = 0; i < n && i < 16; i++)
            if (eventIds_[i] == ev) { events_[i]++; return; }
        int slot = eventSlots_.fetch_add(1);
        if (slot < 16) { eventIds_[slot] = ev; events_[slot] = 1; }
    }

    static void *alloc(void *obj, uint32_t align, uint32_t size)
    {
        void *p = avAlloc(obj, align, size);
        if (!p) static_cast<AvEngine *>(obj)->memFail_++;
        return p;
    }
    static void free_(void *obj, void *mem) { avFree(obj, mem); }
    static void *allocTex(void *obj, uint32_t align, uint32_t size)
    {
        auto *self = static_cast<AvEngine *>(obj);
        // GPU-visible direct memory (type=3, prot=0x33) — required by avplayer_api.h.
        // Uses same parameters as video_out.cpp (confirmed working on PS4).
        void *p = self->texTable_.alloc(align, size);
        if (p) { self->texOk_++; self->texBytes_ += size; }
        else {
            self->texFail_++;
            // Fallback: plain aligned memory — wrong type but avoids hard crash on alloc failure.
            if (align < 16) align = 16;
            if (posix_memalign(&p, align, size) != 0) p = nullptr;
            if (p) self->texFallback_++;
        }
        return p;
    }
    static void freeTex(void *obj, void *mem)
    {
        auto *self = static_cast<AvEngine *>(obj);
        // Try the GPU table first; if not found it was a fallback malloc.
        bool found = false;
        for (int i = 0; i < self->texTable_.count; i++) {
            if (self->texTable_.blocks[i].vaddr == mem) { found = true; break; }
        }
        if (found) self->texTable_.free(mem);
        else std::free(mem);
    }

    static int onSysLog(void *user, const char *format, va_list args)
    {
        auto *self = static_cast<AvEngine *>(user ? user : g_logEngine);
        if (!self || !format) return 0;
        char b[200];
        std::vsnprintf(b, sizeof b, format, args);
        while (self->statusLock_.test_and_set(std::memory_order_acquire)) {}
        if (self->sysLog_.size() < 30) self->sysLog_.push_back(b);
        self->statusLock_.clear(std::memory_order_release);
        return 0;
    }

    std::atomic_flag statusLock_ = ATOMIC_FLAG_INIT;
    // Called from the UI thread and from the player's callback threads.
    void log(const std::string &s, bool settled = false)
    {
        while (statusLock_.test_and_set(std::memory_order_acquire)) {}
        bool changed = status_ != s;
        status_ = s;
        if (changed && journal_.size() < 40) journal_.push_back(s);   // every step, for diagnostics()
        statusLock_.clear(std::memory_order_release);
        // Recorded as the "last step" so that, if the console closes the app (CE-34878-0), the next
        // start shows where the player was.
        if (changed) traceStep(("video: " + s).c_str(), settled);
    }

    static void onEvent(void *obj, int32_t ev, int32_t, void *)
    {
        auto *self = static_cast<AvEngine *>(obj);
        self->countEvent(ev);
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
                audioBlocks_++;
                pending.erase(pending.begin(), pending.begin() + long(block * 2));
            }
        }
        if (out) out->close();
        audioRunning_ = false;
    }
};

}  // namespace

// The module is loaded once per run. 01.17 on the console: the video test said the player engine was
// not working although 01.13 had reached sceAvPlayerAddSource; a second LoadModule call reporting an
// error for an already-loaded module is one possible cause, so it is asked only when not loaded yet.
std::unique_ptr<VideoEngine> Ps4Platform::makeVideoEngine()
{
    if (avLoaded_ == 0) {
        int loaded = sceSysmoduleIsLoaded(ORBIS_SYSMODULE_AV_PLAYER);
        int r = loaded == 0 ? 0 : sceSysmoduleLoadModule(ORBIS_SYSMODULE_AV_PLAYER);
        if (r < 0 && sceSysmoduleIsLoaded(ORBIS_SYSMODULE_AV_PLAYER) == 0) r = 0;   // reported an error but it is there
        if (r < 0) {
            avLoaded_ = -1;
            avErr_ = "modulo AvPlayer: carregar " + hex(r) + ", consulta " + hex(loaded);
        } else {
            avLoaded_ = 1;
        }
    }
    if (avLoaded_ < 0) return nullptr;
    return std::unique_ptr<VideoEngine>(new AvEngine(*this));
}

}  // namespace ml::ps4
