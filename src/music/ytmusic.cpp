// YouTube Music InnerTube — metadata source (search, library, playlists, history, home).
// No audio stream. Adapted from Mochy-Music InnerTube provider architecture (GPL-3.0).
// Data model and endpoint structure ported to C++; network is Moonlight's own.
#include "music/ytmusic.hpp"

#include <cstdlib>
#include <cstring>
#include "core/net.hpp"

namespace ml::ytmusic {

namespace {

// Best thumbnail from a musicThumbnailRenderer.thumbnails array.
std::string bestThumbnail(const Json &thumbs)
{
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
    if (eq != std::string::npos && best.rfind("http", 0) == 0) {
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

// Parse a time string "M:SS" or "H:M:SS" to seconds. No exceptions.
static int safeAtoi(const char *s, const char *end)
{
    int v = 0;
    for (; s < end; s++) {
        if (*s < '0' || *s > '9') return 0;
        v = v * 10 + (*s - '0');
    }
    return v;
}

int parseTime(const std::string &s)
{
    if (s.empty()) return 0;
    // Format: [H:]M:SS
    int parts[3] = {0, 0, 0};
    int n = 0;
    size_t start = 0;
    for (size_t i = 0; i <= s.size() && n < 3; i++) {
        if (i == s.size() || s[i] == ':') {
            if (n < 3) parts[n++] = safeAtoi(s.c_str() + start, s.c_str() + i);
            start = i + 1;
        }
    }
    if (n == 2) return parts[0] * 60 + parts[1];
    if (n == 3) return parts[0] * 3600 + parts[1] * 60 + parts[2];
    return 0;
}

// JSON string escaper — no exceptions.
std::string jsonEsc(const std::string &s)
{
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:   out += c;      break;
        }
    }
    return out;
}

// InnerTube context block (WEB_REMIX client).
static const char *kCtx =
    "\"context\":{"
      "\"client\":{"
        "\"clientName\":\"WEB_REMIX\","
        "\"clientVersion\":\"1.20260707.12.00\","
        "\"gl\":\"US\","
        "\"hl\":\"en\""
      "}"
    "}";

// Try to parse a MusicResponsiveListItemRenderer JSON object into a catalog::Song.
bool parseSongItem(const Json &renderer, catalog::Song &song)
{
    // videoId
    std::string videoId = renderer["playlistItemData"]["videoId"].asString();
    if (videoId.empty())
        videoId = renderer["overlay"]["musicItemThumbnailOverlayRenderer"]
                          ["content"]["musicPlayButtonRenderer"]
                          ["playNavigationEndpoint"]["watchEndpoint"]["videoId"].asString();
    if (videoId.empty()) {
        // Also try navigationEndpoint directly (history items)
        videoId = renderer["flexColumns"][0]
                          ["musicResponsiveListItemFlexColumnRenderer"]["text"]["runs"][0]
                          ["navigationEndpoint"]["watchEndpoint"]["videoId"].asString();
    }
    if (videoId.empty()) return false;

    // flexColumns[0] = title
    std::string title = runsText(
        renderer["flexColumns"][0]
                ["musicResponsiveListItemFlexColumnRenderer"]["text"]["runs"]);
    if (title.empty()) return false;

    // flexColumns[1] = "Song · Artist · Album · Duration"
    const Json &col1runs = renderer["flexColumns"][1]
                                   ["musicResponsiveListItemFlexColumnRenderer"]["text"]["runs"];

    std::string artist, album, duration;
    bool firstArtistSet = false;
    for (size_t i = 0; i < col1runs.size(); i++) {
        const Json &run = col1runs[i];
        std::string text = run["text"].asString();
        if (text.empty() || text == " \xe2\x80\xa2 " || text == " \xc2\xb7 " || text == " • ") continue;
        const Json &ep = run["navigationEndpoint"];
        std::string browseId = ep["browseEndpoint"]["browseId"].asString();
        // Artists: browseId starts with "UC"; Albums: "MPREb_"
        if (browseId.rfind("UC", 0) == 0) {
            if (!artist.empty()) artist += ", ";
            artist += text;
            firstArtistSet = true;
        } else if (browseId.rfind("MPREb_", 0) == 0) {
            if (album.empty()) album = text;
        } else {
            (void)firstArtistSet;
        }
        // Duration: last plain-text run matching M:SS
        if (browseId.empty() && text.find(':') != std::string::npos && text.size() <= 8)
            duration = text;
    }

    std::string art = bestThumbnail(
        renderer["thumbnail"]["musicThumbnailRenderer"]["thumbnail"]["thumbnails"]);

    song.url         = "ytmusic:" + videoId;
    song.title       = std::move(title);
    song.artist      = std::move(artist);
    song.album       = std::move(album);
    song.art         = std::move(art);
    song.durationSec = parseTime(duration);
    song.source      = kName;
    return true;
}

// Recursively collect songs from sectionListRenderer / musicShelfRenderer.
void collectSongs(const Json &node, std::vector<catalog::Song> &out)
{
    if (out.size() >= 50) return;

    // musicShelfRenderer → contents[] → musicResponsiveListItemRenderer
    const Json &shelf = node["musicShelfRenderer"]["contents"];
    for (size_t i = 0; i < shelf.size() && out.size() < 50; i++) {
        const Json &item = shelf[i]["musicResponsiveListItemRenderer"];
        if (!item.isNull()) {
            catalog::Song s;
            if (parseSongItem(item, s)) out.push_back(std::move(s));
        }
    }

    // musicImmersiveListItemRenderer (alternative renderer in some contexts)
    const Json &immShelf = node["musicImmersiveListItemRenderer"];
    if (!immShelf.isNull()) {
        catalog::Song s;
        if (parseSongItem(immShelf, s)) out.push_back(std::move(s));
    }

    // sectionListRenderer → contents[] (may nest)
    const Json &slContents = node["sectionListRenderer"]["contents"];
    for (size_t i = 0; i < slContents.size() && out.size() < 50; i++)
        collectSongs(slContents[i], out);

    // tabbedSearchResultsRenderer → tabs[0] → tabRenderer → content
    const Json &tabs = node["tabbedSearchResultsRenderer"]["tabs"];
    for (size_t i = 0; i < tabs.size() && out.size() < 50; i++)
        collectSongs(tabs[i]["tabRenderer"]["content"], out);

    // musicPlaylistShelfRenderer (library pages)
    const Json &plShelf = node["musicPlaylistShelfRenderer"]["contents"];
    for (size_t i = 0; i < plShelf.size() && out.size() < 50; i++) {
        const Json &item = plShelf[i]["musicResponsiveListItemRenderer"];
        if (!item.isNull()) {
            catalog::Song s;
            if (parseSongItem(item, s)) out.push_back(std::move(s));
        }
    }
}

// Collect from a browse response's musicShelfContinuation (paged).
void collectSongsContinuation(const Json &node, std::vector<catalog::Song> &out)
{
    const Json &cont = node["musicShelfContinuation"]["contents"];
    for (size_t i = 0; i < cont.size() && out.size() < 50; i++) {
        const Json &item = cont[i]["musicResponsiveListItemRenderer"];
        if (!item.isNull()) {
            catalog::Song s;
            if (parseSongItem(item, s)) out.push_back(std::move(s));
        }
    }
}

}  // namespace

// ---------------------------------------------------------------- body builders

std::string searchBody(const std::string &query)
{
    // params = "EgWKAQIIAWoKEAoQAxAEEAkQBQ==" filters to Songs only in YTMusic.
    return std::string("{") + kCtx +
           ",\"query\":\"" + jsonEsc(query) + "\","
           "\"params\":\"EgWKAQIIAWoKEAoQAxAEEAkQBQ==\""
           "}";
}

std::string browseBody(const std::string &browseId, const std::string &token,
                       const std::string & /*authBearer — passed as header, not in body*/)
{
    std::string s = std::string("{") + kCtx;
    if (!token.empty()) {
        s += ",\"continuation\":\"" + jsonEsc(token) + "\"";
    } else {
        s += ",\"browseId\":\"" + jsonEsc(browseId) + "\"";
    }
    s += "}";
    return s;
}

// ---------------------------------------------------------------- parsers

std::vector<catalog::Song> parseSearch(const Json &root)
{
    std::vector<catalog::Song> v;
    collectSongs(root["contents"], v);
    return v;
}

std::vector<catalog::Song> parseBrowseSongs(const Json &root, std::string *continuationToken)
{
    std::vector<catalog::Song> v;

    // Standard browse page
    const Json &header = root["header"];
    (void)header;

    // Contents may be in root["contents"] (first page) or root["continuationContents"] (paged)
    const Json &cont = root["continuationContents"];
    if (!cont.isNull()) {
        collectSongsContinuation(cont, v);
        if (continuationToken) {
            // Look for nextContinuationData
            *continuationToken = cont["musicShelfContinuation"]
                                     ["continuations"][0]
                                     ["nextContinuationData"]["continuation"].asString();
        }
        return v;
    }

    const Json &contents = root["contents"];
    collectSongs(contents, v);

    // Continuation token for first-page response
    if (continuationToken) {
        // May be in sectionListRenderer.continuations
        *continuationToken = contents["singleColumnBrowseResultsRenderer"]
                                     ["tabs"][0]["tabRenderer"]["content"]
                                     ["sectionListRenderer"]["continuations"][0]
                                     ["nextContinuationData"]["continuation"].asString();
    }

    return v;
}

std::vector<Playlist> parsePlaylists(const Json &root)
{
    std::vector<Playlist> out;

    // Library playlists appear in gridRenderer items
    // root → contents → singleColumnBrowseResultsRenderer → tabs[0] → content →
    //        sectionListRenderer → contents[] → gridRenderer → items[] →
    //        musicTwoRowItemRenderer
    const Json &tabs = root["contents"]["singleColumnBrowseResultsRenderer"]["tabs"];
    const Json &sec  = tabs[0]["tabRenderer"]["content"]["sectionListRenderer"]["contents"];
    for (size_t i = 0; i < sec.size(); i++) {
        const Json &grid = sec[i]["gridRenderer"]["items"];
        for (size_t j = 0; j < grid.size(); j++) {
            const Json &item = grid[j]["musicTwoRowItemRenderer"];
            if (item.isNull()) continue;

            std::string title = runsText(item["title"]["runs"]);
            if (title.empty()) continue;

            std::string plId = item["navigationEndpoint"]["browseEndpoint"]["browseId"].asString();
            // plId is "VLPLxxxx" for playlists; strip "VL" prefix → plain playlist ID
            std::string cleanId = plId;
            if (cleanId.rfind("VL", 0) == 0) cleanId = cleanId.substr(2);

            std::string art = bestThumbnail(
                item["thumbnailRenderer"]["musicThumbnailRenderer"]["thumbnail"]["thumbnails"]);

            Playlist pl;
            pl.id    = cleanId;
            pl.title = std::move(title);
            pl.art   = std::move(art);
            out.push_back(std::move(pl));
        }
    }
    return out;
}

// ---------------------------------------------------------------- helpers

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
