// Random-access reads of a remote file over HTTP(S) with Range requests, for players that read a
// "file" by offset (the PS4 system player through its file-replacement callbacks). Sequential reads
// reuse the open response; a jump opens a new request at the new offset. Thread-safe (one lock).
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include "net/web_client.hpp"

namespace ml {

class RangeReader {
public:
    RangeReader(WebClient *client, std::string url) : client_(client), url_(std::move(url)) {}

    // First request: learns the size and whether the server accepts Range. "" or an error.
    std::string open();
    int64_t size() const { return size_; }
    bool seekable() const { return ranges_; }
    // Reads up to len bytes at pos (fewer only at the end of the file). <0 = error (see lastError()).
    long readAt(uint64_t pos, void *buf, size_t len);
    std::string lastError();
    uint64_t requests() const { return requests_; }   // for tests and diagnostics

private:
    std::string openAt(uint64_t pos);
    void lock() { while (lock_.test_and_set(std::memory_order_acquire)) {} }
    void unlock() { lock_.clear(std::memory_order_release); }

    WebClient *client_;
    std::string url_;
    std::unique_ptr<WebStream> stream_;
    uint64_t streamPos_ = 0;
    int64_t size_ = -1;
    bool ranges_ = false;
    uint64_t requests_ = 0;
    std::string err_;
    std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
};

}  // namespace ml
