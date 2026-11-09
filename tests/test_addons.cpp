// Stremio addon protocol + Source Manager, against fixture JSON (in-memory) and a real local
// HTTP server (python3 -m http.server) when ML_ADDON_URL is set by the test script.
#include <cstdlib>

#include "addons/source_manager.hpp"
#include "platform/host/host_platform.hpp"
#include "test.hpp"

using namespace ml;
using namespace ml::addon;

namespace {

// Serves tests/fixtures/addon/<path> for any http://fixture.local/... URL; counts requests.
class FixtureHttp : public HttpClient {
public:
    std::vector<std::string> requested;
    HttpResponse get(const HttpRequest &req) override
    {
        requested.push_back(req.url);
        HttpResponse r;
        Url u = parseUrl(req.url);
        host::HostFileSystem fs("");
        std::string path = t::fixtures + "/addon" + percentDecode(u.path);
        std::string body;
        if (!fs.readFile(path, body, req.maxBytes + 1)) { r.status = 404; return r; }
        if (body.size() > req.maxBytes) { r.error = "resposta maior que o limite"; return r; }
        r.status = 200;
        r.body = body;
        return r;
    }
    bool supportsHttps() const override { return true; }
    std::string name() const override { return "fixture"; }
};

const char *kUrl = "https://fixture.local/manifest.json";

Json fixture(const std::string &rel)
{
    host::HostFileSystem fs("");
    std::string s;
    fs.readFile(t::fixtures + "/addon/" + rel, s, 1 << 20);
    return parseJson(s);
}

}  // namespace

TEST(protocol_manifest)
{
    Manifest m;
    CHECK(parseManifest(fixture("manifest.json"), m).ok());
    CHECK_EQ(m.id, std::string("hb.moonlight.demo"));
    CHECK_EQ(m.catalogs.size(), size_t(3));
    CHECK(m.catalogs[2].requiresExtra());
    CHECK(m.catalogs[0].supportsExtra("genre"));
    CHECK(m.hasResource("subtitles"));
    CHECK(m.supports("stream", "movie", "mlm001"));
    CHECK(!m.supports("stream", "movie", "tt0068646"));   // idPrefixes
    CHECK(!m.supports("stream", "tv", "mlm001"));         // resource types
    CHECK(m.supports("meta", "series", "mls001"));
    CHECK(!m.supports("addon_catalog", "movie", "mlm001"));
    Manifest bad;
    CHECK(!parseManifest(parseJson(R"({"id":"x","name":"y"})"), bad).ok());
    CHECK(!parseManifest(parseJson(R"({"id":"x","name":"y","version":"1","resources":[]})"), bad).ok());
}

TEST(protocol_paths)
{
    CHECK_EQ(baseFromManifestUrl("https://a.b/cfg/manifest.json"), std::string("https://a.b/cfg"));
    CHECK_EQ(resourcePath("catalog", "movie", "top"), std::string("/catalog/movie/top.json"));
    CHECK_EQ(resourcePath("catalog", "movie", "top", {{"search", "game of thrones"}, {"skip", "100"}}),
             std::string("/catalog/movie/top/search=game%20of%20thrones&skip=100.json"));
    CHECK_EQ(resourcePath("stream", "series", "tt1:1:2"), std::string("/stream/series/tt1%3A1%3A2.json"));
}

TEST(protocol_responses)
{
    std::vector<MetaPreview> metas;
    CHECK(parseCatalog(fixture("catalog/movie/ml-top.json"), metas).ok());
    CHECK_EQ(metas.size(), size_t(2));   // entry without id dropped
    CHECK_EQ(metas[1].releaseInfo, std::string("2023"));   // number accepted

    MetaDetail md;
    CHECK(parseMeta(fixture("meta/series/mls001.json"), md).ok());
    CHECK_EQ(md.videos.size(), size_t(4));
    CHECK_EQ(md.videos[0].season, 0);
    CHECK_EQ(md.videos[1].id, std::string("mls001_s1e1"));   // sorted by season/episode
    std::vector<int> seasons = md.seasons();
    CHECK(seasons.size() == 3 && seasons[0] == 1 && seasons[1] == 2 && seasons[2] == 0);

    MetaDetail movie;
    CHECK(parseMeta(fixture("meta/movie/mlm001.json"), movie).ok());
    CHECK_EQ(movie.directors.size(), size_t(1));
    CHECK(movie.description.find("cora\xC3\xA7\xC3\xA3o") != std::string::npos);

    std::vector<Stream> st;
    CHECK(parseStreams(fixture("stream/movie/mlm001.json"), st).ok());
    CHECK_EQ(st.size(), size_t(5));
    CHECK(st[0].kind == StreamKind::Url && st[0].playableHere());
    CHECK_EQ(st[0].subtitles.size(), size_t(1));
    CHECK_EQ(st[0].bingeGroup, std::string("ml-1080"));
    CHECK(st[0].requestHeaders.size() == 1 && st[0].requestHeaders[0].first == "User-Agent");
    CHECK(st[1].kind == StreamKind::YouTube);
    CHECK(st[2].kind == StreamKind::Torrent && !st[2].playableHere());
    CHECK(st[3].kind == StreamKind::External);
    CHECK(st[4].kind == StreamKind::Unsupported);   // javascript: URL rejected

    std::vector<Subtitle> subs;
    CHECK(parseSubtitles(fixture("subtitles/movie/mlm001.json"), subs).ok());
    CHECK_EQ(subs.size(), size_t(1));   // entry without url dropped
    CHECK(!parseCatalog(parseJson("{}"), metas).ok());
}

