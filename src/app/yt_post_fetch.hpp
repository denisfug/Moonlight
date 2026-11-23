// YtPostFetch — single-request async POST fetcher for InnerTube (YouTube Music).
// MultiFetch supports only GET; InnerTube needs POST with a JSON body.
// This companion runs one HTTP POST on a background thread (via MultiFetch::runDetached)
// and stores the JSON result for the UI thread to pick up via poll().
//
// Usage (UI thread):
//   auto f = YtPostFetch::start(platform, url, body, headers);
//   // ... later in the update loop:
//   Json j; std::string err;
//   if (f && f->poll(j, err)) { /* j contains response or err is set */ f.reset(); }
#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/json.hpp"
#include "core/net.hpp"
#include "platform/platform.hpp"
#include "app/multi_fetch.hpp"

namespace ml {

class YtPostFetch {
public:
    using Headers = std::vector<std::pair<std::string, std::string>>;

    // Launches the POST on a background thread. Returns nullptr if the thread could not start.
    static std::shared_ptr<YtPostFetch> start(Platform &p,
                                              const std::string &url,
                                              const std::string &body,
                                              Headers headers = {},
                                              int timeoutMs = 20000);

    // Returns true when the request has finished (success or error).
    // On success: fills *json and leaves *error empty.
    // On error:   leaves *json null and fills *error.
    // May only be called once — after returning true the state is consumed.
    bool poll(Json &json, std::string &error);

    bool done() const { return done_.load(std::memory_order_acquire); }

private:
    YtPostFetch() = default;

    struct State {
        Json     json;
        std::string error;
        bool     ready = false;
    };

    // Shared between UI thread and worker; protected by done_ flag (worker writes before setting).
    std::shared_ptr<State> state_{new State};
    std::atomic<bool>      done_{false};
    bool                   taken_ = false;
};

}  // namespace ml
