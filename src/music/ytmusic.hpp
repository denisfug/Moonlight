// YouTube Music via InnerTube — metadata/discovery source.
// Uses the public InnerTube search endpoint (no API key required for anonymous search).
// With a valid OAuth2 Bearer token (see ytauth.hpp), also fetches library,
// playlists, history and home recommendations.
//
// No audio streams: stream extraction from YouTube/YouTube Music is prohibited by
// YouTube ToS §4(A) and not implemented here.
// Results stored with url = "ytmusic:<videoId>" — metadata ref, not playable.
//
// Endpoints (all POST to https://music.youtube.com/youtubei/v1/):
//   search        — song search (anonymous or authenticated)
//   browse        — library/playlists/history/home (authenticated only)
//
// Architecture note: adapted from Mochy-Music InnerTube provider (GPL-3.0).
// Only the data model, URL structure and browse IDs are ported;
// network/HTTP is Moonlight's own WebClient.
#pragma once
#include <string>
#include <vector>

#include "core/json.hpp"
#include "music/catalog.hpp"

namespace ml::ytmusic {

constexpr const char *kHost = "https://music.youtube.com";
constexpr const char *kName = "YouTube Music";

// Browse IDs for authenticated endpoints (from Mochy-Music / InnerTube).
constexpr const char *kBrowseHome        = "FEmusic_home";
constexpr const char *kBrowseLiked       = "FEmusic_liked_videos";
constexpr const char *kBrowseHistory     = "FEmusic_history";
constexpr const char *kBrowseLibPlaylists= "FEmusic_liked_playlists";

// ---------------------------------------------------------------- body builders
// Returns the POST body for a song search (anonymous, no auth required).
std::string searchBody(const std::string &query);

// Returns the POST body for a browse request.
// browseId: one of the kBrowse* constants (or a playlist ID like "PLxxxx").
// token: continuation token (empty for first page).
// authBearer: "Bearer <access_token>" or "" for anonymous.
std::string browseBody(const std::string &browseId, const std::string &token = "",
                       const std::string &authBearer = "");

// ---------------------------------------------------------------- parsers
// Parse the /youtubei/v1/search response.
// url is set to "ytmusic:<videoId>"; art is the best thumbnail available.
// Skips non-song entries (artists, albums, playlists).
std::vector<catalog::Song> parseSearch(const Json &root);

// Parse a /youtubei/v1/browse response for liked songs / history.
// Returns songs; continuationToken is filled if more pages exist.
std::vector<catalog::Song> parseBrowseSongs(const Json &root,
                                             std::string *continuationToken = nullptr);

// Parse a browse response for playlists.
// Each playlist has title (playlist title), url = "ytplaylist:<playlistId>", art.
struct Playlist {
    std::string id;    // raw playlistId
    std::string title;
    std::string art;
};
std::vector<Playlist> parsePlaylists(const Json &root);

// ---------------------------------------------------------------- helpers
// Returns true when url is a YouTube Music metadata reference.
bool isYtMusicRef(const std::string &url);

// Extract videoId from a "ytmusic:<videoId>" url. Returns "" on bad input.
std::string videoIdFrom(const std::string &url);

}  // namespace ml::ytmusic
