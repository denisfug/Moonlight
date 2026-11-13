#include "radio/radio.hpp"

#include <algorithm>
#include <cctype>

namespace ml::radio {

namespace {

std::string str(const Json &j)
{
    if (j.isString()) return j.asString();
    if (j.isNumber()) return std::to_string(j.asInt());
    return "";
}

int num(const Json &j)
{
    if (j.isNumber()) return int(j.asInt());
    if (j.isString()) return std::atoi(j.asString().c_str());
    if (j.isBool()) return j.asBool() ? 1 : 0;
    return 0;
}

std::string upper(std::string s)
{
    for (auto &c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string &s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

Json toJson(const Station &s)
{
    Json o = Json::object();
    o.set("stationuuid", Json::string(s.uuid));
    o.set("name", Json::string(s.name));
    o.set("url_resolved", Json::string(s.url));
    o.set("homepage", Json::string(s.homepage));
    o.set("favicon", Json::string(s.favicon));
    o.set("tags", Json::string(s.tags));
    o.set("countrycode", Json::string(s.countryCode));
    o.set("language", Json::string(s.language));
    o.set("codec", Json::string(s.codec));
    o.set("bitrate", Json::number(s.bitrate));
    o.set("votes", Json::number(s.votes));
    o.set("hls", Json::number(s.hls ? 1 : 0));
    o.set("lastcheckok", Json::number(s.lastCheckOk ? 1 : 0));
    return o;
}

}  // namespace

bool Station::playable() const
{
    return !hls && lastCheckOk && upper(codec) == "MP3" && parseUrl(url).valid;
}

std::vector<Station> parseStations(const Json &arr)
{
    std::vector<Station> v;
    for (size_t i = 0; i < arr.size(); i++) {
        const Json &x = arr[i];
        Station s;
        s.uuid = str(x["stationuuid"]);
        s.name = trim(str(x["name"]));
        s.url = str(x["url_resolved"]);
        if (s.url.empty()) s.url = str(x["url"]);
        s.url = trim(s.url);
        s.homepage = str(x["homepage"]);
        s.favicon = str(x["favicon"]);
        s.tags = str(x["tags"]);
        s.countryCode = str(x["countrycode"]);
        s.language = str(x["language"]);
        s.codec = str(x["codec"]);
        s.bitrate = num(x["bitrate"]);
        s.votes = num(x["votes"]);
        s.hls = num(x["hls"]) != 0;
        s.lastCheckOk = x.has("lastcheckok") ? num(x["lastcheckok"]) != 0 : true;
        if (!s.uuid.empty() && !s.name.empty()) v.push_back(std::move(s));
    }
    return v;
}

std::string searchPath(const Query &q)
{
    std::string p = "/json/stations/search?codec=MP3&hidebroken=true";
    if (!q.name.empty()) p += "&name=" + percentEncode(q.name);
    if (!q.tag.empty()) p += "&tag=" + percentEncode(q.tag);
    if (!q.countryCode.empty()) p += "&countrycode=" + percentEncode(q.countryCode);
    p += "&order=" + percentEncode(q.order);
    if (q.order == "votes" || q.order == "clickcount") p += "&reverse=true";
    p += "&limit=" + std::to_string(std::clamp(q.limit, 1, 500));
    return p;
}

std::string RadioBrowser::search(const Query &q, std::vector<Station> &out)
{
    out.clear();
    if (!http_) return "rede ainda nao disponivel nesta plataforma";
    std::string last = "nenhum servidor do diretorio respondeu";
    for (const auto &base : servers) {
        Url u = parseUrl(base);
        if (u.scheme == "https" && !http_->supportsHttps()) { last = "HTTPS indisponivel"; continue; }
        HttpRequest req;
        req.url = base + searchPath(q);
        req.timeoutMs = 12000;
        req.maxBytes = 2 * 1024 * 1024;
        req.headers.push_back({"Accept", "application/json"});
        HttpResponse r = http_->get(req);
        if (!r.ok()) { last = r.error.empty() ? "diretorio respondeu HTTP " + std::to_string(r.status) : r.error; continue; }
        JsonError e;
        Json j = parseJson(r.body, &e, JsonLimits{req.maxBytes, 16});
        if (!e.ok() || !j.isArray()) { last = "resposta invalida do diretorio"; continue; }
        for (auto &s : parseStations(j))
            if (s.playable()) out.push_back(std::move(s));
        return "";
    }
    return last;
}

void RadioBrowser::countClick(const std::string &uuid)
{
    if (!http_ || uuid.empty() || !http_->supportsHttps()) return;
    HttpRequest req;
    req.url = servers.front() + "/json/url/" + percentEncode(uuid);
    req.timeoutMs = 5000;
    req.maxBytes = 64 * 1024;
    http_->get(req);   // result ignored: counting is a courtesy to the directory
}

void Favorites::load()
{
    Json j = store_.load("radio.json");
    list_ = parseStations(j["favorites"]);
}

bool Favorites::save()
{
    Json arr = Json::array();
    for (const auto &s : list_) arr.push(toJson(s));
    Json j = Json::object();
    j.set("version", Json::number(1));
    j.set("favorites", arr);
    return store_.save("radio.json", j);
}

bool Favorites::contains(const std::string &uuid) const
{
    return std::any_of(list_.begin(), list_.end(), [&](const Station &s) { return s.uuid == uuid; });
}

void Favorites::toggle(const Station &s)
{
    auto it = std::find_if(list_.begin(), list_.end(), [&](const Station &x) { return x.uuid == s.uuid; });
    if (it != list_.end()) list_.erase(it);
    else list_.insert(list_.begin(), s);
    save();
}

}  // namespace ml::radio
