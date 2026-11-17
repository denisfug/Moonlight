// Internet radio via the Radio Browser community directory (https://api.radio-browser.info):
// free, no key, no ads from the app. Only MP3 stations are listed, because minimp3 is the decoder.
// Endpoints used (from the public API): /json/stations/search, /json/url/{stationuuid}.
#pragma once
#include <string>
#include <vector>

#include "core/json.hpp"
#include "core/net.hpp"
#include "storage/store.hpp"

namespace ml::radio {

struct Station {
    std::string uuid, name, url, homepage, favicon, tags, countryCode, language, codec;
    int bitrate = 0, votes = 0;
    bool hls = false, lastCheckOk = true;
    bool playable() const;   // MP3, not HLS, valid http(s) URL, last check ok
};

// Parses the JSON array returned by the station endpoints (tolerates number/bool/string variations).
std::vector<Station> parseStations(const Json &arr);

struct Query {
    std::string name, tag, countryCode;   // empty = not filtered
    std::string order = "votes";          // votes | clickcount | name
    int limit = 80;
};
// "/json/stations/search?..." (always codec=MP3, hidebroken=true, reverse=true for votes/clicks)
std::string searchPath(const Query &q);

class RadioBrowser {
public:
    explicit RadioBrowser(HttpClient *http) : http_(http) {}
    // Tries each server in order until one answers. Returns "" or a user-facing error.
    std::string search(const Query &q, std::vector<Station> &out);
    void countClick(const std::string &uuid);   // asks the directory to count a play (best effort)
    std::vector<std::string> servers{"https://all.api.radio-browser.info", "https://de1.api.radio-browser.info",
                                     "https://fi1.api.radio-browser.info"};
    HttpClient *http() const { return http_; }

private:
    HttpClient *http_;
};

// Favorite stations, stored in radio.json.
class Favorites {
public:
    explicit Favorites(JsonStore &s) : store_(s) {}
    void load();
    bool save();
    bool contains(const std::string &uuid) const;
    void toggle(const Station &s);
    const std::vector<Station> &list() const { return list_; }

private:
    JsonStore &store_;
    std::vector<Station> list_;
};

}  // namespace ml::radio
