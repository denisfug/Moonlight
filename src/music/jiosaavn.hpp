// JioSaavn music catalog — metadata-only source (search, covers, track info).
// No audio stream: JioSaavn's stream URLs are DES-encrypted; Moonlight does not
// implement stream decryption for third-party services.
// Free public search endpoint, no account or key required.
//
// API: https://www.jiosaavn.com/api.php?__call=search.getResults&_format=json
//      &_marker=0&api_version=4&ctx=android&p=1&n=N&q=QUERY
// Response fields used: id, title, more_info.{album,artistMap,duration,image}
// Cover: more_info.image (medium JPEG); _150x150 suffix → _500x500 for HQ.
//
// Attribution: results show "JioSaavn" as source, linking to jiosaavn.com.
// Terms: https://www.jiosaavn.com/s/webapi — search is publicly accessible.
#pragma once
#include <string>
#include <vector>

#include "core/json.hpp"
#include "music/catalog.hpp"

namespace ml::jiosaavn {

constexpr const char *kHost = "https://www.jiosaavn.com";
constexpr const char *kName = "JioSaavn";
constexpr int         kLimit = 20;

// Build the search URL. base is overridden only in tests.
std::string searchUrl(const std::string &query,
                      int limit = kLimit,
                      const std::string &base = kHost);

// Parse the search response.
// Songs with no title are skipped. url is set to "jiosaavn:<id>" (metadata ref,
// not playable). art is upgraded to 500x500 when available.
std::vector<catalog::Song> parseSearch(const Json &root);

// Returns true when url is a JioSaavn metadata reference.
bool isJioSaavnRef(const std::string &url);

}  // namespace ml::jiosaavn
