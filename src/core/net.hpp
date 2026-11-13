// URL handling and the HTTP client contract used by the source manager.
// The platform supplies the HttpClient implementation (PS4: pending, host: plain HTTP for tests).
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ml {

struct Url {
    std::string scheme;   // "http" / "https" (lower case)
    std::string host;     // lower case
    int port = 0;         // explicit or default (80/443)
    std::string path;     // starts with '/', may contain %-escapes
    std::string query;    // without '?'
    bool valid = false;

    std::string origin() const;   // scheme://host[:port]
    std::string str() const;
};

// Parses absolute http/https URLs only. Rejects user:password@ (credentials must not be in URLs we log),
// control characters, spaces and empty hosts.
Url parseUrl(const std::string &text);

bool isLocalHost(const std::string &host);   // localhost, 127/8, 10/8, 172.16/12, 192.168/16, ::1

std::string percentEncode(const std::string &s);   // RFC 3986 unreserved kept, everything else %XX
std::string percentDecode(const std::string &s);

// Hides query strings and path segments that look like tokens/configuration before logging.
std::string redactUrl(const std::string &url);

struct HttpRequest {
    std::string url;
    int timeoutMs = 15000;
    size_t maxBytes = 4 * 1024 * 1024;
    std::vector<std::pair<std::string, std::string>> headers;   // a User-Agent/Accept here replaces the default one
    // Follow a redirect from https:// to http://. Off for everything that carries the user's data or
    // decides what the app runs (sources, music server, updates); on only for reading a video file.
    bool allowInsecureRedirect = false;
};

struct HttpResponse {
    int status = 0;            // 0 = transport error
    std::string body;
    std::string contentType;
    std::string error;         // transport error description (never contains the full URL)
    bool ok() const { return error.empty() && status >= 200 && status < 300; }
};

// Incremental response body (internet radio, large files). read(): >0 bytes, 0 = end, <0 = error.
class HttpStream {
public:
    virtual ~HttpStream() = default;
    virtual long read(void *buf, size_t len) = 0;
    virtual int status() const = 0;
    virtual std::string contentType() const = 0;
};

class HttpClient {
public:
    virtual ~HttpClient() = default;
    virtual HttpResponse get(const HttpRequest &req) = 0;
    // Opens a GET and returns after the headers (status 2xx), or nullptr with *error set.
    virtual std::unique_ptr<HttpStream> openStream(const HttpRequest &req, std::string *error)
    {
        if (error) *error = "streaming nao suportado por " + name();
        return nullptr;
    }
    virtual bool supportsHttps() const = 0;
    virtual std::string name() const = 0;
};

// ---------------------------------------------------------------- raw TCP, supplied by the platform
// Used by the app's own HTTP/TLS client (src/net) and by the phone remote server. Blocking I/O; the
// timeout given at connect/accept applies to every send/recv. Return values: >0 bytes, 0 = closed, <0 = error.
class TcpConn {
public:
    virtual ~TcpConn() = default;
    virtual long send(const void *buf, size_t len) = 0;
    virtual long recv(void *buf, size_t len) = 0;
    virtual std::string lastError() const = 0;
};

class TcpListener {
public:
    virtual ~TcpListener() = default;
    // Waits up to timeoutMs; nullptr on timeout or error. *peer = "a.b.c.d" of the client.
    virtual std::unique_ptr<TcpConn> accept(int timeoutMs, std::string *peer) = 0;
};

class Sockets {
public:
    virtual ~Sockets() = default;
    virtual std::unique_ptr<TcpConn> connect(const std::string &host, int port, int timeoutMs, std::string *err) = 0;
    virtual std::unique_ptr<TcpListener> listen(int port, std::string *err) = 0;   // all interfaces
    virtual std::string localAddress() = 0;   // LAN IPv4 of this device, "" if unknown
};

// Policy applied before any request: http(s) only, HTTPS required unless the host is local or the
// user explicitly allowed plain HTTP for that source.
struct UrlPolicy {
    bool allowPlainHttpRemote = false;
};
std::string checkUrlPolicy(const Url &u, const UrlPolicy &p);   // "" = allowed, else reason

// Start-up diagnostics: the network layers report the first time they reach each risky step
// (first TCP connection, Mbed TLS start, first handshake). The app records it so that, if the console
// closes the app (CE-34878-0), the next start can say where it stopped. done = step finished.
using NetTraceFn = void (*)(const char *step, bool done);
void setNetTrace(NetTraceFn fn);
void netTraceOnce(int id, const char *step, bool done);   // id 0..15, each reported once per run
void traceStep(const char *step, bool done);   // every call is recorded (use for rare events: player steps)

}  // namespace ml
