// Deezer as a music source: free search + 30-second preview MP3 URLs.
// No authentication required for search and preview (Deezer Open API).
// Full playback requires OAuth + paid subscription and is NOT implemented here.
// Pure parsing/URL building; network runs in MultiFetch threads.
//
// API: https://api.deezer.com
//   GET /search?q=QUERY&limit=N   → tracks array with preview and cover URLs
//   Preview URL: https://cdns-preview-X.dzcdn.net/... (direct MP3, no auth)
//   Cover:  album.cover_xl (1000x1000) / album.cover_medium (250x250)
//
// Terms of Use (api.deezer.com): free, non-commercial use; no caching of audio
// content; display Deezer branding when showing results.
#pragma once
#include <string>
#include <vector>

#include "core/json.hpp"
#include "music/catalog.hpp"

namespace ml::deezer {

using Song = catalog::Song;
constexpr const char *kName  = "Deezer";
constexpr const char *kHost  = "https://api.deezer.com";
constexpr int         kLimit = 25;    // max per search, Deezer allows up to 25 by default

// URL builders (base overridden only in tests).
std::string searchUrl(const std::string &query,
                      int limit = kLimit,
                      const std::string &base = kHost);

// Parse the /search response. Only tracks with a non-empty preview URL are
// kept (is_playable on PS4). Duration is in seconds (Deezer field "duration").
std::vector<Song> parseSearch(const Json &root);

}  // namespace ml::deezer
