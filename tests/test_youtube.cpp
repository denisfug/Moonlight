// Tests for src/music/youtube.{hpp,cpp}
#include "test.hpp"
#include "music/youtube.hpp"
#include "core/json.hpp"

#include <fstream>
#include <sstream>

using namespace ml;
using namespace ml::youtube;

static std::string readFixture(const char *path)
{
    std::ifstream f(path);
    if (!f) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// ---------------------------------------------------------------------------
// URL builders
// ---------------------------------------------------------------------------

TEST(youtube_searchUrl_empty_key) {
    CHECK(ml::youtube::searchUrl("rock", "").empty());
}

TEST(youtube_searchUrl_basic) {
    std::string url = ml::youtube::searchUrl("never gonna give you up", "APIKEY", 10, "https://host");
    CHECK(url.find("/youtube/v3/search") != std::string::npos);
    CHECK(url.find("type=video") != std::string::npos);
    CHECK(url.find("maxResults=10") != std::string::npos);
    CHECK(url.find("key=APIKEY") != std::string::npos);
    CHECK(url.find("videoCategoryId=10") != std::string::npos);
}

TEST(youtube_searchUrl_clamp_limit) {
    // limit > 50 should be clamped to 50
    std::string url = ml::youtube::searchUrl("q", "K", 999, "https://h");
    CHECK(url.find("maxResults=50") != std::string::npos);
    // limit < 1 should be clamped to 1
    std::string url2 = ml::youtube::searchUrl("q", "K", 0, "https://h");
    CHECK(url2.find("maxResults=1") != std::string::npos);
}

TEST(youtube_durationsUrl_empty) {
    CHECK(ml::youtube::durationsUrl("", "KEY", "https://h").empty());
    CHECK(ml::youtube::durationsUrl("id1", "", "https://h").empty());
}

TEST(youtube_durationsUrl_basic) {
    std::string url = ml::youtube::durationsUrl("id1,id2", "KEY", "https://h");
    CHECK(url.find("/youtube/v3/videos") != std::string::npos);
    CHECK(url.find("part=contentDetails") != std::string::npos);
    CHECK(url.find("key=KEY") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Ref helpers
// ---------------------------------------------------------------------------

TEST(youtube_isRef) {
    CHECK(ml::youtube::isYouTubeRef("youtube:dQw4w9WgXcQ"));
    CHECK(!ml::youtube::isYouTubeRef("https://example.com/song.mp3"));
    CHECK(!ml::youtube::isYouTubeRef("youtube:"));   // too short
    CHECK(!ml::youtube::isYouTubeRef(""));
}

TEST(youtube_videoIdFrom) {
    CHECK(ml::youtube::videoIdFrom("youtube:dQw4w9WgXcQ") == "dQw4w9WgXcQ");
    CHECK(ml::youtube::videoIdFrom("https://nope.com").empty());
}

// ---------------------------------------------------------------------------
// parseSearch — fixture
// ---------------------------------------------------------------------------

TEST(youtube_parseSearch_fixture) {
    std::string raw = readFixture("tests/fixtures/youtube/v3/search/music_query");
    CHECK(!raw.empty());
    Json root = parseJson(raw);

    auto songs = parseSearch(root);
    // 5 items in fixture: 1 channel, 1 empty videoId, 1 empty title → 2 valid
    CHECK(songs.size() == 2);

    CHECK(songs[0].title == "Rick Astley - Never Gonna Give You Up (Official Music Video)");
    CHECK(songs[0].artist == "Rick Astley");
    CHECK(ml::youtube::isYouTubeRef(songs[0].url));
    CHECK(ml::youtube::videoIdFrom(songs[0].url) == "dQw4w9WgXcQ");
    CHECK(!songs[0].art.empty());
    CHECK(songs[0].source == std::string(kName));
    CHECK(songs[0].durationSec == 0);  // not filled yet

    CHECK(songs[1].title == "Luis Fonsi - Despacito ft. Daddy Yankee");
    CHECK(ml::youtube::videoIdFrom(songs[1].url) == "kJQP7kiw5Fk");
}

// ---------------------------------------------------------------------------
// parseDurations — fixture
// ---------------------------------------------------------------------------

TEST(youtube_parseDurations_fixture) {
    // Build a small song list matching the durations fixture
    std::vector<catalog::Song> songs;
    {
        catalog::Song a;
        a.url = "youtube:dQw4w9WgXcQ";
        songs.push_back(a);
    }
    {
        catalog::Song b;
        b.url = "youtube:kJQP7kiw5Fk";
        songs.push_back(b);
    }
    {
        catalog::Song c;
        c.url = "youtube:UNKNOWN";
        songs.push_back(c);
    }

    std::string raw = readFixture("tests/fixtures/youtube/v3/videos/durations");
    CHECK(!raw.empty());
    Json root = parseJson(raw);

    int filled = parseDurations(root, songs);
    CHECK(filled == 2);
    CHECK(songs[0].durationSec == 213);  // PT3M33S
    CHECK(songs[1].durationSec == 282);  // PT4M42S
    CHECK(songs[2].durationSec == 0);    // not in fixture
}

TEST(youtube_parseDurations_no_double_fill) {
    // A song already filled should not be overwritten
    catalog::Song s;
    s.url = "youtube:dQw4w9WgXcQ";
    s.durationSec = 999;
    std::vector<catalog::Song> songs = {s};

    std::string raw = readFixture("tests/fixtures/youtube/v3/videos/durations");
    Json root = parseJson(raw);

    int filled = parseDurations(root, songs);
    CHECK(filled == 0);
    CHECK(songs[0].durationSec == 999);  // unchanged
}
