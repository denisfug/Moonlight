// What the owner listens to, kept only on the console (music_taste.json): the last songs played and
// how often each genre / artist was played. It feeds the "Tocadas recentemente" shelf and the
// recommendations ("Porque voce ouve <genero>", "Mais de <artista>"). Nothing is sent anywhere: the
// recommendations are ordinary public queries (trending of a genre, search of an artist name).
#pragma once
#include <string>
#include <utility>
#include <vector>

#include "core/json.hpp"
#include "music/music.hpp"

namespace ml::music {

struct Played {
    std::string title, artist, album, genre, art, source;
    std::string url;    // online song (wins); else path + remote identify a local / server song
    std::string path;
    bool remote = false;
    int64_t durationMs = 0;
};

class Taste {
public:
    static constexpr size_t kMaxRecent = 30, kMaxCounted = 200;
    void load(const Json &j);
    Json save() const;
    void notePlay(const Track &t);   // demo tracks are ignored
    const std::vector<Played> &recent() const { return recent_; }   // newest first, no repeats
    std::string topGenre() const { return top(genres_); }           // "" until something with a genre was played
    std::string topArtist() const { return top(artists_); }
    void clear() { recent_.clear(); genres_.clear(); artists_.clear(); }

private:
    using Counts = std::vector<std::pair<std::string, int>>;   // most recently played first
    std::vector<Played> recent_;
    Counts genres_, artists_;
    static void bump(Counts &c, const std::string &name);
    static std::string top(const Counts &c);
};

}  // namespace ml::music
