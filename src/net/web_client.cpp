#include "net/web_client.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

#include "net/tls.hpp"

namespace ml {

namespace {
std::string lower(std::string s)
{
    for (auto &c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string &s)
{
    size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

bool sendAll(TcpConn &c, const std::string &s)
{
    size_t done = 0;
    while (done < s.size()) {
        long n = c.send(s.data() + done, s.size() - done);
        if (n <= 0) return false;
        done += size_t(n);
    }
    return true;
}
}  // namespace

std::string resolveLocation(const Url &base, const std::string &loc)
{
    std::string l = trim(loc);
    if (l.empty()) return "";
    std::string ll = lower(l.substr(0, 8));
    if (ll.compare(0, 7, "http://") == 0 || ll.compare(0, 8, "https://") == 0) return l;
    if (l.compare(0, 2, "//") == 0) return base.scheme + ":" + l;
    if (l[0] == '/') return base.origin() + l;
    std::string dir = base.path.substr(0, base.path.rfind('/') + 1);
    if (dir.empty()) dir = "/";
    return base.origin() + dir + l;
}

// ---------------------------------------------------------------- stream
std::string WebStream::header(const std::string &lowerName) const
{
    auto it = headers_.find(lowerName);
    return it == headers_.end() ? std::string() : it->second;
}

long WebStream::rawRead(void *buf, size_t len)
{
    if (!pending_.empty()) {
        size_t n = std::min(len, pending_.size());
        std::memcpy(buf, pending_.data(), n);
        pending_.erase(0, n);
        return long(n);
    }
    long n = conn_->recv(buf, len);
    if (n < 0) err_ = conn_->lastError();
    return n;
}

long WebStream::readChunked(void *buf, size_t len)
{
    while (chunkLeft_ == 0) {
        // Chunk size line (the CRLF after the previous chunk's data was consumed already).
        std::string line;
        char c;
        for (;;) {
            long n = rawRead(&c, 1);
            if (n <= 0) { if (err_.empty()) err_ = "resposta cortada (chunked)"; return -1; }
            if (c == '\n') break;
            if (line.size() > 256) { err_ = "chunked invalido"; return -1; }
            line += c;
        }
        line = trim(line);
        if (line.empty()) continue;   // the CRLF that ends a chunk
        char *end = nullptr;
        long long sz = std::strtoll(line.c_str(), &end, 16);
        if (end == line.c_str() || sz < 0) { err_ = "chunked invalido"; return -1; }
        if (sz == 0) { done_ = true; return 0; }   // trailers are ignored (Connection: close)
        chunkLeft_ = sz;
    }
    size_t want = size_t(std::min<int64_t>(int64_t(len), chunkLeft_));
    long n = rawRead(buf, want);
    if (n <= 0) { if (err_.empty()) err_ = "resposta cortada (chunked)"; return -1; }
    chunkLeft_ -= n;
    return n;
}

long WebStream::read(void *buf, size_t len)
{
    if (done_ || len == 0) return 0;
    if (chunked_) return readChunked(buf, len);
    if (remaining_ >= 0) {
        if (remaining_ == 0) { done_ = true; return 0; }
        len = size_t(std::min<int64_t>(int64_t(len), remaining_));
    }
    long n = rawRead(buf, len);
    if (n == 0) {
        done_ = true;
        if (remaining_ > 0) { err_ = "resposta cortada"; return -1; }
        return 0;
    }
    if (n > 0 && remaining_ >= 0) remaining_ -= n;
    return n;
}

// ---------------------------------------------------------------- client
std::unique_ptr<WebStream> WebClient::openOnce(const Url &u, const HttpRequest &req, std::string *error)
{
    auto fail = [&](const std::string &m) { if (error) *error = m; return std::unique_ptr<WebStream>(); };
    if (!sockets_) return fail("sem rede neste aparelho");
    std::string host = u.host;
    if (!host.empty() && host[0] == '[') host = host.substr(1, host.size() - 2);
    std::string e;
    netTraceOnce(0, "primeira conexao de rede (DNS + TCP)", false);
    std::unique_ptr<TcpConn> c = sockets_->connect(host, u.port, req.timeoutMs, &e);
    netTraceOnce(1, "primeira conexao de rede concluida", true);
    if (!c) return fail(host + ": " + e);
    if (u.scheme == "https") {
        c = tls::connect(std::move(c), host, &e);
        if (!c) return fail(host + ": " + e);
    }
    bool defPort = (u.scheme == "https" && u.port == 443) || (u.scheme == "http" && u.port == 80);
    bool ownUa = false, ownAccept = false;
    for (const auto &h : req.headers) {
        std::string k = lower(h.first);
        ownUa = ownUa || k == "user-agent";
        ownAccept = ownAccept || k == "accept";
    }
    std::string q = "GET " + (u.path.empty() ? std::string("/") : u.path) + (u.query.empty() ? "" : "?" + u.query) +
                    " HTTP/1.1\r\nHost: " + u.host + (defPort ? "" : ":" + std::to_string(u.port)) + "\r\n" +
                    (ownUa ? "" : "User-Agent: " + ua_ + "\r\n") + (ownAccept ? "" : "Accept: */*\r\n") +
                    "Accept-Encoding: identity\r\nConnection: close\r\n";
    for (const auto &h : req.headers) {
        // one header per line: a value with CR/LF (from an addon's JSON) must not inject extra lines
        if (h.first.find_first_of("\r\n:") != std::string::npos || h.second.find_first_of("\r\n") != std::string::npos) continue;
        q += h.first + ": " + h.second + "\r\n";
    }
    q += "\r\n";
    if (!sendAll(*c, q)) return fail(host + ": envio falhou (" + c->lastError() + ")");

    std::unique_ptr<WebStream> s(new WebStream);
    std::string head;
    char buf[4096];
    size_t he;
    while ((he = head.find("\r\n\r\n")) == std::string::npos) {
        long n = c->recv(buf, sizeof buf);
        if (n < 0) {
            // TcpConn reports a receive timeout as "tempo esgotado" (PS4: 0x80410123, seen on the console in 01.16)
            if (c->lastError().find("tempo esgotado") != std::string::npos)
                return fail(host + ": conectou, mas o servidor nao respondeu em " + std::to_string(req.timeoutMs / 1000) + " s");
            return fail(host + ": sem resposta (" + c->lastError() + ")");
        }
        if (n == 0) return fail(host + ": conexao fechada sem resposta");
        head.append(buf, size_t(n));
        if (head.size() > 64 * 1024) return fail(host + ": cabecalho grande demais");
    }
    s->pending_ = head.substr(he + 4);
    head.resize(he);
    if (head.compare(0, 5, "HTTP/") != 0 && head.compare(0, 4, "ICY ") != 0) return fail(host + ": resposta nao e HTTP");
    size_t sp = head.find(' ');
    s->status_ = std::atoi(head.c_str() + sp + 1);
    if (s->status_ < 100 || s->status_ > 999) return fail(host + ": status invalido");
    for (size_t p = head.find("\r\n"); p != std::string::npos;) {
        size_t e2 = head.find("\r\n", p + 2);
        std::string line = head.substr(p + 2, e2 == std::string::npos ? std::string::npos : e2 - p - 2);
        p = e2;
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string k = lower(trim(line.substr(0, colon)));
        std::string v = trim(line.substr(colon + 1));
        auto it = s->headers_.find(k);
        if (it == s->headers_.end()) s->headers_[k] = v;
        else it->second += ", " + v;
    }
    std::string te = lower(s->header("transfer-encoding"));
    std::string cl = s->header("content-length");
    if (te.find("chunked") != std::string::npos) {
        s->chunked_ = true;
    } else if (!cl.empty()) {
        char *end = nullptr;
        long long v = std::strtoll(cl.c_str(), &end, 10);
        if (end != cl.c_str() && v >= 0) s->length_ = s->remaining_ = v;
    }
    if (s->status_ == 204 || s->status_ == 304 || (s->status_ >= 100 && s->status_ < 200)) s->done_ = true;
    s->conn_ = std::move(c);
    return s;
}

std::unique_ptr<WebStream> WebClient::open(const HttpRequest &req, std::string *error)
{
    Url u = parseUrl(req.url);
    if (!u.valid) { if (error) *error = "URL invalida"; return nullptr; }
    for (int hop = 0; hop <= 5; ++hop) {
        std::unique_ptr<WebStream> s = openOnce(u, req, error);
        if (!s) return nullptr;
        int st = s->status();
        bool redirect = st == 301 || st == 302 || st == 303 || st == 307 || st == 308;
        if (!redirect) return s;
        std::string next = resolveLocation(u, s->header("location"));
        Url nu = parseUrl(next);
        if (!nu.valid) { if (error) *error = u.host + ": redirecionamento invalido"; return nullptr; }
        if (u.scheme == "https" && nu.scheme == "http" && !req.allowInsecureRedirect) {
            if (error) *error = u.host + ": recusado redirecionamento de HTTPS para HTTP inseguro (" + nu.host + ")";
            return nullptr;
        }
        u = nu;
    }
    if (error) *error = "redirecionamentos demais";
    return nullptr;
}

HttpResponse WebClient::get(const HttpRequest &req)
{
    HttpResponse r;
    std::unique_ptr<WebStream> s = open(req, &r.error);
    if (!s) return r;
    r.status = s->status();
    r.contentType = s->contentType();
    if (s->contentLength() > int64_t(req.maxBytes)) { r.error = "resposta maior que o limite"; return r; }
    char buf[16384];
    for (;;) {
        long n = s->read(buf, sizeof buf);
        if (n < 0) { r.error = s->lastError().empty() ? "leitura falhou" : s->lastError(); r.body.clear(); return r; }
        if (n == 0) break;
        if (r.body.size() + size_t(n) > req.maxBytes) { r.error = "resposta maior que o limite"; r.body.clear(); return r; }
        r.body.append(buf, size_t(n));
    }
    return r;
}

std::unique_ptr<HttpStream> WebClient::openStream(const HttpRequest &req, std::string *error)
{
    std::unique_ptr<WebStream> s = open(req, error);
    if (!s) return nullptr;
    if (s->status() < 200 || s->status() >= 300) {
        if (error) *error = "servidor respondeu HTTP " + std::to_string(s->status());
        return nullptr;
    }
    return std::unique_ptr<HttpStream>(s.release());
}

}  // namespace ml
