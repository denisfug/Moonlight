#include "core/net.hpp"

#include <atomic>

#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace ml {

std::string Url::origin() const
{
    std::string o = scheme + "://" + host;
    bool def = (scheme == "http" && port == 80) || (scheme == "https" && port == 443);
    if (!def) o += ":" + std::to_string(port);
    return o;
}

std::string Url::str() const { return origin() + path + (query.empty() ? "" : "?" + query); }

static std::string lower(std::string s)
{
    for (auto &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

Url parseUrl(const std::string &text)
{
    Url u;
    for (unsigned char c : text)
        if (c <= 0x20 || c == 0x7F) return u;   // spaces/control chars are never valid here
    size_t se = text.find("://");
    if (se == std::string::npos) return u;
    u.scheme = lower(text.substr(0, se));
    if (u.scheme != "http" && u.scheme != "https") return u;
    size_t hs = se + 3;
    size_t pe = text.find_first_of("/?#", hs);
    std::string auth = text.substr(hs, pe == std::string::npos ? std::string::npos : pe - hs);
    if (auth.empty() || auth.find('@') != std::string::npos) return u;
    std::string host = auth;
    u.port = u.scheme == "https" ? 443 : 80;
    if (!auth.empty() && auth[0] == '[') {   // IPv6 literal
        size_t rb = auth.find(']');
        if (rb == std::string::npos) return u;
        host = auth.substr(0, rb + 1);
        if (rb + 1 < auth.size()) {
            if (auth[rb + 1] != ':') return u;
            std::string ps = auth.substr(rb + 2);
            if (ps.empty() || ps.size() > 5) return u;
            for (char c : ps) if (!std::isdigit(static_cast<unsigned char>(c))) return u;
            u.port = std::atoi(ps.c_str());
        }
    } else {
        size_t colon = auth.rfind(':');
        if (colon != std::string::npos) {
            std::string ps = auth.substr(colon + 1);
            host = auth.substr(0, colon);
            if (ps.empty() || ps.size() > 5) return u;
            for (char c : ps) if (!std::isdigit(static_cast<unsigned char>(c))) return u;
            u.port = std::atoi(ps.c_str());
        }
        for (char c : host)
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '.' || c == '_')) return u;
    }
    if (host.empty() || u.port <= 0 || u.port > 65535) return u;
    u.host = lower(host);
    std::string rest = pe == std::string::npos ? "" : text.substr(pe);
    size_t hash = rest.find('#');
    if (hash != std::string::npos) rest = rest.substr(0, hash);
    size_t q = rest.find('?');
    u.path = q == std::string::npos ? rest : rest.substr(0, q);
    u.query = q == std::string::npos ? "" : rest.substr(q + 1);
    if (u.path.empty()) u.path = "/";
    u.valid = true;
    return u;
}

bool isLocalHost(const std::string &h)
{
    if (h == "localhost" || h == "[::1]") return true;
    int a, b, c, d;
    char tail;
    if (std::sscanf(h.c_str(), "%d.%d.%d.%d%c", &a, &b, &c, &d, &tail) != 4) return false;
    if (a < 0 || a > 255 || b < 0 || b > 255 || c < 0 || c > 255 || d < 0 || d > 255) return false;
    return a == 127 || a == 10 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168);
}

std::string percentEncode(const std::string &s)
{
    static const char *hx = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += static_cast<char>(c);
        else { o += '%'; o += hx[c >> 4]; o += hx[c & 15]; }
    }
    return o;
}

std::string percentDecode(const std::string &s)
{
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            o += static_cast<char>(std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16));
            i += 2;
        } else {
            o += s[i];
        }
    }
    return o;
}

std::string redactUrl(const std::string &url)
{
    Url u = parseUrl(url);
    if (!u.valid) return "<invalid url>";
    // Addon URLs often embed configuration (API keys) as a path segment before /manifest.json.
    std::string p;
    size_t i = 0;
    while (i < u.path.size()) {
        size_t j = u.path.find('/', i + 1);
        std::string seg = u.path.substr(i, j == std::string::npos ? std::string::npos : j - i);
        p += seg.size() > 25 ? "/<redacted>" : seg;
        if (j == std::string::npos) break;
        i = j;
    }
    return u.origin() + p + (u.query.empty() ? "" : "?<redacted>");
}

std::string checkUrlPolicy(const Url &u, const UrlPolicy &p)
{
    if (!u.valid) return "URL invalida (use http:// ou https://, sem usuario/senha na URL)";
    if (u.scheme == "https") return "";
    if (isLocalHost(u.host)) return "";
    if (p.allowPlainHttpRemote) return "";
    return "HTTP sem criptografia so e permitido para servidores da rede local";
}

namespace {
std::atomic<NetTraceFn> gTrace{nullptr};
std::atomic<unsigned> gTraced{0};
}  // namespace

void setNetTrace(NetTraceFn fn) { gTrace.store(fn); }

void traceStep(const char *step, bool done)
{
    if (NetTraceFn f = gTrace.load()) f(step, done);
}

void netTraceOnce(int id, const char *step, bool done)
{
    unsigned bit = 1u << (id & 15);
    if (gTraced.fetch_or(bit) & bit) return;
    if (NetTraceFn f = gTrace.load()) f(step, done);
}

}  // namespace ml
