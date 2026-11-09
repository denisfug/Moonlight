#include "addons/source_manager.hpp"

namespace ml {

using namespace addon;

static constexpr size_t kManifestMax = 512 * 1024;
static constexpr size_t kResourceMax = 4 * 1024 * 1024;
static constexpr int kTimeoutMs = 15000;

void SourceManager::load()
{
    sources_.clear();
    Json j = store_.load("sources.json");
    const Json &arr = j["sources"];
    for (size_t i = 0; i < arr.size(); i++) {
        const Json &x = arr[i];
        Source s;
        s.manifestUrl = x["manifestUrl"].asString();
        s.enabled = x["enabled"].asBool(true);
        s.allowPlainHttp = x["allowPlainHttp"].asBool(false);
        s.updatedAt = x["updatedAt"].asInt(0);
        s.lastError = x["lastError"].asString();
        if (parseManifest(x["manifest"], s.manifest).ok() && parseUrl(s.manifestUrl).valid)
            sources_.push_back(std::move(s));
    }
}

bool SourceManager::save()
{
    Json arr = Json::array();
    for (const auto &s : sources_) {
        // The cached manifest is stored as the subset Moonlight understands.
        Json m = Json::object();
        m.set("id", Json::string(s.manifest.id));
        m.set("version", Json::string(s.manifest.version));
        m.set("name", Json::string(s.manifest.name));
        m.set("description", Json::string(s.manifest.description));
        m.set("logo", Json::string(s.manifest.logo));
        Json types = Json::array(), pref = Json::array(), res = Json::array(), cats = Json::array();
        for (const auto &t : s.manifest.types) types.push(Json::string(t));
        for (const auto &p : s.manifest.idPrefixes) pref.push(Json::string(p));
        for (const auto &r : s.manifest.resources) {
            if (r.shortForm) { res.push(Json::string(r.name)); continue; }
            Json o = Json::object(), rt = Json::array(), rp = Json::array();
            for (const auto &t : r.types) rt.push(Json::string(t));
            for (const auto &p : r.idPrefixes) rp.push(Json::string(p));
            o.set("name", Json::string(r.name));
            o.set("types", rt);
            o.set("idPrefixes", rp);
            res.push(o);
        }
        for (const auto &c : s.manifest.catalogs) {
            Json o = Json::object(), ex = Json::array();
            o.set("type", Json::string(c.type));
            o.set("id", Json::string(c.id));
            o.set("name", Json::string(c.name));
            for (const auto &e : c.extra) {
                Json eo = Json::object(), op = Json::array();
                eo.set("name", Json::string(e.name));
                eo.set("isRequired", Json::boolean(e.isRequired));
                for (const auto &v : e.options) op.push(Json::string(v));
                eo.set("options", op);
                ex.push(eo);
            }
            o.set("extra", ex);
            cats.push(o);
        }
        m.set("types", types);
        m.set("idPrefixes", pref);
        m.set("resources", res);
        m.set("catalogs", cats);
        Json bh = Json::object();
        bh.set("adult", Json::boolean(s.manifest.adult));
        bh.set("p2p", Json::boolean(s.manifest.p2p));
        m.set("behaviorHints", bh);

        Json o = Json::object();
        o.set("manifestUrl", Json::string(s.manifestUrl));
        o.set("enabled", Json::boolean(s.enabled));
        o.set("allowPlainHttp", Json::boolean(s.allowPlainHttp));
        o.set("updatedAt", Json::number(static_cast<double>(s.updatedAt)));
        o.set("lastError", Json::string(s.lastError));
        o.set("manifest", m);
        arr.push(o);
    }
    Json root = Json::object();
    root.set("version", Json::number(1));
    root.set("sources", arr);
    return store_.save("sources.json", root);
}

std::string SourceManager::getJson(const std::string &url, const UrlPolicy &pol, Json &out, size_t maxBytes)
{
    Url u = parseUrl(url);
    std::string why = checkUrlPolicy(u, pol);
    if (!why.empty()) return why;
    if (!http_) return "rede ainda nao implementada nesta plataforma";
    if (u.scheme == "https" && !http_->supportsHttps()) return "HTTPS ainda nao suportado por " + http_->name();
    HttpRequest req;
    req.url = u.str();
    req.timeoutMs = kTimeoutMs;
    req.maxBytes = maxBytes;
    req.headers.push_back({"Accept", "application/json"});
    HttpResponse r = http_->get(req);
    if (!r.error.empty()) return "falha de rede: " + r.error;
    if (r.status < 200 || r.status >= 300) return "servidor respondeu HTTP " + std::to_string(r.status);
    JsonError err;
    out = parseJson(r.body, &err, JsonLimits{maxBytes, 64});
    if (!err.ok()) return "JSON invalido (" + err.message + ")";
    return "";
}

std::string SourceManager::fetchManifest(const std::string &url, const UrlPolicy &pol, Manifest &m)
{
    Json j;
    std::string e = getJson(url, pol, j, kManifestMax);
    if (!e.empty()) return e;
    ParseResult r = parseManifest(j, m);
    return r.ok() ? "" : r.error;
}

std::string SourceManager::add(const std::string &manifestUrlIn, bool allowPlainHttp, int64_t now)
{
    std::string url = manifestUrlIn;
    while (!url.empty() && (url.back() == ' ' || url.back() == '\n' || url.back() == '\r')) url.pop_back();
    if (url.size() > 2048) return "URL longa demais";
    Url u = parseUrl(url);
    if (!u.valid) return "URL invalida";
    if (u.path.size() < 14 || u.path.compare(u.path.size() - 14, 14, "/manifest.json") != 0)
        return "a URL precisa terminar em /manifest.json";
    for (const auto &s : sources_)
        if (s.manifestUrl == url) return "essa fonte ja esta na lista";
    Source s;
    s.manifestUrl = url;
    s.allowPlainHttp = allowPlainHttp;
    std::string e = fetchManifest(url, policyFor(s), s.manifest);
    if (!e.empty()) return e;
    for (const auto &o : sources_)
        if (o.manifest.id == s.manifest.id) return "ja existe uma fonte com o id " + s.manifest.id;
    s.updatedAt = now;
    sources_.push_back(std::move(s));
    save();
    return "";
}

bool SourceManager::remove(size_t i)
{
    if (i >= sources_.size()) return false;
    sources_.erase(sources_.begin() + static_cast<long>(i));
    save();
    return true;
}

bool SourceManager::setEnabled(size_t i, bool en)
{
    if (i >= sources_.size()) return false;
    sources_[i].enabled = en;
    save();
    return true;
}

std::string SourceManager::refresh(size_t i, int64_t now)
{
    if (i >= sources_.size()) return "fonte inexistente";
    Manifest m;
    std::string e = fetchManifest(sources_[i].manifestUrl, policyFor(sources_[i]), m);
    sources_[i].lastError = e;
    if (e.empty()) {
        sources_[i].manifest = std::move(m);
        sources_[i].updatedAt = now;
    }
    save();
    return e;
}

std::vector<CatalogRef> SourceManager::browsableCatalogs(const std::string &type) const
{
    std::vector<CatalogRef> v;
    for (size_t i = 0; i < sources_.size(); i++) {
        if (!sources_[i].enabled || !sources_[i].manifest.hasResource("catalog")) continue;
        for (const auto &c : sources_[i].manifest.catalogs)
            if (!c.requiresExtra() && (type.empty() || c.type == type)) v.push_back({i, &c});
    }
    return v;
}

std::string SourceManager::fetchCatalog(const CatalogRef &c, std::vector<MetaPreview> &out,
                                        const std::vector<std::pair<std::string, std::string>> &extra)
{
    out.clear();
    if (c.source >= sources_.size() || !c.decl) return "catalogo invalido";
    const Source &s = sources_[c.source];
    for (const auto &kv : extra)
        if (!c.decl->supportsExtra(kv.first)) return "catalogo nao aceita o filtro " + kv.first;
    Json j;
    std::string e = getJson(baseFromManifestUrl(s.manifestUrl) + resourcePath("catalog", c.decl->type, c.decl->id, extra),
                            policyFor(s), j, kResourceMax);
    if (!e.empty()) return e;
    ParseResult r = parseCatalog(j, out);
    return r.ok() ? "" : r.error;
}

std::string SourceManager::search(const std::string &type, const std::string &query, std::vector<MetaPreview> &out)
{
    out.clear();
    std::string lastErr = "nenhuma fonte ativa oferece busca de " + type;
    for (size_t i = 0; i < sources_.size(); i++) {
        if (!sources_[i].enabled) continue;
        for (const auto &c : sources_[i].manifest.catalogs) {
            if (c.type != type || !c.supportsExtra("search")) continue;
            std::vector<MetaPreview> part;
            std::string e = fetchCatalog({i, &c}, part, {{"search", query}});
            if (!e.empty()) { lastErr = e; continue; }
            out.insert(out.end(), part.begin(), part.end());
            lastErr.clear();
        }
    }
    return out.empty() ? lastErr : "";
}

std::string SourceManager::fetchMeta(const std::string &type, const std::string &id, MetaDetail &out)
{
    std::string lastErr = "nenhuma fonte ativa fornece detalhes para " + id;
    for (const auto &s : sources_) {
        if (!s.enabled || !s.manifest.supports("meta", type, id)) continue;
        Json j;
        std::string e = getJson(baseFromManifestUrl(s.manifestUrl) + resourcePath("meta", type, id), policyFor(s), j, kResourceMax);
        if (e.empty()) {
            ParseResult r = parseMeta(j, out);
            if (r.ok()) return "";
            e = r.error;
        }
        lastErr = s.manifest.name + ": " + e;
    }
    return lastErr;
}

std::vector<Stream> SourceManager::fetchStreams(const std::string &type, const std::string &videoId,
                                                std::vector<std::string> *errors)
{
    std::vector<Stream> all;
    for (const auto &s : sources_) {
        if (!s.enabled || !s.manifest.supports("stream", type, videoId)) continue;
        Json j;
        std::vector<Stream> part;
        std::string e = getJson(baseFromManifestUrl(s.manifestUrl) + resourcePath("stream", type, videoId), policyFor(s), j, kResourceMax);
        if (e.empty()) {
            ParseResult r = parseStreams(j, part);
            e = r.error;
        }
        if (!e.empty()) {
            if (errors) errors->push_back(s.manifest.name + ": " + e);
            continue;
        }
        for (auto &p : part) {
            if (p.name.empty()) p.name = s.manifest.name;
            all.push_back(std::move(p));
        }
    }
    return all;
}

std::vector<Subtitle> SourceManager::fetchSubtitles(const std::string &type, const std::string &id)
{
    std::vector<Subtitle> all;
    for (const auto &s : sources_) {
        if (!s.enabled || !s.manifest.supports("subtitles", type, id)) continue;
        Json j;
        std::vector<Subtitle> part;
        if (getJson(baseFromManifestUrl(s.manifestUrl) + resourcePath("subtitles", type, id), policyFor(s), j, kResourceMax).empty() &&
            parseSubtitles(j, part).ok())
            all.insert(all.end(), part.begin(), part.end());
    }
    return all;
}

}  // namespace ml
