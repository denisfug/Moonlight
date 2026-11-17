#include "music/audius.hpp"

#include "core/net.hpp"

namespace ml::audius {

namespace {
constexpr const char *kApp = "app_name=MoonlightHB";

std::string artOf(const Json &art)
{
    if (art.isString()) return art.asString();
    for (const char *k : {"480x480", "150x150", "1000x1000"})
        if (art[k].isString() && !art[k].asString().empty()) return art[k].asString();
    return "";
}
}  // namespace

bool validId(const std::string &id)
{
    if (id.empty() || id.size() > 40) return false;
    for (char ch : id)
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9'))) return false;
    return true;
}

std::string searchUrl(const std::string &query, const std::string &base)
{
    return base + "/v1/tracks/search?query=" + percentEncode(query) + "&limit=30&" + kApp;
}

std::string trendingUrl(const std::string &base, const std::string &genre)
{
    if (genre.empty()) return base + "/v1/tracks/trending?time=week&limit=40&" + kApp;
    return base + "/v1/tracks/trending?genre=" + percentEncode(genre) + "&time=month&limit=40&" + kApp;
}

std::string streamUrl(const std::string &id, const std::string &base)
{
    return validId(id) ? base + "/v1/tracks/" + id + "/stream?" + kApp : std::string();
}

std::vector<catalog::Song> parseTracks(const Json &root, const std::string &base)
{
    std::vector<catalog::Song> v;
    const Json &data = root["data"];
    for (size_t i = 0; i < data.size() && v.size() < 100; i++) {
        const Json &t = data[i];
        std::string id = t["id"].asString();
        if (!validId(id)) continue;
        if (!t["is_streamable"].asBool(true)) continue;
        if (t["is_stream_gated"].asBool(false) || t["is_delete"].asBool(false)) continue;
        if (t["access"].isObject() && !t["access"]["stream"].asBool(true)) continue;
        catalog::Song s;
        s.title = t["title"].asString();
        if (s.title.empty()) continue;
        s.artist = t["user"]["name"].asString();
        if (s.artist.empty()) s.artist = t["user"]["handle"].asString();
        s.genre = t["genre"].asString();   // Audius tracks are singles: no album, the genre is shown instead
        s.art = artOf(t["artwork"]);
        s.durationSec = int(t["duration"].asInt(0));
        if (s.durationSec < 0 || s.durationSec > 360000) s.durationSec = 0;
        s.url = streamUrl(id, base);
        s.source = kName;
        v.push_back(std::move(s));
    }
    return v;
}

}  // namespace ml::audius
