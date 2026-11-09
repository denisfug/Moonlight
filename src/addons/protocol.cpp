#include "addons/protocol.hpp"

#include <algorithm>

#include "core/net.hpp"

namespace ml::addon {

namespace {

std::vector<std::string> strList(const Json &j)
{
    std::vector<std::string> v;
    for (size_t i = 0; i < j.size(); i++)
        if (j[i].isString()) v.push_back(j[i].asString());
    return v;
}

bool contains(const std::vector<std::string> &v, const std::string &s)
{
    return std::find(v.begin(), v.end(), s) != v.end();
}

bool prefixed(const std::vector<std::string> &prefixes, const std::string &id)
{
    if (prefixes.empty()) return true;
    for (const auto &p : prefixes)
        if (id.compare(0, p.size(), p) == 0) return true;
    return false;
}

void readPreview(const Json &j, MetaPreview &m)
{
    m.id = j["id"].asString();
    m.type = j["type"].asString();
    m.name = j["name"].asString();
    m.poster = j["poster"].asString();
    m.posterShape = j["posterShape"].str("poster");
    m.description = j["description"].asString();
    m.releaseInfo = j["releaseInfo"].isNumber() ? std::to_string(j["releaseInfo"].asInt()) : j["releaseInfo"].asString();
    m.imdbRating = j["imdbRating"].isNumber() ? std::to_string(j["imdbRating"].asNumber()).substr(0, 3)
                                              : j["imdbRating"].asString();
    m.genres = strList(j["genres"]);
}

Subtitle readSubtitle(const Json &j)
{
    Subtitle s;
    s.id = j["id"].asString();
    s.url = j["url"].asString();
    s.lang = j["lang"].asString();
    s.label = j["label"].asString();
    return s;
}

}  // namespace

bool CatalogDecl::requiresExtra() const
{
    for (const auto &e : extra) if (e.isRequired) return true;
    return false;
}

bool CatalogDecl::supportsExtra(const std::string &n) const
{
    for (const auto &e : extra) if (e.name == n) return true;
    return false;
}

bool Manifest::hasResource(const std::string &r) const
{
    for (const auto &x : resources) if (x.name == r) return true;
    return false;
}

bool Manifest::supports(const std::string &resource, const std::string &type, const std::string &id) const
{
    for (const auto &r : resources) {
        if (r.name != resource) continue;
        const auto &ts = r.shortForm || r.types.empty() ? types : r.types;
        if (!ts.empty() && !contains(ts, type)) return false;
        const auto &ps = r.shortForm ? idPrefixes : r.idPrefixes;
        return prefixed(ps, id);
    }
    return false;
}

std::vector<int> MetaDetail::seasons() const
{
    std::vector<int> s;
    for (const auto &v : videos)
        if (std::find(s.begin(), s.end(), v.season) == s.end()) s.push_back(v.season);
    std::sort(s.begin(), s.end(), [](int a, int b) {
        if ((a == 0) != (b == 0)) return b == 0;   // specials (season 0) last
        return a < b;
    });
    return s;
}

ParseResult parseManifest(const Json &j, Manifest &m)
{
    if (!j.isObject()) return {"manifest nao e um objeto JSON"};
    m = Manifest();
    m.id = j["id"].asString();
    m.version = j["version"].asString();
    m.name = j["name"].asString();
    m.description = j["description"].asString();
    m.logo = j["logo"].asString();
    m.background = j["background"].asString();
    m.types = strList(j["types"]);
    m.idPrefixes = strList(j["idPrefixes"]);
    if (m.id.empty() || m.name.empty() || m.version.empty()) return {"manifest sem id, name ou version"};
    if (!j["resources"].isArray()) return {"manifest sem resources"};
    for (size_t i = 0; i < j["resources"].size(); i++) {
        const Json &r = j["resources"][i];
        ResourceDecl d;
        if (r.isString()) {
            d.name = r.asString();
        } else if (r.isObject()) {
            d.name = r["name"].asString();
            d.shortForm = false;
            d.types = strList(r["types"]);
            d.idPrefixes = strList(r["idPrefixes"]);
        }
        if (!d.name.empty()) m.resources.push_back(std::move(d));
    }
    if (!j["catalogs"].isArray()) return {"manifest sem catalogs (deve ser [] quando nao ha catalogos)"};
    for (size_t i = 0; i < j["catalogs"].size(); i++) {
        const Json &c = j["catalogs"][i];
        CatalogDecl d;
        d.type = c["type"].asString();
        d.id = c["id"].asString();
        d.name = c["name"].str(d.id);
        const Json &ex = c["extra"];
        for (size_t k = 0; k < ex.size(); k++) {
            ExtraProp p;
            p.name = ex[k]["name"].asString();
            p.isRequired = ex[k]["isRequired"].asBool();
            p.options = strList(ex[k]["options"]);
            if (!p.name.empty()) d.extra.push_back(std::move(p));
        }
        // legacy shorthand: extraSupported / extraRequired
        for (const auto &n : strList(c["extraSupported"]))
            if (!d.supportsExtra(n)) d.extra.push_back({n, false, {}});
        for (const auto &n : strList(c["extraRequired"]))
            for (auto &p : d.extra) if (p.name == n) p.isRequired = true;
        if (!d.type.empty() && !d.id.empty()) m.catalogs.push_back(std::move(d));
    }
    const Json &bh = j["behaviorHints"];
    m.adult = bh["adult"].asBool();
    m.p2p = bh["p2p"].asBool();
    m.configurable = bh["configurable"].asBool();
    m.configurationRequired = bh["configurationRequired"].asBool();
    return {};
}

ParseResult parseCatalog(const Json &j, std::vector<MetaPreview> &out)
{
    out.clear();
    if (!j["metas"].isArray()) return {"resposta de catalogo sem 'metas'"};
    for (size_t i = 0; i < j["metas"].size(); i++) {
        MetaPreview m;
        readPreview(j["metas"][i], m);
        if (!m.id.empty() && !m.name.empty()) out.push_back(std::move(m));
    }
    return {};
}

ParseResult parseMeta(const Json &j, MetaDetail &m)
{
    const Json &x = j["meta"];
    if (!x.isObject()) return {"resposta de meta sem 'meta'"};
    m = MetaDetail();
    readPreview(x, m);
    if (m.id.empty() || m.name.empty()) return {"meta sem id ou name"};
    m.background = x["background"].asString();
    m.logo = x["logo"].asString();
    m.runtime = x["runtime"].asString();
    m.language = x["language"].asString();
    m.country = x["country"].asString();
    m.website = x["website"].asString();
    m.released = x["released"].asString();
    m.cast = strList(x["cast"]);
    m.directors = strList(x["director"].isArray() ? x["director"] : x["directors"]);
    m.defaultVideoId = x["behaviorHints"]["defaultVideoId"].asString();
    m.isLive = x["behaviorHints"]["isLive"].asBool();
    for (size_t i = 0; i < x["videos"].size(); i++) {
        const Json &v = x["videos"][i];
        Video vd;
        vd.id = v["id"].asString();
        vd.title = v["title"].str(v["name"].asString());
        vd.released = v["released"].asString();
        vd.thumbnail = v["thumbnail"].asString();
        vd.overview = v["overview"].str(v["description"].asString());
        vd.season = static_cast<int>(v["season"].asInt(0));
        vd.episode = static_cast<int>(v["episode"].asInt(v["number"].asInt(0)));
        if (!vd.id.empty()) m.videos.push_back(std::move(vd));
    }
    std::stable_sort(m.videos.begin(), m.videos.end(), [](const Video &a, const Video &b) {
        return a.season != b.season ? a.season < b.season : a.episode < b.episode;
    });
    return {};
}

ParseResult parseStreams(const Json &j, std::vector<Stream> &out)
{
    out.clear();
    if (!j["streams"].isArray()) return {"resposta de stream sem 'streams'"};
    for (size_t i = 0; i < j["streams"].size(); i++) {
        const Json &x = j["streams"][i];
        Stream s;
        if (x["url"].isString()) { s.kind = StreamKind::Url; s.url = x["url"].asString(); }
        else if (x["ytId"].isString()) { s.kind = StreamKind::YouTube; s.ytId = x["ytId"].asString(); }
        else if (x["infoHash"].isString()) {
            s.kind = StreamKind::Torrent;
            s.infoHash = x["infoHash"].asString();
            s.fileIdx = static_cast<int>(x["fileIdx"].asInt(-1));
        } else if (x["externalUrl"].isString()) { s.kind = StreamKind::External; s.url = x["externalUrl"].asString(); }
        else s.kind = StreamKind::Unsupported;
        if (s.kind == StreamKind::Url && !parseUrl(s.url).valid) s.kind = StreamKind::Unsupported;
        s.name = x["name"].asString();
        s.description = x["description"].str(x["title"].asString());
        for (size_t k = 0; k < x["subtitles"].size(); k++) s.subtitles.push_back(readSubtitle(x["subtitles"][k]));
        const Json &bh = x["behaviorHints"];
        s.notWebReady = bh["notWebReady"].asBool();
        s.bingeGroup = bh["bingeGroup"].asString();
        const Json &rh = bh["proxyHeaders"]["request"];
        for (size_t k = 0; k < rh.size(); k++)
            if (rh.items()[k].isString()) s.requestHeaders.push_back({rh.keys()[k], rh.items()[k].asString()});
        out.push_back(std::move(s));
    }
    return {};
}

ParseResult parseSubtitles(const Json &j, std::vector<Subtitle> &out)
{
    out.clear();
    if (!j["subtitles"].isArray()) return {"resposta de legendas sem 'subtitles'"};
    for (size_t i = 0; i < j["subtitles"].size(); i++) {
        Subtitle s = readSubtitle(j["subtitles"][i]);
        if (!s.url.empty()) out.push_back(std::move(s));
    }
    return {};
}

std::string baseFromManifestUrl(const std::string &u)
{
    const std::string suffix = "/manifest.json";
    if (u.size() >= suffix.size() && u.compare(u.size() - suffix.size(), suffix.size(), suffix) == 0)
        return u.substr(0, u.size() - suffix.size());
    return u;
}

std::string resourcePath(const std::string &resource, const std::string &type, const std::string &id,
                         const std::vector<std::pair<std::string, std::string>> &extra)
{
    std::string p = "/" + percentEncode(resource) + "/" + percentEncode(type) + "/" + percentEncode(id);
    if (!extra.empty()) {
        std::string e;
        for (const auto &kv : extra) {
            if (!e.empty()) e += "&";
            e += percentEncode(kv.first) + "=" + percentEncode(kv.second);
        }
        p += "/" + e;
    }
    return p + ".json";
}

}  // namespace ml::addon
