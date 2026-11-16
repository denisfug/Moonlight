// Audius (audius.co): open music platform where artists publish their own tracks. Public REST API,
// no account and no key (only an app name), search by track, MP3 stream, covers in three sizes.
// Written from the public API description: /v1/tracks/search, /v1/tracks/trending,
// /v1/tracks/<id>/stream. Tracks the API marks as not streamable or gated (paid/followers only)
// are left out: the app only plays what the source offers openly.
#pragma once
#include "music/catalog.hpp"

namespace ml::audius {

constexpr const char *kHost = "https://api.audius.co";
constexpr const char *kName = "Audius";

std::string searchUrl(const std::string &query, const std::string &base);
std::string trendingUrl(const std::string &base);   // most played of the week
std::string streamUrl(const std::string &id, const std::string &base);
bool validId(const std::string &id);   // letters and digits only
std::vector<catalog::Song> parseTracks(const Json &root, const std::string &base);

}  // namespace ml::audius
