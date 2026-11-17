// YouTube Music via InnerTube — metadata/discovery source.
// Uses the public InnerTube search endpoint (no API key, no account required for
// anonymous search). Results are music-only (songs, EP/album tracks) and include
// title, artists, album, thumbnail and duration.
//
// No audio streams: stream extraction from YouTube/YouTube Music is prohibited by
// YouTube ToS §4(A) and not implemented here.
// Results stored with url = "ytmusic:<videoId>" — metadata ref, not playable.
//
// Endpoint: POST https://music.youtube.com/youtubei/v1/search
//   Body: { "context": { "client": { "clientName": "WEB_REMIX",
//             "clientVersion": "1.20260707.12.00", "gl": "US", "hl": "en" } },
//           "query": "QUERY", "params": "EgWKAQIIAWoKEAoQAxAEEAkQBQ==" }
//   The params value filters to songs only (EgWKAQIIAW = Song filter in YTMusic).
//
// Architecture note: adapted from Mochy-Music InnerTube provider (GPL-3.0).
// Only the data model and URL structure are ported; network/HTTP is Moonlight's own.
#pragma once
#include <string>
#include <vector>

#include "core/json.hpp"
#include "music/catalog.hpp"

namespace ml::ytmusic {

constexpr const char *kHost = "https://music.youtube.com";
constexpr const char *kName = "YouTube Music";

// Returns the POST body (JSON) for a YouTube Music song search.
// Caller sends this to kHost + "/youtubei/v1/search".
std::string searchBody(const std::string &query);

// Parse the /youtubei/v1/search response.
// url is set to "ytmusic:<videoId>"; art is the best thumbnail available.
// Skips non-song entries (artists, albums, playlists).
std::vector<catalog::Song> parseSearch(const Json &root);

// Returns true when url is a YouTube Music metadata reference.
bool isYtMusicRef(const std::string &url);

// Extract videoId from a "ytmusic:<videoId>" url. Returns "" on bad input.
std::string videoIdFrom(const std::string &url);

}  // namespace ml::ytmusic
