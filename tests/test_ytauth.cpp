// ytauth unit tests: OAuth2 device flow parsers and state helpers.
// Network calls are NOT tested here (require live Google endpoints).
// Tests cover: session store round-trip, state machine, bearer header.
#include "music/ytauth.hpp"
#include "storage/store.hpp"
#include "platform/host/host_platform.hpp"
#include "test.hpp"

using namespace ml;
using namespace ml::ytauth;

// ---------------------------------------------------------------- state machine

TEST(ytauth_state_none)
{
    Session s;
    CHECK_EQ(int(currentState(s)), int(AuthState::kNone));
    CHECK(!isAuthenticated(s));
    CHECK(bearerHeader(s).empty());
}

TEST(ytauth_state_active)
{
    Session s;
    s.accessToken = "ya29.abc";
    s.expiresAt   = int64_t(2e9);  // far future
    CHECK_EQ(int(currentState(s)), int(AuthState::kActive));
    CHECK(isAuthenticated(s));
    CHECK_EQ(bearerHeader(s), std::string("Bearer ya29.abc"));
}

TEST(ytauth_state_expired_by_time)
{
    Session s;
    s.accessToken = "ya29.abc";
    s.expiresAt   = 1000;  // Unix timestamp in the past
    CHECK_EQ(int(currentState(s)), int(AuthState::kExpired));
    CHECK(!isAuthenticated(s));
    CHECK(bearerHeader(s).empty());
}

TEST(ytauth_state_expired_no_access_but_has_refresh)
{
    Session s;
    s.refreshToken = "1//refresh_abc";
    CHECK_EQ(int(currentState(s)), int(AuthState::kExpired));
    CHECK(!isAuthenticated(s));
}

// ---------------------------------------------------------------- store round-trip

TEST(ytauth_store_roundtrip)
{
    // Write session to a temp dir and read it back.
    host::HostFileSystem fs(t::fixtures + "/tmp_ytauth");
    JsonStore store(fs, ".");

    Session in;
    in.accessToken  = "ya29.test_access";
    in.refreshToken = "1//test_refresh";
    in.expiresAt    = 1800000000LL;
    in.accountName  = "Test User";
    in.accountEmail = "test@example.com";
    saveSession(store, in);

    Session out;
    CHECK(loadSession(store, out));
    CHECK_EQ(out.accessToken,  std::string("ya29.test_access"));
    CHECK_EQ(out.refreshToken, std::string("1//test_refresh"));
    CHECK_EQ(out.expiresAt,    int64_t(1800000000LL));
    CHECK_EQ(out.accountName,  std::string("Test User"));
    CHECK_EQ(out.accountEmail, std::string("test@example.com"));

    // Clear and confirm gone
    clearSession(store);
    Session cleared;
    CHECK(!loadSession(store, cleared));
    CHECK(cleared.accessToken.empty());
    CHECK(cleared.refreshToken.empty());
}

TEST(ytauth_store_missing)
{
    host::HostFileSystem fs(t::fixtures + "/tmp_ytauth_missing_1234");
    JsonStore store(fs, ".");
    Session s;
    CHECK(!loadSession(store, s));
}
