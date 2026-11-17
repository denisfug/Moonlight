// Openverse (api.openverse.org): open music licensed under Creative Commons,
// maintained by the Wikimedia Foundation. No account, no key for read-only
// search. Full-length MP3 streams from the original provider. Only tracks that
// have an audio_url AND are licensed for free use are shown.
//
// API: https://api.openverse.org/v1/
//   GET /v1/audio/?q=QUERY&license_type=commercial,modification&media_type=audio&page_size=N
//   Response: { "count", "results": [ { "id", "title", "foreign_landing_url",
//     "url", "duration", "creator", "thumbnail", "license", "license_version",
//     "tags": [ { "name" } ] } ] }
//
// Terms: results are CC-licensed content; attribution is required by the
// license. The "source" badge "Openverse" and the creator field cover this.
// Do NOT cache audio files locally (stream only). Openverse's own ToS:
// https://docs.openverse.org/terms_of_service.html
#pragma once
#include <string>
#include <vector>

#include "core/json.hpp"
#include "music/catalog.hpp"

namespace ml::openverse {

using Song = catalog::Song;
constexpr const char *kName = "Openverse";
constexpr const char *kHost = "https://api.openverse.org";
constexpr int kLimit        = 25;

// URL builders (base overridden only in tests).
// license_type=commercial,modification → CC0, CC BY, CC BY-SA (all free use).
std::string searchUrl(const std::string &query,
                      int limit           = kLimit,
                      const std::string &base = kHost);

// Most popular audio of the week — shows "In the spotlight" shelf.
std::string featuredUrl(int limit           = 20,
                        const std::string &base = kHost);

// Parse the /v1/audio/ response. Only tracks with a non-empty url are kept.
// Duration is in seconds (integer field "duration").
std::vector<Song> parseSearch(const Json &root);

}  // namespace ml::openverse
