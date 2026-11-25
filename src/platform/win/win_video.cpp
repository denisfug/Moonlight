// VideoEngine implementation for Desktop (Linux/Windows) using libmpv software renderer.
// Uses MPV_RENDER_API_TYPE_SW to render directly into the Moonlight Framebuffer (no OpenGL).
// Moonlight pixel format: 0xAARRGGBB (little-endian bytes: B G R A) → mpv format "bgra".
// Audio is handled internally by mpv (default audio output device).
// This file is added to ml_win only when the cmake ML_WIN option is ON.
#include "platform/win/win_video.hpp"

#include <mpv/client.h>
#include <mpv/render.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>

namespace ml::win {

// ---------------------------------------------------------------- helpers
static std::string mpvErr(int e)
{
    const char *s = mpv_error_string(e);
    return s ? s : "unknown";
}

// ---------------------------------------------------------------- WinVideoEngine

WinVideoEngine::WinVideoEngine() = default;

WinVideoEngine::~WinVideoEngine()
{
    stop();
    if (renderCtx_) {
        mpv_render_context_free(renderCtx_);
        renderCtx_ = nullptr;
    }
    if (mpv_) {
        mpv_terminate_destroy(mpv_);
        mpv_ = nullptr;
    }
}

bool WinVideoEngine::open(const std::string &location, std::string *error)
{
    auto fail = [&](const std::string &m) {
        if (error) *error = m;
        status_ = "erro: " + m;
        return false;
    };

    // Reset any previous state
    stop();
    if (renderCtx_) {
        mpv_render_context_free(renderCtx_);
        renderCtx_ = nullptr;
    }
    if (mpv_) {
        mpv_terminate_destroy(mpv_);
        mpv_ = nullptr;
    }
    finished_ = false;
    paused_   = false;
    posMs_    = 0;
    durMs_    = 0;
    newFrame_ = false;

    // Create mpv handle
    mpv_ = mpv_create();
    if (!mpv_) return fail("mpv_create falhou");

    // Configure mpv options
    mpv_set_option_string(mpv_, "vo", "null");          // no window; we render via SW API
    mpv_set_option_string(mpv_, "ao", "auto");          // auto audio output
    mpv_set_option_string(mpv_, "hwdec", "no");         // software decode only (reliable)
    mpv_set_option_string(mpv_, "video-timing-offset", "0");
    mpv_set_option_string(mpv_, "cache", "yes");
    mpv_set_option_string(mpv_, "demuxer-max-bytes", "50MiB");

    // Apply extra request headers (e.g. from Stremio sources)
    if (!requestHeaders_.empty()) {
        std::string hdrStr;
        for (const auto &kv : requestHeaders_) {
            if (!hdrStr.empty()) hdrStr += ',';
            hdrStr += kv.first + ": " + kv.second;
        }
        mpv_set_option_string(mpv_, "http-header-fields", hdrStr.c_str());
    }

    int r = mpv_initialize(mpv_);
    if (r < 0) return fail("mpv_initialize: " + mpvErr(r));

    // Create software render context
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_SW)},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
    r = mpv_render_context_create(&renderCtx_, mpv_, params);
    if (r < 0) return fail("mpv_render_context_create: " + mpvErr(r));

    // Set update callback (called from mpv thread when a new frame is ready)
    mpv_render_context_set_update_callback(renderCtx_, onUpdate, this);

    // Start playback
    const char *cmd[] = {"loadfile", location.c_str(), nullptr};
    r = mpv_command(mpv_, cmd);
    if (r < 0) return fail("loadfile: " + mpvErr(r));

    status_ = "carregando";

    // Start event-polling thread
    quit_ = false;
    if (!startEventThread()) return fail("nao foi possivel iniciar thread de eventos");

    return true;
}

void WinVideoEngine::pause()
{
    if (!mpv_) return;
    int yes = 1;
    mpv_set_property(mpv_, "pause", MPV_FORMAT_FLAG, &yes);
    paused_ = true;
}

void WinVideoEngine::resume()
{
    if (!mpv_) return;
    int no = 0;
    mpv_set_property(mpv_, "pause", MPV_FORMAT_FLAG, &no);
    paused_ = false;
}

void WinVideoEngine::stop()
{
    if (!mpv_) return;
    quit_ = true;
    // Unblock the event thread's mpv_wait_event
    mpv_wakeup(mpv_);
    joinEventThread();

    const char *cmd[] = {"stop", nullptr};
    mpv_command(mpv_, cmd);
    finished_ = true;
    status_   = "parado";
}

bool WinVideoEngine::seek(int64_t ms)
{
    if (!mpv_) return false;
    double secs = ms / 1000.0;
    mpv_set_property(mpv_, "time-pos", MPV_FORMAT_DOUBLE, &secs);
    return true;
}

int64_t WinVideoEngine::positionMs()
{
    return posMs_.load();
}

