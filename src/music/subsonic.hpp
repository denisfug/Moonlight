// Client for Subsonic-compatible music servers (Navidrome, Gonic, Airsonic, Ampache...), written from
// the OpenSubsonic API docs (Apache-2.0): https://opensubsonic.netlify.app / github.com/opensubsonic.
// The user runs the server on their own PC with their own music; Moonlight streams it over Wi-Fi.
// Streams are requested as MP3 (format=mp3) so the server transcodes anything minimp3 can't decode.
#pragma once
#include <string>
#include <vector>

#include "core/json.hpp"
#include "core/net.hpp"
#include "storage/store.hpp"

namespace ml::subsonic {

struct Config {
    std::string url;     // e.g. http://192.168.15.10:4533
    std::string user;
    std::string salt, token;   // token = md5(password + salt); the password itself is never stored
    bool configured() const { return !url.empty() && !user.empty() && !token.empty(); }
};

// Creates salt+token from the password (salt derived from `entropy`, e.g. the clock).
Config makeConfig(const std::string &url, const std::string &user, const std::string &password, uint64_t entropy);
Config loadConfig(JsonStore &s);
bool saveConfig(JsonStore &s, const Config &c);

struct Song {
    std::string id, title, artist, album, coverArt, suffix;
    int durationSec = 0, track = 0;
};

struct Playlist {
    std::string id, name;
    int songCount = 0;
};

class Client {
public:
    Client(HttpClient *http, Config cfg) : http_(http), cfg_(std::move(cfg)) {}
    const Config &config() const { return cfg_; }

    std::string ping();   // "" = credentials and server OK
    std::string playlists(std::vector<Playlist> &out);
    std::string playlist(const std::string &id, std::vector<Song> &out);
    std::string randomSongs(int size, std::vector<Song> &out);
    std::string search(const std::string &query, std::vector<Song> &out);
    // URL that streams a song as MP3 (contains credentials: never log it, use redactUrl).
    std::string streamUrl(const std::string &songId, int maxKbps = 192) const;

    // Exposed for tests.
    std::string endpoint(const std::string &method, const std::string &extraQuery) const;
    static std::string parse(const std::string &body, Json &payload);   // checks status / error codes

private:
    HttpClient *http_;
    Config cfg_;
    std::string call(const std::string &method, const std::string &extraQuery, Json &payload);
};

std::vector<Song> parseSongs(const Json &arr);

}  // namespace ml::subsonic