TEST(sources_add_browse_persist)
{
    host::HostFileSystem fs(t::scratch);
    fs.makeDirs("/src1");
    JsonStore store(fs, "/src1");
    FixtureHttp http;
    SourceManager sm(store, &http);
    sm.load();
    CHECK(sm.sources().empty());
    CHECK(!sm.add("https://fixture.local/nope.json", false, 1).empty());        // must end in manifest.json
    CHECK(!sm.add("http://remote.example/manifest.json", false, 1).empty());   // plain http remote refused
    CHECK(sm.add(kUrl, false, 1).empty());
    CHECK(!sm.add(kUrl, false, 1).empty());   // duplicate
    CHECK_EQ(sm.sources().size(), size_t(1));

    auto cats = sm.browsableCatalogs();
    CHECK_EQ(cats.size(), size_t(2));   // search-only catalog excluded
    CHECK_EQ(sm.browsableCatalogs("series").size(), size_t(1));
    std::vector<MetaPreview> metas;
    CHECK(sm.fetchCatalog(cats[0], metas).empty());
    CHECK_EQ(metas.size(), size_t(2));
    CHECK(!sm.fetchCatalog(cats[1], metas, {{"genre", "x"}}).empty());   // not declared -> refused locally

    CHECK(sm.search("movie", "farol", metas).empty());
    CHECK_EQ(metas.size(), size_t(1));
    CHECK(http.requested.back().find("/catalog/movie/ml-") != std::string::npos);

    MetaDetail md;
    CHECK(sm.fetchMeta("movie", "mlm001", md).empty());
    CHECK(!sm.fetchMeta("movie", "tt999", md).empty());   // no source handles tt*
    std::vector<std::string> errs;
    auto streams = sm.fetchStreams("series", "mls001_s1e1", &errs);
    CHECK_EQ(streams.size(), size_t(1));
    CHECK(errs.empty());
    CHECK_EQ(sm.fetchSubtitles("movie", "mlm001").size(), size_t(1));

    CHECK(sm.setEnabled(0, false));
    CHECK(sm.browsableCatalogs().empty());
    CHECK(sm.fetchStreams("movie", "mlm001").empty());

    SourceManager again(store, &http);   // persisted with enabled=false
    again.load();
    CHECK_EQ(again.sources().size(), size_t(1));
    CHECK(!again.sources()[0].enabled);
    CHECK_EQ(again.sources()[0].manifest.catalogs.size(), size_t(3));
    CHECK(again.sources()[0].manifest.supports("stream", "movie", "mlm002"));
    CHECK(again.remove(0));
    CHECK(again.sources().empty());
}

TEST(sources_size_limit_and_bad_json)
{
    class Huge : public HttpClient {
    public:
        std::string body;
        HttpResponse get(const HttpRequest &req) override
        {
            HttpResponse r;
            if (body.size() > req.maxBytes) { r.error = "resposta maior que o limite"; return r; }
            r.status = 200;
            r.body = body;
            return r;
        }
        bool supportsHttps() const override { return true; }
        std::string name() const override { return "huge"; }
    } http;
    host::HostFileSystem fs(t::scratch);
    fs.makeDirs("/src2");
    JsonStore store(fs, "/src2");
    SourceManager sm(store, &http);
    http.body = std::string(600 * 1024, ' ');
    CHECK(sm.add(kUrl, false, 1).find("limite") != std::string::npos);
    http.body = "<html>not json</html>";
    CHECK(sm.add(kUrl, false, 1).find("JSON") != std::string::npos);
    CHECK(sm.sources().empty());
}

TEST(sources_no_network_platform)
{
    host::HostFileSystem fs(t::scratch);
    fs.makeDirs("/src3");
    JsonStore store(fs, "/src3");
    SourceManager sm(store, nullptr);
    CHECK(sm.add(kUrl, false, 1).find("rede") != std::string::npos);
}

TEST(sources_live_local_server)
{
    const char *base = std::getenv("ML_ADDON_URL");   // e.g. http://127.0.0.1:8765
    if (!base) { std::printf("(skipped: ML_ADDON_URL not set) "); return; }
    host::HostFileSystem fs(t::scratch);
    fs.makeDirs("/src4");
    JsonStore store(fs, "/src4");
    host::HostHttp http;
    SourceManager sm(store, &http);
    std::string url = std::string(base) + "/manifest.json";
    std::string e = sm.add(url, false, 1);
    CHECK(e.empty());
    if (!e.empty()) { std::printf("[%s] ", e.c_str()); return; }
    std::vector<MetaPreview> metas;
    CHECK(sm.fetchCatalog(sm.browsableCatalogs("movie")[0], metas).empty());
    CHECK_EQ(metas.size(), size_t(2));
    CHECK(sm.search("movie", "farol", metas).empty());
    MetaDetail md;
    CHECK(sm.fetchMeta("series", "mls001", md).empty());
    CHECK_EQ(md.videos.size(), size_t(4));
    CHECK_EQ(sm.fetchStreams("movie", "mlm001").size(), size_t(5));
}
