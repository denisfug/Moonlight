// Deezer music source: search + 30-second preview MP3.
// API: https://www.deezer.com/us/legal/api (free, non-commercial).
#include "music/deezer.hpp"

#include "core/net.hpp"

namespace ml::deezer {

std::string searchUrl(const std::string &query, int limit, const std::string &base)
{
    std::string url = base + "/search?q=" + percentEncode(query);
    url += "&limit=" + std::to_string(limit);
    // Request only tracks (not albums/artists/playlists) for a focused result.
    // Deezer default returns tracks when no type is specified — explicit is safer.
    url += "&order=RANKING";
    return url;
}

std::vector<Song> parseSearch(const Json &root)
{
    std::vector<Song> v;
    // Response shape: { "data": [ { "id", "title", "duration", "preview",
    //   "artist": { "name" }, "album": { "title", "cover_xl", "cover_medium" } } ] }
    const Json &data = root["data"];
    for (size_t i = 0; i < data.size() && v.size() < 50; i++) {
        const Json &t = data[i];

        // preview is mandatory: without it we have nothing to play on PS4
        std::string preview = t["preview"].asString();
        if (preview.empty()) continue;
        // Deezer preview URLs use https:// — verify they're not being changed
        if (preview.rfind("http", 0) != 0) continue;

        Song s;
        s.url = std::move(preview);
        s.title = t["title"].asString();
        if (s.title.empty()) continue;

        s.artist = t["artist"]["name"].asString();
        s.album  = t["album"]["title"].asString();

        // Prefer high-res cover; fall back to medium
        std::string artXl = t["album"]["cover_xl"].asString();
        std::string artMd = t["album"]["cover_medium"].asString();
        s.art = artXl.empty() ? artMd : artXl;

        s.durationSec = t["duration"].asInt(0);
        if (s.durationSec < 0 || s.durationSec > 36000) s.durationSec = 0;

        // Note: Deezer previews are exactly 30 seconds for most tracks.
        // The durationSec field still reflects the full track length which
        // is useful metadata even though only 30s plays.

        s.source = kName;
        v.push_back(std::move(s));
    }
    return v;
}

}  // namespace ml::deezer
