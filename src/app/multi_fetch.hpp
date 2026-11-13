// Fetches several JSON resources at the same time, one worker thread each, so a source that takes
// 40 s to answer does not hold back the others and never freezes the interface.
// (01.16 on the console: title details and stream links were fetched one source after the other on the
// UI thread with a 15 s limit; the screen froze and a slow source ended in "sem resposta 0x80410123".)
//
// Ownership: shared. The screen keeps one reference and each worker keeps one until it finishes, so a
// screen can be closed while requests are still in the air. The platform (and its HTTP client) must
// outlive the workers: App's destructor waits for MultiFetch::activeThreads() == 0.
#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/json.hpp"
#include "core/net.hpp"
#include "platform/platform.hpp"

namespace ml {

class MultiFetch {
public:
    struct Item {
        std::string label;   // who is being asked (source name)
        std::string url;
        UrlPolicy policy;
        int timeoutMs = 20000;
    };
    enum class State { Pending, Done, Failed };

    static std::shared_ptr<MultiFetch> start(Platform &p, std::vector<Item> items);

    size_t size() const { return slots_.size(); }
    const std::string &label(size_t i) const { return slots_[i].label; }
    int pending();   // requests still waiting for an answer
    // State of request i. The first time it is seen as Done its JSON is moved to *json (later calls
    // return Done with *json untouched). *error is set when Failed; *seconds is how long it took.
    State poll(size_t i, Json *json, std::string *error, int *seconds);

    static int activeThreads();   // workers still running, over all instances

    // Runs fn on its own thread, counted in activeThreads() (so App's destructor waits for it).
    // fn must only touch state it owns (shared_ptr) and the platform. false = thread not started.
    static bool runDetached(Platform &p, std::function<void()> fn);

private:
    struct Slot {
        std::string label, url, error;
        UrlPolicy policy;
        int timeoutMs = 20000;
        State state = State::Pending;
        Json json;
        bool taken = false;
        int seconds = 0;
    };
    struct Arg {
        std::shared_ptr<MultiFetch> self;
        size_t index;
    };
    explicit MultiFetch(Platform &p) : platform_(p) {}
    static void threadMain(void *arg);
    void run(size_t index);
    void lock() { while (lock_.test_and_set(std::memory_order_acquire)) {} }
    void unlock() { lock_.clear(std::memory_order_release); }

    Platform &platform_;
    std::vector<Slot> slots_;   // size fixed at start; label/url/policy never change afterwards
    std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
};

}  // namespace ml
