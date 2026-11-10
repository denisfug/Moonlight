// Source Manager: the user's list of addon sources (Stremio protocol), independent of the UI.
// Network calls go through HttpClient with UrlPolicy and size/time limits; nothing an addon
// returns is ever executed — responses are parsed as data only.
#pragma once
#include <string>
#include <vector>

#include "addons/protocol.hpp"
#include "core/net.hpp"
#include "storage/store.hpp"

namespace ml {

struct Source {
    std::string manifestUrl;
    bool enabled = true;
    bool allowPlainHttp = false;   // explicit user opt-in for a remote http:// source
    addon::Manifest manifest;
    std::string lastError;
    int64_t updatedAt = 0;         // ms (platform clock at fetch time)
};

struct CatalogRef {
    size_t source = 0;
    const addon::CatalogDecl *decl = nullptr;
};

class SourceManager {
public:
    SourceManager(JsonStore &store, HttpClient *http) : store_(store), http_(http) {}

    void load();   // from sources.json
    bool save();

    const std::vector<Source> &sources() const { return sources_; }

    // Fetches and validates the manifest, then adds it. Returns "" or an error for the user.
    std::string add(const std::string &manifestUrl, bool allowPlainHttp, int64_t now);
    bool remove(size_t index);
    bool setEnabled(size_t index, bool enabled);
    std::string refresh(size_t index, int64_t now);

    // Catalogs of enabled sources that can be browsed without user input (no required extra).
    std::vector<CatalogRef> browsableCatalogs(const std::string &type = "") const;

    std::string fetchCatalog(const CatalogRef &c, std::vector<addon::MetaPreview> &out,
                             const std::vector<std::pair<std::string, std::string>> &extra = {});
    std::string search(const std::string &type, const std::string &query, std::vector<addon::MetaPreview> &out);
    std::string fetchMeta(const std::string &type, const std::string &id, addon::MetaDetail &out);
    // Aggregates streams of every enabled source that supports (type, videoId). Errors are per source.
    std::vector<addon::Stream> fetchStreams(const std::string &type, const std::string &videoId,
                                            std::vector<std::string> *errors = nullptr);
    std::vector<addon::Subtitle> fetchSubtitles(const std::string &type, const std::string &id);

    HttpClient *http() const { return http_; }
    UrlPolicy policyFor(const Source &s) const { return UrlPolicy{s.allowPlainHttp}; }

private:
    JsonStore &store_;
    HttpClient *http_;
    std::vector<Source> sources_;
    std::vector<std::pair<std::string, std::vector<addon::MetaPreview>>> catalogCache_;   // url -> metas

    std::string getJson(const std::string &url, const UrlPolicy &pol, Json &out, size_t maxBytes);
    std::string fetchManifest(const std::string &url, const UrlPolicy &pol, addon::Manifest &m);
};

}  // namespace ml