int64_t WinVideoEngine::durationMs()
{
    return durMs_.load();
}

bool WinVideoEngine::paused()
{
    return paused_.load();
}

bool WinVideoEngine::finished()
{
    return finished_.load();
}

bool WinVideoEngine::updateFrame(Framebuffer fb)
{
    if (!renderCtx_ || !fb.pixels || fb.width <= 0 || fb.height <= 0) return false;

    // Only render when mpv signals a new frame is ready
    uint64_t flags = mpv_render_context_update(renderCtx_);
    if (!(flags & MPV_RENDER_UPDATE_FRAME) && !newFrame_.exchange(false)) return false;

    // "bgra" = bytes B G R A at increasing address → 0xAARRGGBB in little-endian uint32 ✓
    static const char fmt[] = "bgra";
    int   sz[2]    = {fb.width, fb.height};
    size_t stride  = static_cast<size_t>(fb.stride) * sizeof(uint32_t);

    mpv_render_param renderParams[] = {
        {MPV_RENDER_PARAM_SW_SIZE,    sz},
        {MPV_RENDER_PARAM_SW_FORMAT,  const_cast<char *>(fmt)},
        {MPV_RENDER_PARAM_SW_STRIDE,  &stride},
        {MPV_RENDER_PARAM_SW_POINTER, fb.pixels},
        {MPV_RENDER_PARAM_INVALID,    nullptr},
    };
    int r = mpv_render_context_render(renderCtx_, renderParams);
    return r >= 0;
}

std::string WinVideoEngine::status()
{
    return status_;
}

std::string WinVideoEngine::diagnostics()
{
    return diagLog_;
}

void WinVideoEngine::setRequestHeaders(
    const std::vector<std::pair<std::string, std::string>> &hdrs)
{
    requestHeaders_ = hdrs;
}

// ---------------------------------------------------------------- internal

/*static*/ void WinVideoEngine::onUpdate(void *self)
{
    // Called from mpv's render thread; just flag that a new frame is available.
    static_cast<WinVideoEngine *>(self)->newFrame_ = true;
}

bool WinVideoEngine::startEventThread()
{
    // Platform::startThread is not available here (we don't hold a Platform&).
    // Use pthreads directly — this file is POSIX-only (ML_WIN on Linux).
    // If a Windows port is needed, replace with CreateThread.
#if defined(_WIN32)
    // Windows: use std::thread or CreateThread
    eventThread_ = std::thread([this] { eventLoop(); });
    return true;
#else
    return pthread_create(&eventTid_, nullptr,
                          [](void *p) -> void * {
                              static_cast<WinVideoEngine *>(p)->eventLoop();
                              return nullptr;
                          },
                          this) == 0;
#endif
}

void WinVideoEngine::joinEventThread()
{
#if defined(_WIN32)
    if (eventThread_.joinable()) eventThread_.join();
#else
    if (eventTid_) {
        pthread_join(eventTid_, nullptr);
        eventTid_ = 0;
    }
#endif
}

void WinVideoEngine::eventLoop()
{
    while (!quit_.load()) {
        mpv_event *ev = mpv_wait_event(mpv_, 0.1);
        if (!ev) continue;

        switch (ev->event_id) {
        case MPV_EVENT_NONE:
            break;

        case MPV_EVENT_PLAYBACK_RESTART:
            status_ = "reproduzindo";
            break;

        case MPV_EVENT_END_FILE: {
            auto *data = static_cast<mpv_event_end_file *>(ev->data);
            if (data && data->reason == MPV_END_FILE_REASON_EOF) {
                finished_ = true;
                status_   = "concluido";
            } else if (data && data->reason == MPV_END_FILE_REASON_ERROR) {
                finished_ = true;
                const char *es = mpv_error_string(data->error);
                status_ = std::string("erro: ") + (es ? es : "desconhecido");
                diagLog_ += status_ + "\n";
            }
            break;
        }

        case MPV_EVENT_LOG_MESSAGE: {
            auto *msg = static_cast<mpv_event_log_message *>(ev->data);
            if (msg && msg->text && internalLog_) {
                diagLog_ += std::string("[mpv] ") + msg->text;
                // Keep diagLog_ bounded
                if (diagLog_.size() > 8192)
                    diagLog_ = diagLog_.substr(diagLog_.size() - 8192);
            }
            break;
        }

        default:
            break;
        }

        // Update position and duration
        if (mpv_) {
            double pos = 0.0, dur = 0.0;
            if (mpv_get_property(mpv_, "time-pos",  MPV_FORMAT_DOUBLE, &pos) >= 0)
                posMs_ = static_cast<int64_t>(pos * 1000.0);
            if (mpv_get_property(mpv_, "duration",  MPV_FORMAT_DOUBLE, &dur) >= 0)
                durMs_ = static_cast<int64_t>(dur * 1000.0);
        }
    }
}

}  // namespace ml::win
