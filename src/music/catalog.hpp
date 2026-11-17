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
    std::string title, artist, album, genre;
    std::string art;      // cover address, "" when the source has none (never a cover of something else)
    std::string source;   // name shown next to the result ("Audius", "Internet Archive")
    int durationSec = 0, track = 0;
    int previewSec = 0;   // >0 when only a preview clip plays (e.g. Deezer: 30 s); 0 means full stream
};

// A source that can search single songs: address of the search + reading of the answer.
struct SongSource {
    const char *name;
    std::string (*searchUrl)(const std::string &query, const std::string &base);
    std::vector<Song> (*parse)(const Json &root, const std::string &base);
};

}  // namespace ml::catalog
