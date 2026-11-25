// VideoEngine implementation for Desktop (Linux/Windows) using libmpv software renderer.
// This file is added to ml_win only when the cmake ML_WIN option is ON.
#pragma once

#include "platform/platform.hpp"

#include <atomic>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <thread>
#else
#include <pthread.h>
#endif

// Forward declarations (avoid including mpv headers in the header)
struct mpv_handle;
struct mpv_render_context;

namespace ml::win {

class WinVideoEngine : public VideoEngine {
public:
    WinVideoEngine();
    ~WinVideoEngine() override;

    // VideoEngine interface
    void setRequestHeaders(const std::vector<std::pair<std::string, std::string>> &hdrs) override;
    bool open(const std::string &location, std::string *error) override;
    void pause() override;
    void resume() override;
    void stop() override;
    bool seek(int64_t ms) override;
    int64_t positionMs() override;
    int64_t durationMs() override;
    bool paused() override;
    bool finished() override;
    bool updateFrame(Framebuffer fb) override;
    std::string status() override;
    std::string diagnostics() override;
    void setInternalLog(bool on) override { internalLog_ = on; }

private:
    static void onUpdate(void *self);
    bool startEventThread();
    void joinEventThread();
    void eventLoop();

    mpv_handle         *mpv_       = nullptr;
    mpv_render_context *renderCtx_ = nullptr;

    std::atomic<bool>    finished_{false};
    std::atomic<bool>    paused_{false};
    std::atomic<bool>    quit_{false};
    std::atomic<bool>    newFrame_{false};
    std::atomic<int64_t> posMs_{0};
    std::atomic<int64_t> durMs_{0};

    std::string status_;
    std::string diagLog_;
    bool        internalLog_ = false;

    std::vector<std::pair<std::string, std::string>> requestHeaders_;

#if defined(_WIN32)
    std::thread eventThread_;
#else
    pthread_t eventTid_ = 0;
#endif
};

}  // namespace ml::win
