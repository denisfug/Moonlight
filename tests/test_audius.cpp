// Audius connector: addresses and reading of the track list (fixtures). The whole path with a local
// server is in test_archive.cpp (archive_live_local_server). Not tested against api.audius.co itself.
#include "music/audius.hpp"
#include "platform/host/host_platform.hpp"
#include "test.hpp"

using namespace ml;
using namespace ml::audius;

TEST(audius_urls)
{
    CHECK_EQ(searchUrl("mc teste & cia", kHost),
             std::string("https://api.audius.co/v1/tracks/search?query=mc%20teste%20%26%20cia&limit=30&app_name=MoonlightHB"));
    CHECK_EQ(trendingUrl(kHost), std::string("https://api.audius.co/v1/tracks/trending?time=week&limit=40&app_name=MoonlightHB"));
    CHECK_EQ(streamUrl("D7KyD", kHost), std::string("https://api.audius.co/v1/tracks/D7KyD/stream?app_name=MoonlightHB"));
    CHECK(streamUrl("a/b", kHost).empty());
    CHECK(streamUrl("", kHost).empty());
    CHECK(validId("aB3x9") && !validId("a-b") && !validId("a b"));
}

TEST(audius_parse_tracks)
{
    std::string body;
    host::HostFileSystem fs("");
    CHECK(fs.readFile(t::fixtures + "/audius/v1/tracks/search", body, 1 << 20));
    auto v = parseTracks(parseJson(body), kHost);
    // gated, not streamable, no access, bad id and empty title are left out (the repeated one is the screen's job)
    CHECK_EQ(v.size(), size_t(3));
    CHECK_EQ(v[0].title, std::string("Paauer | Baauer B2B Party Favor"));
    CHECK_EQ(v[0].artist, std::string("Baauer"));
    CHECK_EQ(v[0].album, std::string("Electronic"));
    CHECK_EQ(v[0].durationSec, 5265);
    CHECK_EQ(v[0].art, std::string("https://example.invalid/480.jpg"));   // middle size preferred
    CHECK_EQ(v[0].source, std::string("Audius"));
    CHECK_EQ(v[0].url, std::string("https://api.audius.co/v1/tracks/D7KyD/stream?app_name=MoonlightHB"));
    CHECK_EQ(v[1].artist, std::string("mc_teste"));   // no name: handle
    CHECK(v[1].art.empty());                           // no cover stays without cover
    CHECK(parseTracks(parseJson("{}"), kHost).empty());
    CHECK(parseTracks(parseJson(R"({"data":{"id":"x"}})"), kHost).empty());
    auto one = parseTracks(parseJson(R"({"data":[{"id":"x1","title":"T","artwork":"https://a.invalid/c.jpg","duration":-5,"user":{"name":"U"}}]})"), kHost);
    CHECK_EQ(one.size(), size_t(1));
    if (!one.empty()) { CHECK_EQ(one[0].art, std::string("https://a.invalid/c.jpg")); CHECK_EQ(one[0].durationSec, 0); }
}
