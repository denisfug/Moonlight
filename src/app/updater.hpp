// App updates from the project's public GitHub releases (denisfug/Moonlight, no credentials):
//   1. check: GET api.github.com/repos/<repo>/releases/latest -> tag "vNN.NN" + the .pkg asset;
//   2. download: the .pkg to /data/pkg/ (where GoldHEN's Package Installer also finds it);
//   3. install: Platform::installPackage (PS4: system background-install service), experimental.
// Network work runs on a worker thread; the UI polls state().
#pragma once
#include <atomic>
#include <string>

#include "platform/platform.hpp"

namespace ml {

// "v01.10", "01.10", "Moonlight 1.10" -> comparable number (major*1000 + minor); -1 if none.
int parseVersion(const std::string &s);

class Updater {
public:
    enum class Phase { Idle, Checking, UpToDate, Available, Downloading, Downloaded, Failed };
    struct State {
        Phase phase = Phase::Idle;
        std::string latest;      // e.g. "01.10"
        std::string assetUrl;    // .pkg download URL
        std::string notes;       // release title/body, first line
        std::string path;        // downloaded file
        std::string error;
        int64_t size = 0, done = 0;
    };

    Updater(Platform &p, std::string repo, std::string current) : platform_(p), repo_(std::move(repo)), current_(std::move(current)) {}
    ~Updater();

    void check();       // starts a background check (ignored while busy)
    void download();    // starts downloading the available .pkg (ignored unless Available/Failed after one)
    State state();
    const std::string &current() const { return current_; }
    std::string apiBase = "https://api.github.com";   // tests point this at a local server

    // Parses a GitHub "latest release" JSON; public for tests. "" or an error.
    static std::string parseRelease(const std::string &json, std::string *tag, std::string *assetUrl, int64_t *size,
                                    std::string *title);

private:
    static void checkMain(void *self);
    static void downloadMain(void *self);
    void runCheck();
    void runDownload();
    void set(const State &s);
    void lock() { while (lock_.test_and_set(std::memory_order_acquire)) {} }
    void unlock() { lock_.clear(std::memory_order_release); }

    Platform &platform_;
    std::string repo_, current_;
    State state_;
    std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
    std::atomic<bool> busy_{false}, quit_{false};
};

}  // namespace ml
