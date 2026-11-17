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
    CHECK_EQ(trendingUrl(kHost, "Hip-Hop/Rap"), std::string("https://api.audius.co/v1/tracks/trending?genre=Hip-Hop%2FRap&time=month&limit=40&app_name=MoonlightHB"));
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
    CHECK_EQ(v[0].genre, std::string("Electronic"));
    CHECK(v[0].album.empty());
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

#include "music/taste.hpp"

TEST(taste_recent_and_counts)
{
    music::Taste t;
    CHECK(t.topGenre().empty() && t.topArtist().empty() && t.recent().empty());
    auto song = [](const char *title, const char *artist, const char *genre, const char *url) {
        music::Track tr;
        tr.title = title; tr.artist = artist; tr.genre = genre; tr.url = url; tr.source = "Audius";
        return tr;
    };
    t.notePlay(song("A", "X", "Funk", "u1"));
    t.notePlay(song("B", "Y", "Rock", "u2"));
    t.notePlay(song("A", "X", "Funk", "u1"));      // again: moves to the front, not duplicated
    CHECK_EQ(t.recent().size(), size_t(2));
    CHECK_EQ(t.recent()[0].title, std::string("A"));
    CHECK_EQ(t.topGenre(), std::string("Funk"));
    CHECK_EQ(t.topArtist(), std::string("X"));
    t.notePlay(song("C", "Y", "Rock", "u3"));      // tie 2 x 2: the one played last wins
    CHECK_EQ(t.topGenre(), std::string("Rock"));
    music::Track demo; demo.title = "D"; demo.demo = true; demo.path = "p";
    t.notePlay(demo);
    CHECK_EQ(t.recent().size(), size_t(3));        // demo tracks are not remembered
    music::Track local; local.title = "L"; local.artist = "Z"; local.path = "/data/music/l.mp3";
    t.notePlay(local);
    CHECK_EQ(t.recent()[0].path, std::string("/data/music/l.mp3"));
    CHECK_EQ(t.topGenre(), std::string("Rock"));   // no genre: does not count as one

    music::Taste u;
    u.load(t.save());                               // what is written is what comes back
    CHECK_EQ(u.recent().size(), size_t(4));
    CHECK_EQ(u.recent()[1].url, std::string("u3"));
    CHECK_EQ(u.topGenre(), std::string("Rock"));
    CHECK_EQ(u.topArtist(), t.topArtist());
    for (int i = 0; i < 50; i++) t.notePlay(song("S", "W", "Pop", ("x" + std::to_string(i)).c_str()));
    CHECK_EQ(t.recent().size(), music::Taste::kMaxRecent);
    u.load(parseJson("{\"recent\":[{\"title\":\"sem endereco\"},{\"url\":\"u\"}],\"genres\":[{\"name\":\"\",\"n\":3},{\"name\":\"G\",\"n\":-1}]}"));
    CHECK(u.recent().empty() && u.topGenre().empty());   // broken file: ignored, no crash
}
