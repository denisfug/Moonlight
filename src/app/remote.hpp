// Phone remote: a small web page served by the app on the local network (port 8080).
// The phone's browser sends D-pad/buttons, typed text and source URLs. Every API call needs the 4-digit
// PIN shown on the TV; 5 wrong PINs lock the API for 60 s. Off by default (Configuracoes).
// The server runs on its own thread and never touches App: commands go through a small locked queue
// that App::frame() drains on the UI thread.
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "platform/platform.hpp"

namespace ml {

class RemoteControl {
public:
    struct Command {
        enum Kind { Buttons, Text, Source } kind;
        uint32_t buttons;
        std::string text;
    };

    explicit RemoteControl(Platform &p) : platform_(p) {}
    ~RemoteControl();

    bool start(std::string *err);   // opens the port and starts the thread (no-op when running)
    void stop();
    bool running() const { return running_.load(); }
    std::string address() const { return address_; }   // "http://192.168.0.10:8080"
    std::string pin() const { return pin_; }
    uint64_t lastSeenMs() const { return lastSeen_.load(); }   // last authorised request (platform clock)

    std::vector<Command> take();   // UI thread

    // Diagnostics for the QR screen: connections accepted so far, and a request the console makes to
    // its own server through its LAN address (if that fails, the problem is on the console side).
    int accepted() const { return accepted_.load(); }
    void selfTest();
    std::string selfTestResult();   // "" while running
    // UI thread: what the phone page shows (screen name, and the text field the TV is waiting for).
    void setState(const std::string &screen, bool prompt, const std::string &title, const std::string &initial, bool url);

    // Request handling, public for tests: raw HTTP request in, raw HTTP response out.
    std::string handle(const std::string &request, uint64_t nowMs);

private:
    static void threadMain(void *self);
    static void selfTestMain(void *self);
    void loop();
    void lock() { while (lock_.test_and_set(std::memory_order_acquire)) {} }
    void unlock() { lock_.clear(std::memory_order_release); }

    Platform &platform_;
    std::unique_ptr<TcpListener> listener_;
    std::atomic<bool> running_{false}, quit_{false};
    std::atomic<uint64_t> lastSeen_{0};
    std::atomic<int> accepted_{0};
    std::atomic<bool> testing_{false};
    std::string testResult_;
    std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
    std::vector<Command> queue_;
    std::string state_ = "{}";
    std::string address_, pin_;
    int failures_ = 0;
    uint64_t lockedUntil_ = 0;
};

}  // namespace ml
