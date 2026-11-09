// Stremio Addon Protocol (client side): data model + parsers + request paths.
// Reference: https://stremio.github.io/stremio-addon-sdk/protocol.html and the SDK docs
// (docs/api/responses/*.md, MIT). Implemented from the documentation, not from SDK code.
// Video-only: music has its own model in src/music (see docs/ADDON_PROTOCOL.md).
#pragma once
#include <string>
#include <vector>

#include "core/json.hpp"

namespace ml::addon {

struct ExtraProp {
    std::string name;
    bool isRequired = false;
    std::vector<std::string> options;
};

struct CatalogDecl {
    std::string type, id, name;
    std::vector<ExtraProp> extra;
    bool requiresExtra() const;   // has an isRequired extra (e.g. search-only catalogs)
    bool supportsExtra(const std::string &name) const;
};

struct ResourceDecl {
    std::string name;                     // catalog | meta | stream | subtitles | addon_catalog
    std::vector<std::string> types;       // empty = manifest.types
    std::vector<std::string> idPrefixes;  // short form: manifest.idPrefixes apply (empty = any id)
    bool shortForm = true;                // declared as a plain string in manifest.resources
};

struct Manifest {
    std::string id, version, name, description, logo, background;
    std::vector<std::string> types;
    std::vector<std::string> idPrefixes;
    std::vector<ResourceDecl> resources;
    std::vector<CatalogDecl> catalogs;
    bool adult = false, p2p = false, configurable = false, configurationRequired = false;

    bool hasResource(const std::string &resource) const;
    // Whether a request for (resource, type, id) should go to this addon (types + idPrefixes rules).
    bool supports(const std::string &resource, const std::string &type, const std::string &id) const;
};

struct MetaPreview {
    std::string id, type, name, poster, posterShape, description, releaseInfo, imdbRating;
    std::vector<std::string> genres;
};

struct Video {   // episode / video inside a meta
    std::string id, title, released, thumbnail, overview;
    int season = 0, episode = 0;
};

struct MetaDetail : MetaPreview {
    std::string background, logo, runtime, language, country, website, released;
    std::vector<std::string> cast, directors;
    std::vector<Video> videos;
    std::string defaultVideoId;
    bool isLive = false;
    std::vector<int> seasons() const;   // sorted distinct season numbers (>0 first, specials 0 last)
};

struct Subtitle {
    std::string id, url, lang, label;
};

enum class StreamKind { Url, YouTube, Torrent, External, Unsupported };

struct Stream {
    StreamKind kind = StreamKind::Unsupported;
    std::string url;           // Url / External
    std::string ytId;
    std::string infoHash;      // parsed for completeness; Moonlight has no torrent engine
    int fileIdx = -1;
    std::string name, description;
    std::vector<Subtitle> subtitles;
    bool notWebReady = false;
    std::string bingeGroup;
    std::vector<std::pair<std::string, std::string>> requestHeaders;   // behaviorHints.proxyHeaders.request
    bool playableHere() const { return kind == StreamKind::Url; }
};

struct ParseResult {
    std::string error;   // "" = ok
    bool ok() const { return error.empty(); }
};

ParseResult parseManifest(const Json &j, Manifest &out);
ParseResult parseCatalog(const Json &j, std::vector<MetaPreview> &out);
ParseResult parseMeta(const Json &j, MetaDetail &out);
ParseResult parseStreams(const Json &j, std::vector<Stream> &out);
ParseResult parseSubtitles(const Json &j, std::vector<Subtitle> &out);

// Base URL = manifest URL without the trailing "/manifest.json".
std::string baseFromManifestUrl(const std::string &manifestUrl);
// "/{resource}/{type}/{id}.json" or "/{resource}/{type}/{id}/{extra}.json"; all segments percent-encoded.
std::string resourcePath(const std::string &resource, const std::string &type, const std::string &id,
                         const std::vector<std::pair<std::string, std::string>> &extra = {});

}  // namespace ml::addon
