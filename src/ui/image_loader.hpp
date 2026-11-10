// Background poster loader: downloads JPEG/PNG images (stb_image), scales them to the card size
// (cover crop) and keeps a bounded cache. One worker thread; the UI only ever reads finished images.
// Synchronisation uses an atomic spinlock (no std::mutex: keeps the PS4 binary free of pthread mutexes).
#pragma once
#include <atomic>
#include <deque>
#include <string>
#include <unordered_map>

#include "core/net.hpp"
#include "platform/platform.hpp"
#include "ui/canvas.hpp"

namespace ml::ui {

// Decodes an image file in memory and scales it to w x h, cropping to keep the aspect ratio.
bool decodeImage(const std::string &bytes, int w, int h, Image &out);

class ImageLoader {
public:
    ImageLoader(Platform &p, HttpClient *http) : platform_(p), http_(http) {}
    void start();   // launches the worker thread (once)
    // Returns the image when ready; otherwise queues it (once) and returns nullptr.
    const Image *get(const std::string &url, int w, int h);
    // True once since the last call if new images finished (the UI then redraws).
    bool takeUpdated() { return updated_.exchange(false); }
    size_t cached() const { return done_.size(); }
    void trim();   // UI thread, between frames: bounds memory

    static constexpr size_t kMaxImages = 160;
    static constexpr size_t kMaxBytes = 3 * 1024 * 1024;

private:
    struct Job {
        std::string key, url;
        int w, h;
    };
    Platform &platform_;
    HttpClient *http_;
    std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
    std::atomic<bool> updated_{false}, started_{false};
    std::deque<Job> queue_;
    std::unordered_map<std::string, Image> done_;
    std::unordered_map<std::string, bool> failed_, queued_;

    void lock() { while (lock_.test_and_set(std::memory_order_acquire)) {} }
    void unlock() { lock_.clear(std::memory_order_release); }
    static void threadMain(void *self);
    void run();
};

}  // namespace ml::ui
