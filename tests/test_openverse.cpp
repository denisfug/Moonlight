// Openverse connector tests: URL building and JSON parsing (inline fixtures shaped like /v1/audio/).
#include "music/openverse.hpp"
#include "test.hpp"

using namespace ml;
using namespace ml::openverse;

TEST(openverse_search_url)
{
    std::string url = searchUrl("jazz piano", 25, kHost);
    CHECK(url.find("https://api.openverse.org/v1/audio/?q=jazz%20piano") == 0);
    CHECK(url.find("license_type=all") != std::string::npos);
    CHECK(url.find("extension=mp3") != std::string::npos);
    CHECK(url.find("page_size=25") != std::string::npos);
    // special characters encoded
    CHECK(searchUrl("R&B", 5, "http://127.0.0.1:9").find("q=R%26B") != std::string::npos);
}

TEST(openverse_featured_url)
{
    std::string url = featuredUrl(20, kHost);
    CHECK(url.find("https://api.openverse.org/v1/audio/") == 0);
    CHECK(url.find("extension=mp3") != std::string::npos);
    CHECK(url.find("page_size=20") != std::string::npos);
}

TEST(openverse_parse_search)
{
    auto v = parseSearch(parseJson(R"({
        "count": 5,
        "results": [
            {"id":"a1","title":"Moonlight Sonata","url":"https://files.openverse.org/a1.mp3",
             "duration":180,"creator":"Beethoven",
             "thumbnail":"https://files.openverse.org/a1.jpg","license":"cc0",
             "tags":[{"name":"classical"},{"name":"piano"}]},
            {"id":"a2","title":"Sem url","url":"","duration":100,"creator":"X","thumbnail":"","license":"by"},
            {"id":"a3","title":"URL invalida","url":"ftp://x/a3.mp3","duration":100,"creator":"Y","thumbnail":"","license":"by"},
            {"id":"a4","title":"","url":"https://x/a4.mp3","duration":100,"creator":"Z","thumbnail":"","license":"by"},
            {"id":"a5","title":"Duracao absurda","url":"https://x/a5.mp3","duration":99999,"creator":"W","thumbnail":"","license":"by"}
        ]
    })"));
    // a2 (no url), a3 (non-http url), a4 (no title) are rejected; a1 and a5 pass
    CHECK_EQ(v.size(), size_t(2));
    CHECK_EQ(v[0].title,       std::string("Moonlight Sonata"));
    CHECK_EQ(v[0].artist,      std::string("Beethoven"));
    CHECK_EQ(v[0].durationSec, 180);
    CHECK_EQ(v[0].genre,       std::string("classical"));   // first tag
    CHECK_EQ(v[0].source,      std::string("Openverse"));
    CHECK(v[0].url.find("https://files.openverse.org/a1.mp3") == 0);
    CHECK(v[0].art.find("https://") == 0);
    CHECK_EQ(v[1].title,       std::string("Duracao absurda"));
    CHECK_EQ(v[1].durationSec, 0);   // out of range → 0 (unknown)
}

TEST(openverse_parse_empty)
{
    CHECK(parseSearch(parseJson("{}")).empty());
    CHECK(parseSearch(parseJson(R"({"count":0,"results":[]})")).empty());
    CHECK(parseSearch(parseJson("not json {{")).empty());
}
