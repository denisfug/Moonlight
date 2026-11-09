// Music module: local library (MP3 files on the PS4 HD / USB), ID3 tags, playlists and the play queue.
// Independent of the video addon system (see docs/ARCHITECTURE.md).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "platform/platform.hpp"

namespace ml::music {

struct Track {
    std::string path;
    std::string title, artist, album;
    uint64_t sizeBytes = 0;
    bool demo = false;   // demonstration entry without an audio file
};

struct Playlist {
    std::string name;
    std::string source;          // folder path or "demo"
    std::vector<size_t> tracks;  // indices into MusicLibrary::tracks()
};

struct Id3Info {
    std::string title, artist, album;
    size_t tagBytes = 0;   // size of the ID3v2 tag at the start (0 = none)
};

// Parses an ID3v2.2/2.3/2.4 header + text frames from the first bytes of a file.
Id3Info parseId3(const std::string &head);
// "Artist - Title.mp3", "001 - Title.mp3" -> fills missing title/artist.
void titleFromFilename(const std::string &filename, std::string &title, std::string &artist);

class MusicLibrary {
public:
    // Every subfolder of each root is a playlist; MP3s directly in a root form "Soltas".
    // Reads at most 64 KiB of each file for tags. Returns the number of tracks found.
    size_t scan(FileSystem &fs, const std::vector<std::string> &roots);
    void addDemo();   // demonstration playlists (no audio) so the UI can be exercised without files
    const std::vector<Track> &tracks() const { return tracks_; }
    const std::vector<Playlist> &playlists() const { return playlists_; }
    std::vector<size_t> search(const std::string &query) const;   // case-insensitive title/artist/album

private:
    std::vector<Track> tracks_;
    std::vector<Playlist> playlists_;
};

enum class Repeat { Off, All, One };

// Play order over a list of track indices, with shuffle and repeat.
class Queue {
public:
    void set(std::vector<size_t> items, size_t startPos = 0);
    bool empty() const { return items_.empty(); }
    size_t size() const { return items_.size(); }
    size_t current() const;   // track index; size_t(-1) if empty
    size_t position() const { return pos_; }
    const std::vector<size_t> &order() const { return order_; }

    bool next(bool userAction);   // false = end of queue (Repeat::Off)
    bool previous();
    void setShuffle(bool on, uint32_t seed);
    bool shuffle() const { return shuffle_; }
    void setRepeat(Repeat r) { repeat_ = r; }
    Repeat repeat() const { return repeat_; }

private:
    std::vector<size_t> items_, order_;
    size_t pos_ = 0;
    bool shuffle_ = false;
    Repeat repeat_ = Repeat::All;
};

}  // namespace ml::music
