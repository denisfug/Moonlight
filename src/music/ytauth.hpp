// YouTube / Google OAuth2 Device Flow — PS4-compatible authentication.
// PS4 has no browser, so we use the Device Authorization Grant (RFC 8628):
//   1. App POSTs to accounts.google.com/o/oauth2/device/code  → gets device_code + user_code
//   2. User visits accounts.google.com/device on PC/phone and enters user_code
//   3. App polls accounts.google.com/o/oauth2/token until approved
//   4. App stores access_token + refresh_token in JsonStore (never in logs)
//
// InnerTube auth: access_token is passed as "Authorization: Bearer <token>" in
// every InnerTube API call when the user is logged in.
// SAPISID-based auth (browser cookie) is NOT used — not applicable to PS4.
//
// Token storage: src/storage/store (JsonStore, atomic writes).
//   Keys: ytauth_access_token, ytauth_refresh_token, ytauth_expires_at
//   The access_token is NEVER written to logs (use redactUrl/redactToken).
//
// Scopes requested: https://www.googleapis.com/auth/youtube.readonly
//   (read library, playlists, history — no write access, no data modification)
//
// References:
//   https://developers.google.com/identity/protocols/oauth2/limited-input-device
//   Mochy-Music InnerTube.kt (GPL-3.0) — cookie auth studied for reference only;
//   device flow implemented independently for PS4 compatibility.
#pragma once
#include <cstdint>
#include <string>

#include "core/net.hpp"
#include "storage/store.hpp"

namespace ml::ytauth {

// OAuth2 client_id / client_secret for the TV/Limited-Input Device flow.
// These are the well-known public YouTube TV app credentials used by many
// open-source media players (Kodi, LibreELEC, etc.) under the same device-flow
// grant. They convey no special privilege and are not user credentials.
// See: https://github.com/nicko7i/tvh-iptv-proxy/issues/12 (public reference)
constexpr const char *kClientId     = "861556708454-d6dlm3lh05idd8npek18k6be8ba3oc68.apps.googleusercontent.com";
constexpr const char *kClientSecret = "SboVhoG9s0rNafixCSGGKXAT";
constexpr const char *kScope        = "https://www.googleapis.com/auth/youtube.readonly";

// ---------------------------------------------------------------- state
enum class AuthState {
    kNone,            // no session stored
    kPendingDevice,   // device code obtained, waiting for user to visit URL
    kActive,          // valid access_token available
    kExpired,         // refresh_token available; call refresh() first
};

// Persistent session stored via JsonStore.
// All token values must be treated as secrets — never log them.
struct Session {
    std::string accessToken;      // Bearer token for InnerTube (empty if none)
    std::string refreshToken;     // long-lived refresh token
    int64_t     expiresAt = 0;    // Unix timestamp (seconds); 0 = unknown
    std::string accountName;      // display name, may be empty
    std::string accountEmail;     // display email, may be empty
};

// ---------------------------------------------------------------- device-flow steps
struct DeviceCode {
    std::string deviceCode;       // opaque, sent to poll endpoint
    std::string userCode;         // shown to user (e.g. "XXXX-YYYY")
    std::string verificationUrl;  // e.g. "https://www.google.com/device"
    int         intervalSec = 5;  // minimum polling interval
    int         expiresIn   = 1800;
    std::string error;            // non-empty on failure
};

// Step 1: Request a device code.  Caller shows verificationUrl + userCode to the user.
DeviceCode requestDeviceCode(HttpClient &http);

// Step 2: Poll for the access token.  Returns true when approved (session filled).
// If denied/expired: session.accessToken is empty and error message is set.
// Caller must wait at least intervalSec between calls.
bool pollDeviceToken(HttpClient &http, const DeviceCode &dc,
                     Session &session, std::string &error);

// Refresh an expired access_token using the stored refresh_token.
// Returns true on success; updates session.accessToken and session.expiresAt.
bool refresh(HttpClient &http, Session &session, std::string &error);

// ---------------------------------------------------------------- store helpers
// Load/save session from/to JsonStore.  Returns false if no session stored.
bool loadSession(JsonStore &store, Session &out);
void saveSession(JsonStore &store, const Session &s);
void clearSession(JsonStore &store);

// ---------------------------------------------------------------- convenience
AuthState currentState(const Session &s);
// Returns true if access_token appears valid (not expired, not empty).
bool isAuthenticated(const Session &s);
// Returns the Authorization header value "Bearer <token>", or "" if not auth.
std::string bearerHeader(const Session &s);

}  // namespace ml::ytauth
