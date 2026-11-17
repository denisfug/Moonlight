// YouTube Data API v3 as a metadata/discovery source.
// Provides: search by track, title, artist, channel, thumbnail, duration, videoId.
// Does NOT provide audio streams — YouTube streams are AAC/fMP4 (not MP3-compatible)
// and stream extraction is prohibited by YouTube ToS §4(A) and YouTube API Developer
// Policies. This module is a metadata enrichment source only.
//
// API: https://developers.google.com/youtube/v3/docs/search/list
// Endpoint: GET https://www.googleapis.com/youtube/v3/search
//   ?part=snippet&q=QUERY&type=video&videoCategoryId=10&maxResults=N&key=APIKEY
// Thumbnail: snippet.thumbnails.high.url (480x360) or medium (320x180) or default (120x90)
// Duration: requires a second call to /videos?part=contentDetails&id=VIDEO_IDS&key=KEY
//           OR can be omitted (show 0) to avoid the extra round-trip.
//
// Authentication: API key (no OAuth needed for public search).
//   The user must obtain a free API key from console.cloud.google.com and configure it
//   in Moonlight Settings. Without a key, searches return HTTP 403.
//   kPublicApiKey is intentionally empty: embedding a key in the binary would expose it.
//
// Cross-source matching: the videoId field in catalog::Song is stored in the url field
// prefixed with "youtube:VIDEO_ID" so other modules can use it to look up the same
// track on Audius, Internet Archive, etc. This prefix is recognised by the player and
// treated as "no direct stream available" (the track is shown but cannot be played
// unless a cross-source match is found).
#pragma once
#include <string>
#include <vector>

#include "core/json.hpp"
#include "music/catalog.hpp"

namespace ml::youtube {

constexpr const char *kHost = "https://www.googleapis.com";
constexpr const char *kName = "YouTube";
// Music video category ID on YouTube (used to narrow results to music content).
constexpr const char *kMusicCategoryId = "10";

// URL for a combined search + snippet request.
// apiKey must be non-empty; limit is capped to 50 (YouTube API maximum for search).
std::string searchUrl(const std::string &query, const std::string &apiKey,
                      int limit = 25,
                      const std::string &base = kHost);

// URL to retrieve content-duration for a comma-separated list of videoIds.
// Used as a second request to fill durationSec after parseSearch() identifies IDs.
std::string durationsUrl(const std::string &videoIds, const std::string &apiKey,
                         const std::string &base = kHost);

// Parse the /youtube/v3/search response.
// url field is set to "youtube:<videoId>" — signals "metadata only, no direct stream".
// art is the highest-resolution thumbnail available.
// durationSec is left at 0 (requires a separate /videos?part=contentDetails call).
std::vector<catalog::Song> parseSearch(const Json &root);

// Parse /youtube/v3/videos?part=contentDetails response and fill durationSec
// into the songs vector, matching by videoId stored in song.url.
// Returns the number of durations filled.
int parseDurations(const Json &root, std::vector<catalog::Song> &songs);

// Returns true when url is a YouTube metadata reference ("youtube:VIDEO_ID").
bool isYouTubeRef(const std::string &url);

// Extract the videoId from a "youtube:VIDEO_ID" url. Returns "" on bad input.
std::string videoIdFrom(const std::string &url);

}  // namespace ml::youtube
