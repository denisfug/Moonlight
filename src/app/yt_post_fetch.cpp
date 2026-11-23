#include "app/yt_post_fetch.hpp"

#include "net/web_client.hpp"

namespace ml {

std::shared_ptr<YtPostFetch> YtPostFetch::start(Platform &p,
                                                 const std::string &url,
                                                 const std::string &body,
                                                 Headers headers,
                                                 int timeoutMs)
{
    auto self = std::shared_ptr<YtPostFetch>(new YtPostFetch());

    // Capture by value so the worker outlives the caller.
    std::string captUrl  = url;
    std::string captBody = body;
    Headers     captHdr  = std::move(headers);
    int         captMs   = timeoutMs;
    auto        state    = self->state_;
    auto        done_ptr = std::shared_ptr<std::atomic<bool>>(self, &self->done_);

    bool ok = MultiFetch::runDetached(p, [captUrl, captBody, captHdr, captMs, state, done_ptr, &p]() {
        HttpRequest req;
        req.url       = captUrl;
        req.timeoutMs = captMs;
        req.maxBytes  = 4 * 1024 * 1024;
        req.headers   = captHdr;

        HttpClient *hc = p.http();
        HttpResponse r;
        if (hc) {
            r = hc->post(req, captBody);
        } else {
            r.error = "no network";
        }
        if (!r.error.empty()) {
            state->error = r.error;
        } else if (!r.ok()) {
            state->error = "HTTP " + std::to_string(r.status);
        } else {
            JsonError je;
            state->json = parseJson(r.body, &je);
            if (!je.ok()) state->error = "JSON: " + je.message;
        }
        state->ready = true;
        done_ptr->store(true, std::memory_order_release);
    });

    if (!ok) return nullptr;
    return self;
}

bool YtPostFetch::poll(Json &json, std::string &error)
{
    if (taken_) return true;  // already consumed
    if (!done_.load(std::memory_order_acquire)) return false;
    taken_ = true;
    json  = std::move(state_->json);
    error = std::move(state_->error);
    return true;
}

}  // namespace ml
