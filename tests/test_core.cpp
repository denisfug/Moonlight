// JSON, URL/policy, M3U, library, storage.
#include "core/json.hpp"
#include "core/net.hpp"
#include "library/library.hpp"
#include "live/m3u.hpp"
#include "platform/host/host_platform.hpp"
#include "storage/store.hpp"
#include "test.hpp"

using namespace ml;

TEST(json_parse_basic)
{
    JsonError e;
    Json j = parseJson(R"({"a":1,"b":[true,false,null],"c":"x\u00e7\ud83d\ude00\n","d":-2.5e2,"e":{}})", &e);
    CHECK(e.ok());
    CHECK_EQ(j["a"].asInt(), 1);
    CHECK_EQ(j["b"].size(), size_t(3));
    CHECK(j["b"][0].asBool());
    CHECK(j["b"][2].isNull());
    CHECK_EQ(j["c"].asString(), std::string("x\xC3\xA7\xF0\x9F\x98\x80\n"));
    CHECK_EQ(j["d"].asNumber(), -250.0);
    CHECK(j["e"].isObject());
    CHECK(j["missing"]["deep"][3].isNull());   // safe chained access
}

TEST(json_rejects_invalid)
{
    const char *bad[] = {"", "{", "[1,]", "{\"a\" 1}", "01", "\"\\x\"", "tru", "\"a\nb\"", "[1] 2", "{\"a\":1,}"};
    for (const char *b : bad) {
        JsonError e;
        Json j = parseJson(b, &e);
        CHECK(!e.ok());
        CHECK(j.isNull());
    }
}

TEST(json_limits)
{
    std::string deep(100, '[');
    deep += std::string(100, ']');
    JsonError e;
    parseJson(deep, &e);
    CHECK(!e.ok());
    parseJson("[1,2,3]", &e, JsonLimits{3, 64});
    CHECK(!e.ok());   // larger than maxBytes
}

TEST(json_roundtrip)
{
    Json o = Json::object();
    o.set("n", Json::string("a\"b\\c\x01"));
    o.set("v", Json::number(3));
    o.set("f", Json::number(0.5));
    Json arr = Json::array();
    arr.push(Json::boolean(true));
    o.set("a", arr);
    o.set("v", Json::number(4));   // replace keeps one key
    JsonError e;
    Json back = parseJson(o.dump(), &e);
    CHECK(e.ok());
    CHECK_EQ(back["n"].asString(), std::string("a\"b\\c\x01"));
    CHECK_EQ(back["v"].asInt(), 4);
    CHECK_EQ(back.keys().size(), size_t(4));
    CHECK_EQ(back["f"].asNumber(), 0.5);
}

TEST(url_parse_and_policy)
{
    Url u = parseUrl("HTTPS://Example.ORG/path/manifest.json?x=1#frag");
    CHECK(u.valid);
    CHECK_EQ(u.host, std::string("example.org"));
    CHECK_EQ(u.port, 443);
    CHECK_EQ(u.path, std::string("/path/manifest.json"));
    CHECK_EQ(u.query, std::string("x=1"));
    CHECK(parseUrl("http://192.168.15.32:8080/a").valid);
    CHECK(!parseUrl("ftp://a/b").valid);
    CHECK(!parseUrl("http://user:pass@host/").valid);
    CHECK(!parseUrl("http://host/a b").valid);
    CHECK(!parseUrl("javascript:alert(1)").valid);
    CHECK(!parseUrl("http://:80/").valid);
    CHECK(!parseUrl("http://host:99999/").valid);
    CHECK(isLocalHost("192.168.15.32"));
    CHECK(isLocalHost("127.0.0.1"));
    CHECK(!isLocalHost("8.8.8.8"));
    CHECK(!isLocalHost("192.168.1.300"));
    CHECK(checkUrlPolicy(parseUrl("https://a.org/x"), {}).empty());
    CHECK(checkUrlPolicy(parseUrl("http://10.0.0.2/x"), {}).empty());
    CHECK(!checkUrlPolicy(parseUrl("http://a.org/x"), {}).empty());
    CHECK(checkUrlPolicy(parseUrl("http://a.org/x"), UrlPolicy{true}).empty());
}

TEST(url_encoding_and_redaction)
{
    CHECK_EQ(percentEncode("game of thrones&x=1"), std::string("game%20of%20thrones%26x%3D1"));
    CHECK_EQ(percentDecode("a%20b%3a"), std::string("a b:"));
    std::string r = redactUrl("https://addon.example/eyJhcGlLZXkiOiJzZWNyZXQtdG9rZW4ifQ/manifest.json?token=abc");
    CHECK(r.find("secret") == std::string::npos);
    CHECK(r.find("eyJ") == std::string::npos);
    CHECK(r.find("abc") == std::string::npos);
    CHECK(r.find("/manifest.json") != std::string::npos);
}

TEST(m3u_parse)
{
    host::HostFileSystem fs("");
    std::string text;
    CHECK(fs.readFile(t::fixtures + "/channels.m3u", text, 1 << 20));
    M3uResult r = parseM3u(text);
    CHECK(r.error.empty());
    CHECK_EQ(r.channels.size(), size_t(4));
    CHECK_EQ(r.skipped, 1);
    CHECK_EQ(r.channels[0].name, std::string("Moonlight News"));
    CHECK_EQ(r.channels[0].tvgId, std::string("ml.news"));
    CHECK_EQ(r.channels[0].group, std::string("Noticias"));
    CHECK_EQ(r.channels[1].name, std::string("Canal Sem Nome"));     // falls back to tvg-name
    CHECK_EQ(r.channels[1].group, std::string("Musica, Clipes"));   // comma inside quotes
    CHECK(r.channels[2].drmProtected);
    CHECK_EQ(r.channels[3].group, std::string("Sem categoria"));
    CHECK_EQ(r.groups.size(), size_t(3));
    CHECK(!parseM3u("hello").error.empty());
}

TEST(library_progress_and_favorites)
{
    host::HostFileSystem fs(t::scratch);
    fs.makeDirs("/lib");
    JsonStore store(fs, "/lib");
    {
        Library lib(store);
        lib.load();
        LibraryItem a{"mls001", "series", "Cidade Submersa", "", "mls001_s1e2", 1, 2, 30000, 100000, 0};
        lib.updateProgress(a, 10);
        LibraryItem b{"mlm001", "movie", "O Farol de Vidro", "", "mlm001", 0, 0, 95000, 100000, 0};
        lib.updateProgress(b, 20);   // 95% = finished: not in continue watching
        lib.toggleFavorite(b, 21);
        CHECK(lib.isFavorite("mlm001"));
        CHECK_EQ(lib.continueWatching().size(), size_t(1));
    }
    Library again(store);   // persisted?
    again.load();
    CHECK(again.isFavorite("mlm001"));
    CHECK_EQ(again.history().size(), size_t(2));
    const LibraryItem *p = again.progressFor("mls001");
    CHECK(p && p->episode == 2 && p->positionMs == 30000);
    LibraryItem fav;
    fav.id = "mlm001";
    again.toggleFavorite(fav, 30);
    CHECK(!again.isFavorite("mlm001"));
}

TEST(store_survives_corrupt_file)
{
    host::HostFileSystem fs(t::scratch);
    fs.makeDirs("/bad");
    fs.writeFile("/bad/library.json", "{not json");
    JsonStore store(fs, "/bad");
    std::string warn;
    Json j = store.load("library.json", &warn);
    CHECK(j.isObject());
    CHECK(!warn.empty());
    CHECK(fs.exists("/bad/library.json.corrupt"));
}
