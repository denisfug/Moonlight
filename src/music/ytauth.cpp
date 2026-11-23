// YouTube / Google OAuth2 Device Flow implementation for PS4.
// See ytauth.hpp for overview and references.
#include "music/ytauth.hpp"

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include "core/json.hpp"
#include "core/net.hpp"

namespace ml::ytauth {

namespace {

// Build a simple application/x-www-form-urlencoded body from key=value pairs.
// Keys and values must already be percent-encoded where needed.
std::string formBody(std::initializer_list<std::pair<const char *, std::string>> pairs)
{
    std::string s;
    for (const auto &p : pairs) {
        if (!s.empty()) s += '&';
        s += p.first;
        s += '=';
        s += p.second;
    }
    return s;
}

// Very small percent-encoder: encodes everything except unreserved chars.
// Used only for the fixed credential strings — no dynamic user data.
std::string encode(const std::string &s)
{
    static const char *hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out += char(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0xf];
        }
    }
    return out;
}

// POST helper: sends form body, returns parsed JSON.
Json postForm(HttpClient &http, const std::string &url, const std::string &body, std::string &err)
{
    HttpRequest req;
    req.url = url;
    req.timeoutMs = 15000;
    req.headers.push_back({"Content-Type", "application/x-www-form-urlencoded"});
    HttpResponse r = http.post(req, body);
    if (!r.error.empty()) { err = r.error; return {}; }
    if (r.status < 200 || r.status >= 300) {
        err = "HTTP " + std::to_string(r.status);
        // Try to include error field from JSON
        Json j = parseJson(r.body);
        std::string e = j["error"].asString();
        if (!e.empty()) err += " (" + e + ")";
        return j;  // caller may inspect
    }
    JsonError pe;
    Json j = parseJson(r.body, &pe);
    if (!pe.message.empty()) { err = "JSON invalido: " + pe.message; return {}; }
    return j;
}

}  // namespace

DeviceCode requestDeviceCode(HttpClient &http)
{
    DeviceCode dc;
    std::string err;
    std::string body = formBody({
        {"client_id",    encode(kClientId)},
        {"scope",        encode(kScope)},
    });
    Json j = postForm(http, "https://accounts.google.com/o/oauth2/device/code", body, err);
    if (!err.empty()) { dc.error = err; return dc; }

    dc.deviceCode      = j["device_code"].asString();
    dc.userCode        = j["user_code"].asString();
    dc.verificationUrl = j["verification_url"].asString();

    // interval and expires_in come as numbers
    std::string is = j["interval"].asString();
    std::string es = j["expires_in"].asString();
    if (!is.empty()) {
        char *ep = nullptr;
        long v = std::strtol(is.c_str(), &ep, 10);
        if (ep != is.c_str() && v > 0) dc.intervalSec = int(v);
    }
    if (!es.empty()) {
        char *ep = nullptr;
        long v = std::strtol(es.c_str(), &ep, 10);
        if (ep != es.c_str() && v > 0) dc.expiresIn = int(v);
    }

    if (dc.deviceCode.empty() || dc.userCode.empty()) {
        dc.error = "resposta incompleta do servidor";
    }
    return dc;
}

bool pollDeviceToken(HttpClient &http, const DeviceCode &dc,
                     Session &session, std::string &error)
{
    std::string body = formBody({
        {"client_id",     encode(kClientId)},
        {"client_secret", encode(kClientSecret)},
        {"device_code",   encode(dc.deviceCode)},
        {"grant_type",    "urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Adevice_code"},
    });
    Json j = postForm(http, "https://oauth2.googleapis.com/token", body, error);

    // "authorization_pending" or "slow_down" → not approved yet, not an error
    std::string e = j["error"].asString();
    if (e == "authorization_pending" || e == "slow_down") {
        error.clear();
        return false;
    }
    if (!e.empty()) {
        if (error.empty()) error = e;
        return false;
    }

    std::string at = j["access_token"].asString();
    std::string rt = j["refresh_token"].asString();
    if (at.empty()) {
        error = "token ausente na resposta";
        return false;
    }

    session.accessToken  = at;
    session.refreshToken = rt;

    std::string exs = j["expires_in"].asString();
    if (!exs.empty()) {
        char *ep = nullptr;
        long v = std::strtol(exs.c_str(), &ep, 10);
        if (ep != exs.c_str() && v > 0)
            session.expiresAt = int64_t(std::time(nullptr)) + v - 60;  // 60s safety margin
    }
    error.clear();
    return true;
}

bool refresh(HttpClient &http, Session &session, std::string &error)
{
    if (session.refreshToken.empty()) {
        error = "sem refresh token armazenado";
        return false;
    }
    std::string body = formBody({
        {"client_id",     encode(kClientId)},
        {"client_secret", encode(kClientSecret)},
        {"refresh_token", encode(session.refreshToken)},
        {"grant_type",    "refresh_token"},
    });
    Json j = postForm(http, "https://oauth2.googleapis.com/token", body, error);
    if (!error.empty()) return false;

    std::string at = j["access_token"].asString();
    if (at.empty()) { error = "token ausente na resposta de refresh"; return false; }

    session.accessToken = at;
    std::string exs = j["expires_in"].asString();
    if (!exs.empty()) {
        char *ep = nullptr;
        long v = std::strtol(exs.c_str(), &ep, 10);
        if (ep != exs.c_str() && v > 0)
            session.expiresAt = int64_t(std::time(nullptr)) + v - 60;
    }
    error.clear();
    return true;
}

// ---------------------------------------------------------------- store helpers

static const char *kStoreFile = "ytauth_session";

bool loadSession(JsonStore &store, Session &out)
{
    Json j = store.load(kStoreFile);
    std::string at = j["access_token"].asString();
    std::string rt = j["refresh_token"].asString();
    if (at.empty() && rt.empty()) return false;
    out.accessToken  = at;
    out.refreshToken = rt;
    // expires_at stored as string to avoid int overflow in JSON
    std::string es = j["expires_at"].asString();
    if (!es.empty()) {
        char *ep = nullptr;
        long long v = std::strtoll(es.c_str(), &ep, 10);
        if (ep != es.c_str()) out.expiresAt = int64_t(v);
    }
    out.accountName  = j["account_name"].asString();
    out.accountEmail = j["account_email"].asString();
    return true;
}

void saveSession(JsonStore &store, const Session &s)
{
    // NEVER log access_token or refresh_token values
    Json j = Json::object();
    j.set("access_token",  Json::string(s.accessToken));
    j.set("refresh_token", Json::string(s.refreshToken));
    j.set("expires_at",    Json::string(std::to_string(s.expiresAt)));
    j.set("account_name",  Json::string(s.accountName));
    j.set("account_email", Json::string(s.accountEmail));
    store.save(kStoreFile, j);
}

void clearSession(JsonStore &store)
{
    store.save(kStoreFile, Json::object());
}

// ---------------------------------------------------------------- convenience

AuthState currentState(const Session &s)
{
    if (s.accessToken.empty() && s.refreshToken.empty()) return AuthState::kNone;
    if (s.accessToken.empty()) return AuthState::kExpired;
    if (s.expiresAt > 0 && int64_t(std::time(nullptr)) >= s.expiresAt) return AuthState::kExpired;
    return AuthState::kActive;
}

bool isAuthenticated(const Session &s)
{
    return currentState(s) == AuthState::kActive;
}

std::string bearerHeader(const Session &s)
{
    if (!isAuthenticated(s)) return "";
    return "Bearer " + s.accessToken;
}

}  // namespace ml::ytauth
