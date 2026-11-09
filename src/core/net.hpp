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
    std::vector<std::pair<std::string, std::string>> headers;
};

struct HttpResponse {
    int status = 0;            // 0 = transport error
    std::string body;
    std::string contentType;
    std::string error;         // transport error description (never contains the full URL)
    bool ok() const { return error.empty() && status >= 200 && status < 300; }
};

class HttpClient {
public:
    virtual ~HttpClient() = default;
    virtual HttpResponse get(const HttpRequest &req) = 0;
    virtual bool supportsHttps() const = 0;
    virtual std::string name() const = 0;
};

// Policy applied before any request: http(s) only, HTTPS required unless the host is local or the
// user explicitly allowed plain HTTP for that source.
struct UrlPolicy {
    bool allowPlainHttpRemote = false;
};
std::string checkUrlPolicy(const Url &u, const UrlPolicy &p);   // "" = allowed, else reason

}  // namespace ml
