// YouTube Music InnerTube — metadata source (search, covers, track info only).
// No audio stream. Adapted from Mochy-Music InnerTube provider architecture (GPL-3.0).
// Data model and endpoint structure ported to C++; network is Moonlight's own.
#include "music/ytmusic.hpp"

#include "core/net.hpp"

namespace ml::ytmusic {

namespace {

// Best thumbnail from a musicThumbnailRenderer.thumbnails array.
// InnerTube usually provides two sizes; we pick the largest.
std::string bestThumbnail(const Json &thumbs)
{
    // thumbnails is an array of {url, width, height}; last entry is typically largest
    std::string best;
    int bestW = 0;
    for (size_t i = 0; i < thumbs.size(); i++) {
        int w = thumbs[i]["width"].asInt(0);
        std::string u = thumbs[i]["url"].asString();
        if (u.rfind("http", 0) != 0) continue;
        if (w > bestW || best.empty()) { bestW = w; best = u; }
    }
    // Strip query params that limit resolution (e.g. =w60-h60) for a cleaner URL
    size_t eq = best.find('=');
    if (eq != std::string::npos && best.substr(0, 4) == "http") {
        // only strip if the param looks like a size constraint
        const std::string sub = best.substr(eq + 1);
        if (!sub.empty() && (sub[0] == 'w' || sub[0] == 's')) return best.substr(0, eq);
    }
    return best;
}

// Extract the flat runs text from a "runs" array.
std::string runsText(const Json &runs)
{
    std::string out;
    for (size_t i = 0; i < runs.size(); i++) {
        std::string t = runs[i]["text"].asString();
        if (!t.empty()) { if (!out.empty()) out += ' '; out += t; }
    }
    return out;
}

// Parse a time string "M:SS" or "H:M:SS" to seconds.
int parseTime(const std::string &s)
{
    if (s.empty()) return 0;
    int parts[3] = {0, 0, 0};
    int n = 0;
    size_t start = 0;
    for (size_t i = 0; i <= s.size() && n < 3; i++) {
        if (i == s.size() || s[i] == ':') {
            parts[n++] = std::stoi(s.substr(start, i - start));
            start = i + 1;
        }
    }
    if (n == 2) return parts[0] * 60 + parts[1];
    if (n == 3) return parts[0] * 3600 + parts[1] * 60 + parts[2];
    return 0;
}

// Try to parse a MusicResponsiveListItemRenderer JSON object.
// Returns false and leaves song unchanged on failure.
bool parseSongItem(const Json &renderer, catalog::Song &song)
{
    // videoId is in playlistItemData or overlay
    std::string videoId = renderer["playlistItemData"]["videoId"].asString();
    if (videoId.empty())
        videoId = renderer["overlay"]["musicItemThumbnailOverlayRenderer"]
                          ["content"]["musicPlayButtonRenderer"]
                          ["playNavigationEndpoint"]["watchEndpoint"]["videoId"].asString();
    if (videoId.empty()) return false;

    // flexColumns[0] = title
    std::string title = runsText(
        renderer["flexColumns"][0]
                ["musicResponsiveListItemFlexColumnRenderer"]["text"]["runs"]);
    if (title.empty()) return false;

    // flexColumns[1] = "Song · Artist · Album · Duration"
    // We read all runs and classify by navigation type.
    const Json &col1runs = renderer["flexColumns"][1]
                                   ["musicResponsiveListItemFlexColumnRenderer"]["text"]["runs"];

    std::string artist, album, duration;
    bool firstArtistSet = false;
    for (size_t i = 0; i < col1runs.size(); i++) {
        const Json &run = col1runs[i];
        std::string text = run["text"].asString();
        if (text.empty() || text == " \xe2\x80\xa2 " || text == " • ") continue;
        const Json &ep = run["navigationEndpoint"];
        std::string browseId = ep["browseEndpoint"]["browseId"].asString();
        // Artists: browseId starts with "UC" (channel); Albums: "MPREb_"
        if (browseId.rfind("UC", 0) == 0) {
            if (!artist.empty()) artist += ", ";
            artist += text;
            firstArtistSet = true;
        } else if (browseId.rfind("MPREb_", 0) == 0 || browseId.rfind("FEmusic_", 0) == 0) {
            if (album.empty()) album = text;
        } else if (!firstArtistSet && browseId.empty()) {
            // first text before an artist/album is often the song type ("Song") — skip
        }
        // Duration: last plain text run that looks like M:SS
        if (browseId.empty() && text.find(':') != std::string::npos && text.size() <= 8)
            duration = text;
    }

    // Thumbnail
    std::string art = bestThumbnail(
        renderer["thumbnail"]["musicThumbnailRenderer"]["thumbnail"]["thumbnails"]);

    song.url        = "ytmusic:" + videoId;
    song.title      = std::move(title);
    song.artist     = std::move(artist);
    song.album      = std::move(album);
    song.art        = std::move(art);
    song.durationSec = parseTime(duration);
    song.source     = kName;
    return true;
}

// Recursively walk sectionListRenderer contents looking for musicShelfRenderer items.
void collectSongs(const Json &node, std::vector<catalog::Song> &out)
{
    if (out.size() >= 50) return;

    // musicShelfRenderer → contents[] → musicResponsiveListItemRenderer
    const Json &shelf = node["musicShelfRenderer"]["contents"];
    for (size_t i = 0; i < shelf.size() && out.size() < 50; i++) {
        const Json &item = shelf[i]["musicResponsiveListItemRenderer"];
        if (item.isNull()) continue;
        catalog::Song s;
        if (parseSongItem(item, s)) out.push_back(std::move(s));
    }

    // sectionListRenderer → contents[] (may nest)
    const Json &slContents = node["sectionListRenderer"]["contents"];
    for (size_t i = 0; i < slContents.size() && out.size() < 50; i++)
        collectSongs(slContents[i], out);

    // tabbedSearchResultsRenderer → tabs[0] → tabRenderer → content
    const Json &tabs = node["tabbedSearchResultsRenderer"]["tabs"];
    for (size_t i = 0; i < tabs.size() && out.size() < 50; i++)
        collectSongs(tabs[i]["tabRenderer"]["content"], out);
}

}  // namespace

std::string searchBody(const std::string &query)
{
    // params = "EgWKAQIIAWoKEAoQAxAEEAkQBQ==" filters to Songs only in YTMusic search.
    // Escape the query for JSON manually (handle the most common special chars).
    std::string escaped;
    escaped.reserve(query.size() + 8);
    for (char c : query) {
        switch (c) {
        case '"':  escaped += "\\\""; break;
        case '\\': escaped += "\\\\"; break;
        case '\n': escaped += "\\n";  break;
        case '\r': escaped += "\\r";  break;
        case '\t': escaped += "\\t";  break;
        default:   escaped += c;      break;
        }
    }
    return
        "{"
        "\"context\":{"
          "\"client\":{"
            "\"clientName\":\"WEB_REMIX\","
            "\"clientVersion\":\"1.20260707.12.00\","
            "\"gl\":\"US\","
            "\"hl\":\"en\""
          "}"
        "},"
        "\"query\":\"" + escaped + "\","
        "\"params\":\"EgWKAQIIAWoKEAoQAxAEEAkQBQ==\""
        "}";
}

std::vector<catalog::Song> parseSearch(const Json &root)
{
    std::vector<catalog::Song> v;
    // Top-level shape: { "contents": { ... } }
    collectSongs(root["contents"], v);
    return v;
}

bool isYtMusicRef(const std::string &url)
{
    return url.rfind("ytmusic:", 0) == 0 && url.size() > 8;
}

std::string videoIdFrom(const std::string &url)
{
    if (!isYtMusicRef(url)) return "";
    return url.substr(8);
}

}  // namespace ml::ytmusic
