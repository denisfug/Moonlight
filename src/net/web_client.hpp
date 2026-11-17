// The app's own HTTP/1.1 client: platform TCP sockets + Mbed TLS (tls.hpp) for https.
// Same code on PC and PS4, so the PC tests exercise exactly what runs on the console.
// - GET only; Connection: close (one request per connection).
// - Content-Length, chunked and close-delimited bodies; "ICY 200" status lines (SHOUTcast radio).
// - Follows up to 5 redirects; an https -> http downgrade is refused.
// - Errors never contain the full URL (only the host).
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "core/net.hpp"

namespace ml {

class WebStream : public HttpStream {
public:
    long read(void *buf, size_t len) override;
    int status() const override { return status_; }
    std::string contentType() const override { return header("content-type"); }
    int64_t contentLength() const { return length_; }   // -1 = unknown
    std::string header(const std::string &lowerName) const;
    const std::string &lastError() const { return err_; }

private:
    friend class WebClient;
    long rawRead(void *buf, size_t len);
    long readChunked(void *buf, size_t len);

    std::unique_ptr<TcpConn> conn_;
    std::map<std::string, std::string> headers_;
    std::string pending_;          // bytes received after the header, not yet handed out
    int status_ = 0;
    int64_t length_ = -1;          // Content-Length
    int64_t remaining_ = -1;       // body bytes left when length is known
    bool chunked_ = false;
    int64_t chunkLeft_ = 0;        // bytes left in the current chunk
    bool done_ = false;
    std::string err_;
};

class WebClient : public HttpClient {
public:
    WebClient(Sockets *sockets, std::string userAgent) : sockets_(sockets), ua_(std::move(userAgent)) {}

    HttpResponse get(const HttpRequest &req) override;
    std::unique_ptr<HttpStream> openStream(const HttpRequest &req, std::string *error) override;
    bool supportsHttps() const override { return true; }
    std::string name() const override { return "moonlight-web"; }

    // Sends the request (following redirects) and returns after the headers, whatever the final status.
    std::unique_ptr<WebStream> open(const HttpRequest &req, std::string *error);

private:
    std::unique_ptr<WebStream> openOnce(const Url &u, const HttpRequest &req, std::string *error);
    Sockets *sockets_;
    std::string ua_;
};

// Resolves a Location header against the URL that produced it ("" if it cannot be resolved).
std::string resolveLocation(const Url &base, const std::string &location);

}  // namespace ml
