// Internet Archive (archive.org) as a music source: public-domain / Creative Commons audio, no account,
// no key, no DRM. Written from the public API description (advancedsearch.php + /metadata/<id> +
// /download/<id>/<file>). The Archive searches ITEMS (albums, concerts, collections), not single tracks,
// so a search returns albums and the tracks come from the item's file list. Only MP3 files are used
// (the app's decoder is minimp3); items that exist only as OGG/FLAC yield no tracks.
// Pure parsing/URL building here: the network runs in MultiFetch threads (never on the UI thread).
#pragma once
#include <string>
#include <vector>

#include "core/json.hpp"

namespace ml::archive {

struct Item {
    std::string id, title, creator;
};

struct Song {
    std::string url;   // https://archive.org/download/<id>/<file> (redirects to a data server)
    std::string title, artist, album;
    int durationSec = 0, track = 0;
};

constexpr const char *kHost = "https://archive.org";

// base is only replaced by the tests (local fixture server).
std::string searchUrl(const std::string &query, int rows = 30, const std::string &base = kHost);
std::string metadataUrl(const std::string &id, const std::string &base = kHost);
bool validId(const std::string &id);   // letters, digits, . _ - only (what the Archive itself allows)

std::vector<Item> parseSearch(const Json &root);
// MP3 files of an item in track order. Prefers the "VBR MP3" set when the item has several encodings.
std::vector<Song> parseItem(const Json &root, const std::string &base = kHost);
int parseLength(const std::string &s);   // "215.4" or "3:35" or "1:02:03" -> seconds, 0 = unknown

}  // namespace ml::archive
