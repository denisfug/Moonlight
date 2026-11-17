// JioSaavn catalog — metadata source (search, covers, track info only).
// No stream: encrypted URLs require DES decryption with a service-side key.
// Moonlight does not implement unauthorized stream extraction.
// Based on the public API observed in Mochy-Music (GPL-3.0, structure reference only).
#include "music/jiosaavn.hpp"

#include "core/net.hpp"
#include <cstring>

namespace ml::jiosaavn {

namespace {

// HTML entities used in JioSaavn titles ("Tum Hi Ho (From &quot;Aashiqui 2&quot;)").
std::string unescape(std::string s)
{
    struct { const char *from; const char *to; } kEnt[] = {
        {"&quot;",  "\""}, {"&#039;", "'"}, {"&apos;", "'"},
        {"&lt;",    "<"},  {"&gt;",   ">"}, {"&amp;",  "&"},
    };
    for (auto &e : kEnt) {
        size_t p = 0;
        while ((p = s.find(e.from, p)) != std::string::npos) {
            s.replace(p, strlen(e.from), e.to);
            p += strlen(e.to);
        }
    }
    // trim trailing/leading whitespace
    while (!s.empty() && (s.front() == ' ' || s.front() == '\n')) s.erase(s.begin());
    while (!s.empty() && (s.back()  == ' ' || s.back()  == '\n')) s.pop_back();
    return s;
}

// Upgrade JioSaavn image URL from 150x150 to 500x500 quality.
// Pattern: "...c.saavncdn.com/..._150x150.jpg" → "_500x500.jpg"
std::string upgradeArt(const std::string &url)
{
    if (url.empty()) return url;
    // Try the common _NNNxNNN suffix pattern
    size_t dot = url.rfind('.');
    if (dot == std::string::npos) return url;
    size_t x = url.rfind('_', dot);
    if (x == std::string::npos) return url;
    // Check it looks like dimensions
    std::string dim = url.substr(x + 1, dot - x - 1);  // e.g. "150x150"
    if (dim.find('x') == std::string::npos) return url;
    return url.substr(0, x) + "_500x500" + url.substr(dot);
}

}  // namespace

std::string searchUrl(const std::string &query, int limit, const std::string &base)
{
    int capped = (limit < 1) ? 1 : (limit > 50 ? 50 : limit);
    return base + "/api.php?__call=search.getResults"
           "&_format=json&_marker=0&api_version=4&ctx=android&p=1"
           "&n=" + std::to_string(capped) +
           "&q=" + percentEncode(query);
}

std::vector<catalog::Song> parseSearch(const Json &root)
{
    std::vector<catalog::Song> v;
    // Response: { "results": [ { "id", "title", "more_info": { ... } } ] }
    const Json &results = root["results"];
    for (size_t i = 0; i < results.size() && v.size() < 50; i++) {
        const Json &t = results[i];
        const Json &more = t["more_info"];

        std::string id = t["id"].asString();
        if (id.empty()) continue;

        std::string title = unescape(t["title"].asString());
        if (title.empty()) continue;

        // Artists: more_info.artistMap.primary_artists array
        std::string artist;
        const Json &amap = more["artistMap"]["primary_artists"];
        for (size_t j = 0; j < amap.size(); j++) {
            std::string name = unescape(amap[j]["name"].asString());
            if (name.empty()) continue;
            if (!artist.empty()) artist += ", ";
            artist += name;
        }
        // Fallback: more_info.singers plain string
        if (artist.empty()) artist = unescape(more["singers"].asString());

        // Skip tracks that have no encrypted URL — they can't be identified later
        std::string encUrl = more["encrypted_media_url"].asString();
        if (encUrl.empty()) continue;

        std::string album = unescape(more["album"].asString());

        // Cover: more_info.image (medium JPEG); upgrade to 500x500 for display
        std::string art = upgradeArt(more["image"].asString());
        if (art.empty()) art = upgradeArt(t["image"].asString());

        // duration comes as a string "204" in JioSaavn responses
        int durSec = 0;
        {
            std::string ds = more["duration"].asString();
            for (char c : ds) {
                if (c < '0' || c > '9') { durSec = 0; break; }
                durSec = durSec * 10 + (c - '0');
            }
            if (durSec < 0 || durSec > 86400) durSec = 0;
        }

        catalog::Song s;
        // Metadata ref — not a playable URL.
        s.url        = "jiosaavn:" + id;
        s.title      = std::move(title);
        s.artist     = std::move(artist);
        s.album      = std::move(album);
        s.art        = std::move(art);
        s.durationSec = durSec;
        s.source     = kName;
        v.push_back(std::move(s));
    }
    return v;
}

bool isJioSaavnRef(const std::string &url)
{
    return url.rfind("jiosaavn:", 0) == 0 && url.size() > 9;
}

}  // namespace ml::jiosaavn
