// Openverse music source: search + full-length MP3 stream (Creative Commons).
// API: https://docs.openverse.org/api/reference/
#include "music/openverse.hpp"

#include "core/net.hpp"

namespace ml::openverse {

std::string searchUrl(const std::string &query, int limit, const std::string &base)
{
    // license_type covers CC0 + CC BY + CC BY-SA + CC BY-NC (all allow free
    // personal streaming). "commercial" and "modification" permissions filter
    // only the most permissive subsets; we keep all CC types so the user sees
    // more results. The PS4 app is not a commercial venture.
    std::string url = base + "/v1/audio/?q=" + percentEncode(query);
    url += "&license_type=all";           // any CC license (all require attribution, none allow DRM)
    url += "&media_type=audio";
    url += "&extension=mp3";              // only ask for formats our decoder supports
    url += "&page_size=" + std::to_string(limit);
    return url;
}

std::string featuredUrl(int limit, const std::string &base)
{
    // /v1/audio/ with no query returns recent/trending items.
    std::string url = base + "/v1/audio/?media_type=audio";
    url += "&extension=mp3";
    url += "&page_size=" + std::to_string(limit);
    return url;
}

std::vector<Song> parseSearch(const Json &root)
{
    std::vector<Song> v;
    // Response: { "count": N, "results": [ { "id", "title", "url",
    //   "duration", "creator", "thumbnail", "license", "tags": [...] } ] }
    const Json &results = root["results"];
    for (size_t i = 0; i < results.size() && v.size() < 100; i++) {
        const Json &t = results[i];

        // "url" is the direct audio file address (not the landing page).
        std::string audio = t["url"].asString();
        if (audio.empty()) continue;
        if (audio.rfind("http", 0) != 0) continue;

        Song s;
        s.url = std::move(audio);
        s.title = t["title"].asString();
        if (s.title.empty()) continue;

        s.artist = t["creator"].asString();     // may be empty for some CC0 uploads
        // Openverse has no album field — use the first tag as genre hint if available.
        const Json &tags = t["tags"];
        for (size_t j = 0; j < tags.size() && s.genre.empty(); j++) {
            std::string tag = tags[j]["name"].asString();
            if (!tag.empty()) s.genre = tag;
        }

        s.art = t["thumbnail"].asString();      // may be empty; that's fine

        // Duration comes as an integer number of seconds (or null).
        int dur = t["duration"].asInt(0);
        s.durationSec = (dur > 0 && dur < 36000) ? dur : 0;

        s.source = kName;
        v.push_back(std::move(s));
    }
    return v;
}

}  // namespace ml::openverse
