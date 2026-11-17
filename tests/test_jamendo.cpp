// Jamendo connector tests: URL building and JSON parsing (inline fixtures shaped like /v3.0/tracks/).
#include "music/jamendo.hpp"
#include "test.hpp"

using namespace ml;
using namespace ml::jamendo;

TEST(jamendo_search_url)
{
    CHECK_EQ(searchUrl("lofi beats", "abc123", 25, kHost),
             std::string("https://api.jamendo.com/v3.0/tracks/?client_id=abc123&search=lofi%20beats&limit=25"
                         "&format=json&audioformat=mp32&imagesize=500&include=musicinfo"));
    // reserved characters in the query are encoded, base is replaceable (tests / local server)
    CHECK(searchUrl("R&B/soul", "x", 5, "http://127.0.0.1:9").find("http://127.0.0.1:9/v3.0/tracks/?client_id=x&search=R%26B%2Fsoul&limit=5") == 0);
}

TEST(jamendo_featured_url)
{
    CHECK_EQ(featuredUrl("abc123", 20, kHost),
             std::string("https://api.jamendo.com/v3.0/tracks/?client_id=abc123&limit=20&format=json"
                         "&audioformat=mp32&imagesize=500&include=musicinfo&order=popularity_week"));
}

TEST(jamendo_parse_search)
{
    auto v = parseSearch(parseJson(R"({"headers":{"status":"success","results_count":4},"results":[
        {"id":"1","name":"Night Drive","duration":214,"artist_name":"Ana","album_name":"Roads",
         "audio":"https://prod-1.storage.jamendo.com/?trackid=1&format=mp32","image":"https://usercontent.jamendo.com/?type=album&id=9&width=500",
         "musicinfo":{"tags":{"genres":["electronic","ambient"]}}},
        {"id":"2","name":"Sem audio","duration":100,"artist_name":"B","audio":""},
        {"id":"3","name":"Audio estranho","duration":100,"artist_name":"C","audio":"ftp://x/y.mp3"},
        {"id":"4","name":"","duration":100,"artist_name":"D","audio":"https://x/4.mp3"},
        {"id":"5","name":"Longa","duration":99999,"artist_name":"E","audio":"https://x/5.mp3"}
    ]})"));
    CHECK_EQ(v.size(), size_t(2));   // no audio / non-http audio / no title are left out
    CHECK_EQ(v[0].title, std::string("Night Drive"));
    CHECK_EQ(v[0].artist, std::string("Ana"));
    CHECK_EQ(v[0].album, std::string("Roads"));
    CHECK_EQ(v[0].durationSec, 214);
    CHECK_EQ(v[0].genre, std::string("electronic"));
    CHECK_EQ(v[0].source, std::string("Jamendo"));
    CHECK(v[0].url.find("https://prod-1.storage.jamendo.com/") == 0);
    CHECK(v[0].art.find("width=500") != std::string::npos);
    CHECK_EQ(v[1].title, std::string("Longa"));
    CHECK_EQ(v[1].durationSec, 0);   // out of range -> unknown
    CHECK(v[1].genre.empty());       // no musicinfo
}

TEST(jamendo_parse_empty)
{
    CHECK(parseSearch(parseJson("{}")).empty());
    CHECK(parseSearch(parseJson(R"({"results":[]})")).empty());
    CHECK(parseSearch(parseJson("not json {{")).empty());
}
