// YouTube Music InnerTube connector tests: parsing (fixtures).
#include "music/ytmusic.hpp"
#include "platform/host/host_platform.hpp"
#include "test.hpp"

using namespace ml;
using namespace ml::ytmusic;

TEST(ytmusic_parse_search)
{
    std::string body;
    host::HostFileSystem fs("");
    CHECK(fs.readFile(t::fixtures + "/ytmusic/search/believer", body, 1 << 20));
    auto v = parseSearch(parseJson(body));

    CHECK_EQ(v.size(), size_t(1));

    CHECK_EQ(v[0].title,  std::string("Believer"));
    CHECK_EQ(v[0].artist, std::string("Imagine Dragons"));
    CHECK_EQ(v[0].album,  std::string("Evolve"));
    CHECK_EQ(v[0].durationSec, 204);  // 3:24
    CHECK_EQ(v[0].source, std::string("YouTube Music"));
    CHECK(isYtMusicRef(v[0].url));
    CHECK_EQ(videoIdFrom(v[0].url), std::string("7wtfhZwyrcc"));
    // Best thumbnail: 226x226 (larger), query param stripped
    CHECK_EQ(v[0].art, std::string("https://lh3.googleusercontent.com/large.jpg"));
}

TEST(ytmusic_parse_search_empty)
{
    CHECK(parseSearch(parseJson("{}")).empty());
    CHECK(parseSearch(parseJson(R"({"contents":{}})")).empty());
    CHECK(parseSearch(parseJson("invalid {{")).empty());
}

TEST(ytmusic_ref_helpers)
{
    CHECK(isYtMusicRef("ytmusic:7wtfhZwyrcc"));
    CHECK(!isYtMusicRef("youtube:7wtfhZwyrcc"));
    CHECK(!isYtMusicRef("jiosaavn:abc"));
    CHECK(!isYtMusicRef(""));
    CHECK_EQ(videoIdFrom("ytmusic:7wtfhZwyrcc"), std::string("7wtfhZwyrcc"));
    CHECK_EQ(videoIdFrom("ytmusic:"), std::string(""));
    CHECK_EQ(videoIdFrom("other:id"), std::string(""));
}
