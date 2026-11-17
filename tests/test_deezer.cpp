// Deezer connector tests: URL building and JSON parsing (fixtures).
#include "music/deezer.hpp"
#include "platform/host/host_platform.hpp"
#include "test.hpp"

using namespace ml;
using namespace ml::deezer;

TEST(deezer_search_url)
{
    CHECK_EQ(searchUrl("bohemian rhapsody", 25, kHost),
             std::string("https://api.deezer.com/search?q=bohemian%20rhapsody&limit=25&order=RANKING"));
    CHECK_EQ(searchUrl("AC/DC", 10, kHost),
             std::string("https://api.deezer.com/search?q=AC%2FDC&limit=10&order=RANKING"));
    // Special characters encoded
    CHECK_EQ(searchUrl("R&B hits", 5, kHost),
             std::string("https://api.deezer.com/search?q=R%26B%20hits&limit=5&order=RANKING"));
}

TEST(deezer_parse_search)
{
    std::string body;
    host::HostFileSystem fs("");
    CHECK(fs.readFile(t::fixtures + "/deezer/search/bohemian", body, 1 << 20));
    auto v = parseSearch(parseJson(body));

    // 4 tracks in fixture but:
    // - "No Preview Track" has empty preview → excluded
    // - "Bad Preview URL" has non-http preview → excluded
    CHECK_EQ(v.size(), size_t(2));

    // First track: full data
    CHECK_EQ(v[0].title, std::string("Bohemian Rhapsody"));
    CHECK_EQ(v[0].artist, std::string("Queen"));
    CHECK_EQ(v[0].album, std::string("A Night at the Opera"));
    CHECK_EQ(v[0].durationSec, 354);
    CHECK_EQ(v[0].source, std::string("Deezer"));
    // cover_xl preferred over cover_medium
    CHECK_EQ(v[0].art, std::string("https://e-cdns-images.dzcdn.net/images/cover/abc/1000x1000.jpg"));
    // preview URL is the playable URL
    CHECK_EQ(v[0].url, std::string("https://cdns-preview-d.dzcdn.net/stream/c-dca0d99f47f4c8b5fc1413e4d7e18ed5-5.mp3"));

    // Second track: cover_xl empty → falls back to cover_medium
    CHECK_EQ(v[1].title, std::string("Bohemian Rhapsody (Instrumental)"));
    CHECK_EQ(v[1].art, std::string("https://e-cdns-images.dzcdn.net/images/cover/kk/250x250.jpg"));
}

TEST(deezer_parse_search_empty)
{
    CHECK(parseSearch(parseJson("{}")).empty());
    CHECK(parseSearch(parseJson(R"({"data":[]})")).empty());
    CHECK(parseSearch(parseJson("invalid json {{")).empty());
}

TEST(deezer_parse_duration_bounds)
{
    // Duration out of range → 0
    auto v = parseSearch(parseJson(
        R"({"data":[{"id":1,"title":"T","duration":-5,)"
        R"("preview":"https://cdn.example.com/p.mp3",)"
        R"("artist":{"name":"A"},"album":{"title":"B","cover_xl":"","cover_medium":""}}]})"));
    CHECK_EQ(v.size(), size_t(1));
    CHECK_EQ(v[0].durationSec, 0);
}

TEST(deezer_parse_no_title)
{
    // Empty title → excluded
    auto v = parseSearch(parseJson(
        R"({"data":[{"id":1,"title":"",)"
        R"("preview":"https://cdn.example.com/p.mp3",)"
        R"("artist":{"name":"A"},"album":{"title":"B","cover_xl":"","cover_medium":""}}]})"));
    CHECK(v.empty());
}

TEST(deezer_parse_preview_sec)
{
    // Every Deezer track must have previewSec == 30 so that callers cap
    // durationMs to the actual clip length, preventing premature auto-advance
    // when the 30-second stream ends before the full-track duration elapses.
    auto v = parseSearch(parseJson(
        R"({"data":[{"id":1,"title":"Song","duration":240,)"
        R"("preview":"https://cdns-preview-d.dzcdn.net/p.mp3",)"
        R"("artist":{"name":"A"},"album":{"title":"B","cover_xl":"","cover_medium":""}}]})"));
    CHECK_EQ(v.size(), size_t(1));
    CHECK_EQ(v[0].durationSec, 240);   // full-track metadata preserved
    CHECK_EQ(v[0].previewSec, 30);     // signals that only 30 s actually plays
}
