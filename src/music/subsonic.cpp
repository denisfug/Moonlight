#include "music/subsonic.hpp"

#include "core/md5.hpp"

namespace ml::subsonic {

namespace {
constexpr const char *kApiVersion = "1.16.1";
constexpr const char *kClientName = "MoonlightHB";

std::string trimSlash(std::string u)
{
    while (!u.empty() && (u.back() == '/' || u.back() == ' ')) u.pop_back();
    return u;
}
}  // namespace

Config makeConfig(const std::string &url, const std::string &user, const std::string &password, uint64_t entropy)
{
    Config c;
    c.url = trimSlash(url);
    c.user = user;
    c.salt = md5Hex(std::to_string(entropy) + user).substr(0, 12);
    c.token = md5Hex(password + c.salt);
    return c;
}

Config loadConfig(JsonStore &s)
{
    Json j = s.load("subsonic.json");
    Config c;
    c.url = j["url"].asString();
    c.user = j["user"].asString();
    c.salt = j["salt"].asString();
    c.token = j["token"].asString();
    return c;
}

bool saveConfig(JsonStore &s, const Config &c)
{
    Json j = Json::object();
    j.set("url", Json::string(c.url));
    j.set("user", Json::string(c.user));
    j.set("salt", Json::string(c.salt));
    j.set("token", Json::string(c.token));
    return s.save("subsonic.json", j);
}

std::vector<Song> parseSongs(const Json &arr)
{
    std::vector<Song> v;
    for (size_t i = 0; i < arr.size(); i++) {
        const Json &x = arr[i];
        if (x["isDir"].asBool(false) || x["isVideo"].asBool(false)) continue;
        Song s;
        s.id = x["id"].isNumber() ? std::to_string(x["id"].asInt()) : x["id"].asString();
        s.title = x["title"].asString();
        s.artist = x["artist"].asString();
        s.album = x["album"].asString();
        s.coverArt = x["coverArt"].asString();
        s.suffix = x["suffix"].asString();
        s.durationSec = int(x["duration"].asInt(0));
        s.track = int(x["track"].asInt(0));
        if (!s.id.empty() && !s.title.empty()) v.push_back(std::move(s));
    }
    return v;
}

std::string Client::endpoint(const std::string &method, const std::string &extraQuery) const
{
    std::string q = cfg_.url + "/rest/" + method + ".view?u=" + percentEncode(cfg_.user) + "&t=" + cfg_.token +
                    "&s=" + cfg_.salt + "&v=" + kApiVersion + "&c=" + kClientName + "&f=json";
    if (!extraQuery.empty()) q += "&" + extraQuery;
    return q;
}

std::string Client::parse(const std::string &body, Json &payload)
{
    JsonError e;
    Json j = parseJson(body, &e, JsonLimits{8 * 1024 * 1024, 32});
    if (!e.ok()) return "resposta invalida do servidor (nao e Subsonic?)";
    const Json &r = j["subsonic-response"];
    if (!r.isObject()) return "resposta invalida do servidor (nao e Subsonic?)";
    if (r["status"].asString() != "ok") {
        int code = int(r["error"]["code"].asInt(0));
        switch (code) {
        case 40: return "usuario ou senha errados";
        case 41: return "o servidor nao aceita login por token para esse usuario (LDAP)";
        case 50: return "usuario sem permissao";
        default: return "servidor recusou (" + std::to_string(code) + "): " + r["error"]["message"].asString();
        }
    }
    payload = r;
    return "";
}

std::string Client::call(const std::string &method, const std::string &extraQuery, Json &payload)
{
    if (!cfg_.configured()) return "servidor de musica nao configurado";
    if (!http_) return "rede indisponivel nesta plataforma";
    Url u = parseUrl(cfg_.url);
    // Home servers usually run plain http on the LAN; remote servers must be https.
    std::string why = checkUrlPolicy(u, UrlPolicy{});
    if (!why.empty()) return why;
    if (u.scheme == "https" && !http_->supportsHttps()) return "HTTPS indisponivel";
    HttpRequest req;
    req.url = endpoint(method, extraQuery);
    req.timeoutMs = 10000;
    req.maxBytes = 8 * 1024 * 1024;
    HttpResponse r = http_->get(req);
    if (!r.error.empty()) return "sem conexao com o servidor (" + r.error + ")";
    if (r.status != 200) return "servidor respondeu HTTP " + std::to_string(r.status);
    return parse(r.body, payload);
}

std::string Client::ping()
{
    Json p;
    return call("ping", "", p);
}

std::string Client::playlists(std::vector<Playlist> &out)
{
    out.clear();
    Json p;
    std::string e = call("getPlaylists", "", p);
    if (!e.empty()) return e;
    const Json &arr = p["playlists"]["playlist"];
    for (size_t i = 0; i < arr.size(); i++) {
        Playlist pl;
        pl.id = arr[i]["id"].isNumber() ? std::to_string(arr[i]["id"].asInt()) : arr[i]["id"].asString();
        pl.name = arr[i]["name"].asString();
        pl.songCount = int(arr[i]["songCount"].asInt(0));
        if (!pl.id.empty()) out.push_back(pl);
    }
    return "";
}

std::string Client::playlist(const std::string &id, std::vector<Song> &out)
{
    Json p;
    std::string e = call("getPlaylist", "id=" + percentEncode(id), p);
    out = e.empty() ? parseSongs(p["playlist"]["entry"]) : std::vector<Song>{};
    return e;
}

std::string Client::randomSongs(int size, std::vector<Song> &out)
{
    Json p;
    std::string e = call("getRandomSongs", "size=" + std::to_string(size), p);
    out = e.empty() ? parseSongs(p["randomSongs"]["song"]) : std::vector<Song>{};
    return e;
}

std::string Client::search(const std::string &query, std::vector<Song> &out)
{
    Json p;
    std::string e = call("search3", "query=" + percentEncode(query) + "&songCount=100&artistCount=0&albumCount=0", p);
    out = e.empty() ? parseSongs(p["searchResult3"]["song"]) : std::vector<Song>{};
    return e;
}

std::string Client::streamUrl(const std::string &songId, int maxKbps) const
{
    return endpoint("stream", "id=" + percentEncode(songId) + "&format=mp3&maxBitRate=" + std::to_string(maxKbps));
}

}  // namespace ml::subsonic
