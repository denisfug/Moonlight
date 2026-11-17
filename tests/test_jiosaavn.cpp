// JioSaavn connector tests: URL building and JSON parsing (fixtures).
#include "music/jiosaavn.hpp"
#include "platform/host/host_platform.hpp"
#include "test.hpp"

using namespace ml;
using namespace ml::jiosaavn;

TEST(jiosaavn_search_url)
{
    CHECK_EQ(searchUrl("believer", 20, kHost),
             std::string("https://www.jiosaavn.com/api.php?__call=search.getResults"
                         "&_format=json&_marker=0&api_version=4&ctx=android&p=1&n=20&q=believer"));
    // spaces encoded
    CHECK_EQ(searchUrl("imagine dragons", 10, kHost),
             std::string("https://www.jiosaavn.com/api.php?__call=search.getResults"
                         "&_format=json&_marker=0&api_version=4&ctx=android&p=1&n=10&q=imagine%20dragons"));
    // special chars
    CHECK_EQ(searchUrl("R&B", 5, kHost),
             std::string("https://www.jiosaavn.com/api.php?__call=search.getResults"
                         "&_format=json&_marker=0&api_version=4&ctx=android&p=1&n=5&q=R%26B"));
}

TEST(jiosaavn_parse_search)
{
    std::string body;
    host::HostFileSystem fs("");
    CHECK(fs.readFile(t::fixtures + "/jiosaavn/search/believer", body, 1 << 20));
    auto v = parseSearch(parseJson(body));

    // 3 results in fixture but "No Encrypted URL" has no encrypted_media_url → excluded
    CHECK_EQ(v.size(), size_t(2));

    // First song: basic fields
    CHECK_EQ(v[0].title,  std::string("Believer"));
    CHECK_EQ(v[0].artist, std::string("Imagine Dragons"));
    CHECK_EQ(v[0].album,  std::string("Evolve"));
    CHECK_EQ(v[0].durationSec, 204);
    CHECK_EQ(v[0].source, std::string("JioSaavn"));
    // url is a metadata ref (no stream)
    CHECK(isJioSaavnRef(v[0].url));
    CHECK_EQ(v[0].url, std::string("jiosaavn:abc123"));
    // cover upgraded from 150x150 to 500x500
    CHECK_EQ(v[0].art, std::string("https://c.saavncdn.com/believer/abc_500x500.jpg"));

    // Second song: HTML entities unescaped, two artists joined
    CHECK_EQ(v[1].title,  std::string("Believer & More (From \"Evolve\")"));
    CHECK_EQ(v[1].artist, std::string("Imagine Dragons, Feat. Artist"));
    CHECK_EQ(v[1].url,    std::string("jiosaavn:def456"));
}

TEST(jiosaavn_parse_search_empty)
{
    CHECK(parseSearch(parseJson("{}")).empty());
    CHECK(parseSearch(parseJson(R"({"results":[]})")).empty());
    CHECK(parseSearch(parseJson("invalid {{")).empty());
}

TEST(jiosaavn_ref_helpers)
{
    CHECK(isJioSaavnRef("jiosaavn:abc123"));
    CHECK(!isJioSaavnRef("youtube:abc123"));
    CHECK(!isJioSaavnRef("ytmusic:abc"));
    CHECK(!isJioSaavnRef(""));
}
