// Background catalog loader for the Movies/Series rows: one worker thread fetches catalog URLs
// (SourceManager::fetchCatalogAt touches no shared state) so the UI never freezes while a source
// answers. Spinlock + deques, same pattern as ImageLoader (no std::mutex in the PS4 binary).
#pragma once
#include <atomic>
#include <deque>
#include <string>
#include <vector>

#include "addons/source_manager.hpp"
#include "platform/platform.hpp"

namespace ml {

class CatalogLoader {
public:
    struct Result {
        int ticket = 0;
        std::string error;
        std::vector<addon::MetaPreview> metas;
    };

    CatalogLoader(Platform &p, HttpClient *http) : platform_(p), http_(http) {}
    ~CatalogLoader();

    // Queues a fetch and returns its ticket (>0), or 0 when there is no network client.
    int request(const std::string &url, const UrlPolicy &policy);
    void cancelAll();                  // drops queued (not yet started) jobs
    std::vector<Result> take();        // UI thread: finished jobs since the last call
    bool busy();

private:
    struct Job {
        int ticket;
        std::string url;
        UrlPolicy policy;
    };
    Platform &platform_;
    HttpClient *http_;
    std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
    std::atomic<bool> started_{false}, quit_{false}, running_{false};
    std::atomic<int> active_{0};
    int next_ = 0;
    std::deque<Job> queue_;
    std::vector<Result> done_;

    void lock() { while (lock_.test_and_set(std::memory_order_acquire)) {} }
    void unlock() { lock_.clear(std::memory_order_release); }
    static void threadMain(void *self);
    void run();
};

}  // namespace ml
