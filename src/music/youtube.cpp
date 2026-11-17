// YouTube Data API v3 — metadata/discovery source.
// See youtube.hpp for the full contract and restrictions.
#include "music/youtube.hpp"

#include "core/net.hpp"

namespace ml::youtube {

namespace {

// YouTube ISO 8601 duration "PT4M33S" -> seconds.
int parseIso8601Duration(const std::string &s)
{
    // Format: PT[nH][nM][nS]  (hours/minutes/seconds all optional)
    if (s.size() < 3 || s[0] != 'P' || s[1] != 'T') return 0;
    int total = 0, cur = 0;
    for (size_t i = 2; i < s.size(); i++) {
        char ch = s[i];
        if (ch >= '0' && ch <= '9') {
            cur = cur * 10 + (ch - '0');
        } else if (ch == 'H') { total += cur * 3600; cur = 0; }
        else if (ch == 'M') { total += cur * 60;   cur = 0; }
        else if (ch == 'S') { total += cur;         cur = 0; }
        else return 0;   // unexpected char -> treat as unknown
    }
    return (total > 0 && total < 86400 * 7) ? total : 0;
}

// Pick the best thumbnail URL from snippet.thumbnails.
std::string bestThumbnail(const Json &thumbs)
{
    // Preference: high (480x360) > medium (320x180) > default (120x90)
    for (const char *k : {"high", "medium", "default"}) {
        std::string u = thumbs[k]["url"].asString();
        if (!u.empty() && u.rfind("http", 0) == 0) return u;
    }
    return "";
}

}  // namespace

std::string searchUrl(const std::string &query, const std::string &apiKey,
                      int limit, const std::string &base)
{
    if (apiKey.empty()) return "";
    int capped = (limit < 1) ? 1 : (limit > 50 ? 50 : limit);
    return base + "/youtube/v3/search"
           "?part=snippet"
           "&type=video"
           "&videoCategoryId=" + std::string(kMusicCategoryId) +
           "&q=" + percentEncode(query) +
           "&maxResults=" + std::to_string(capped) +
           "&key=" + percentEncode(apiKey);
}

std::string durationsUrl(const std::string &videoIds, const std::string &apiKey,
                         const std::string &base)
{
    if (apiKey.empty() || videoIds.empty()) return "";
    return base + "/youtube/v3/videos"
           "?part=contentDetails"
           "&id=" + percentEncode(videoIds) +
           "&key=" + percentEncode(apiKey);
}

std::vector<catalog::Song> parseSearch(const Json &root)
{
    std::vector<catalog::Song> v;
    const Json &items = root["items"];
    for (size_t i = 0; i < items.size() && v.size() < 50; i++) {
        const Json &item = items[i];

        // Only video results are useful (search?type=video should guarantee this,
        // but we check defensively).
        std::string kind = item["id"]["kind"].asString();
        if (kind != "youtube#video") continue;

        std::string videoId = item["id"]["videoId"].asString();
        if (videoId.empty()) continue;

        const Json &snip = item["snippet"];
        std::string title = snip["title"].asString();
        if (title.empty()) continue;

        catalog::Song s;
        // Store the videoId as a metadata reference, not a playable stream URL.
        s.url    = "youtube:" + videoId;
        s.title  = title;
        s.artist = snip["channelTitle"].asString();
        // YouTube search results don't return album; leave it empty.
        // durationSec left at 0 — filled by a separate parseDurations() call.
        s.art    = bestThumbnail(snip["thumbnails"]);
        s.source = kName;
        v.push_back(std::move(s));
    }
    return v;
}

int parseDurations(const Json &root, std::vector<catalog::Song> &songs)
{
    int filled = 0;
    const Json &items = root["items"];
    for (size_t i = 0; i < items.size(); i++) {
        std::string vid = items[i]["id"].asString();
        if (vid.empty()) continue;
        std::string ref = "youtube:" + vid;
        int dur = parseIso8601Duration(
            items[i]["contentDetails"]["duration"].asString());
        for (auto &s : songs) {
            if (s.url == ref && s.durationSec == 0 && dur > 0) {
                s.durationSec = dur;
                filled++;
                break;
            }
        }
    }
    return filled;
}

bool isYouTubeRef(const std::string &url)
{
    return url.rfind("youtube:", 0) == 0 && url.size() > 8;
}

std::string videoIdFrom(const std::string &url)
{
    if (!isYouTubeRef(url)) return "";
    return url.substr(8);
}

}  // namespace ml::youtube
