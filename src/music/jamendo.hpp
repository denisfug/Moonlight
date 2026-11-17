// Jamendo as a music source: search + free MP3 stream.
// All tracks on Jamendo are under Creative Commons licenses — legal for
// streaming and non-commercial use. Full-length MP3s, no authentication
// for search/stream (only client_id from devportal.jamendo.com is required).
//
// API: https://api.jamendo.com/v3.0
//   GET /tracks/?client_id=CLIENTID&search=QUERY&limit=N&format=json
//       &audioformat=mp32&include=musicinfo&imagesize=500
//   Response: { "results": [ { "id", "name", "duration", "artist_name",
//     "album_name", "audio", "image" } ] }
//
// Terms of Use: free non-commercial use with attribution; display Jamendo
// branding when showing results; do NOT cache audio files.
// client_id must be obtained from devportal.jamendo.com and stored in
// Settings — never hard-coded here.
#pragma once
#include <string>
#include <vector>

#include "core/json.hpp"
#include "music/catalog.hpp"

namespace ml::jamendo {

using Song = catalog::Song;
constexpr const char *kName  = "Jamendo";
constexpr const char *kHost  = "https://api.jamendo.com";
constexpr int         kLimit = 25;

// URL builders (base overridden only in tests).
// client_id must be non-empty for the URL to be valid; callers check that.
std::string searchUrl(const std::string &query,
                      const std::string &clientId,
                      int limit = kLimit,
                      const std::string &base = kHost);

// Parse the /tracks/ response. Only tracks with a non-empty audio URL are kept.
// Duration is in seconds (Jamendo field "duration").
std::vector<Song> parseSearch(const Json &root);

}  // namespace ml::jamendo
