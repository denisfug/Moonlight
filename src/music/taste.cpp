#include "music/taste.hpp"

#include <algorithm>

namespace ml::music {

namespace {
bool same(const Played &a, const Played &b)
{
    if (!a.url.empty() || !b.url.empty()) return a.url == b.url;
    return a.path == b.path && a.remote == b.remote;
}

Json countsToJson(const std::vector<std::pair<std::string, int>> &c)
{
    Json a = Json::array();
    for (const auto &p : c) {
        Json o = Json::object();
        o.set("name", Json::string(p.first));
        o.set("n", Json::number(p.second));
        a.push(std::move(o));
    }
    return a;
}

void countsFromJson(const Json &a, std::vector<std::pair<std::string, int>> &c, size_t max)
{
    c.clear();
    for (size_t i = 0; i < a.size() && c.size() < max; i++) {
        std::string name = a[i]["name"].asString();
        int n = int(a[i]["n"].asInt(0));
        if (!name.empty() && n > 0) c.emplace_back(name, n);
    }
}
}  // namespace

void Taste::bump(Counts &c, const std::string &name)
{
    if (name.empty()) return;
    int n = 1;
    for (size_t i = 0; i < c.size(); i++)
        if (c[i].first == name) { n = c[i].second + 1; c.erase(c.begin() + long(i)); break; }
    c.insert(c.begin(), {name, n});
    if (c.size() > kMaxCounted) c.pop_back();   // the one played longest ago
}

// Highest count; between equals, the one played most recently (they are stored newest first).
std::string Taste::top(const Counts &c)
{
    std::string best;
    int n = 0;
    for (const auto &p : c)
        if (p.second > n) { n = p.second; best = p.first; }
    return best;
}

void Taste::notePlay(const Track &t)
{
    if (t.demo) return;
    Played p;
    p.title = t.title;
    p.artist = t.artist;
    p.album = t.album;
    p.genre = t.genre;
    p.art = t.art;
    p.source = t.source;
    p.url = t.url;
    p.path = t.url.empty() ? t.path : std::string();
    p.remote = t.url.empty() && t.remote;
    p.durationMs = t.durationMs;
    recent_.erase(std::remove_if(recent_.begin(), recent_.end(), [&](const Played &o) { return same(o, p); }), recent_.end());
    recent_.insert(recent_.begin(), std::move(p));
    if (recent_.size() > kMaxRecent) recent_.resize(kMaxRecent);
    bump(genres_, t.genre);
    bump(artists_, t.artist);
}

Json Taste::save() const
{
    Json j = Json::object();
    Json r = Json::array();
    for (const auto &p : recent_) {
        Json o = Json::object();
        o.set("title", Json::string(p.title));
        o.set("artist", Json::string(p.artist));
        o.set("album", Json::string(p.album));
        o.set("genre", Json::string(p.genre));
        o.set("art", Json::string(p.art));
        o.set("source", Json::string(p.source));
        o.set("url", Json::string(p.url));
        o.set("path", Json::string(p.path));
        o.set("remote", Json::boolean(p.remote));
        o.set("ms", Json::number(double(p.durationMs)));
        r.push(std::move(o));
    }
    j.set("recent", std::move(r));
    j.set("genres", countsToJson(genres_));
    j.set("artists", countsToJson(artists_));
    return j;
}

void Taste::load(const Json &j)
{
    clear();
    const Json &r = j["recent"];
    for (size_t i = 0; i < r.size() && recent_.size() < kMaxRecent; i++) {
        Played p;
        p.title = r[i]["title"].asString();
        p.artist = r[i]["artist"].asString();
        p.album = r[i]["album"].asString();
        p.genre = r[i]["genre"].asString();
        p.art = r[i]["art"].asString();
        p.source = r[i]["source"].asString();
        p.url = r[i]["url"].asString();
        p.path = r[i]["path"].asString();
        p.remote = r[i]["remote"].asBool(false);
        p.durationMs = r[i]["ms"].asInt(0);
        if (p.title.empty() || (p.url.empty() && p.path.empty())) continue;
        recent_.push_back(std::move(p));
    }
    countsFromJson(j["genres"], genres_, kMaxCounted);
    countsFromJson(j["artists"], artists_, kMaxCounted);
}

}  // namespace ml::music
