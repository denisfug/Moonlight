// Shared model of the online music sources (connectors). Each source lives in its own file
// (archive.*, audius.*): it builds request addresses and turns the answer into these structs; the
// screen only sees Song/Item and the SongSource table, never a source's JSON.
#pragma once
#include <string>
#include <vector>

#include "core/json.hpp"

namespace ml::catalog {

struct Song {
    std::string url;      // direct audio address the source allows players to use (MP3)
    std::string title, artist, album;
    std::string art;      // cover address, "" when the source has none (never a cover of something else)
    std::string source;   // name shown next to the result ("Audius", "Internet Archive")
    int durationSec = 0, track = 0;
};

// A source that can search single songs: address of the search + reading of the answer.
struct SongSource {
    const char *name;
    std::string (*searchUrl)(const std::string &query, const std::string &base);
    std::vector<Song> (*parse)(const Json &root, const std::string &base);
};

}  // namespace ml::catalog
