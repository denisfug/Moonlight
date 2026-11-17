// Jamendo music source: search + full-length MP3 stream (Creative Commons).
// API: https://developer.jamendo.com/v3.0/tracks
#include "music/jamendo.hpp"

#include "core/net.hpp"

namespace ml::jamendo {

std::string searchUrl(const std::string &query, const std::string &clientId,
                      int limit, const std::string &base)
{
    std::string url = base + "/v3.0/tracks/?client_id=" + percentEncode(clientId);
    url += "&search=" + percentEncode(query);
    url += "&limit=" + std::to_string(limit);
    url += "&format=json";
    url += "&audioformat=mp32";   // VBR MP3 — best quality, still MP3 (minimp3 compatible)
    url += "&imagesize=500";      // square cover, good for the shelf view
    url += "&include=musicinfo";  // adds tags/genres (unused now but useful later)
    return url;
}

std::string featuredUrl(const std::string &clientId, int limit, const std::string &base)
{
    std::string url = base + "/v3.0/tracks/?client_id=" + percentEncode(clientId);
    url += "&limit=" + std::to_string(limit);
    url += "&format=json";
    url += "&audioformat=mp32";
    url += "&imagesize=500";
    url += "&include=musicinfo";
    url += "&order=popularity_week";
    return url;
}

std::vector<Song> parseSearch(const Json &root)
{
    std::vector<Song> v;
    // Response shape: { "headers": { ... }, "results": [ { "id", "name",
    //   "duration", "artist_name", "album_name", "audio", "image" } ] }
    const Json &results = root["results"];
    for (size_t i = 0; i < results.size() && v.size() < 50; i++) {
        const Json &t = results[i];

        // audio is mandatory: without it we have nothing to play
        std::string audio = t["audio"].asString();
        if (audio.empty()) continue;
        if (audio.rfind("http", 0) != 0) continue;

        Song s;
        s.url = std::move(audio);
        s.title = t["name"].asString();
        if (s.title.empty()) continue;

        s.artist = t["artist_name"].asString();
        s.album  = t["album_name"].asString();
        s.art    = t["image"].asString();

        int dur = t["duration"].asInt(0);
        s.durationSec = (dur > 0 && dur < 36000) ? dur : 0;

        // first genre tag, when musicinfo was included
        const Json &genres = t["musicinfo"]["tags"]["genres"];
        if (genres.size() > 0) s.genre = genres[0].asString();

        s.source = kName;
        v.push_back(std::move(s));
    }
    return v;
}

}  // namespace ml::jamendo
