// Internet radio: directory parsing, query building, favorites, and live MP3 stream playback.
#include <cstdlib>
#include <cstring>

#include "platform/host/host_platform.hpp"
#include "player/player.hpp"
#include "radio/radio.hpp"
#include "test.hpp"

using namespace ml;
using namespace ml::radio;

static std::string readFixture(const std::string &rel)
{
    host::HostFileSystem fs("");
    std::string s;
    fs.readFile(t::fixtures + "/" + rel, s, 1 << 24);
    return s;
}

TEST(radio_parse_and_filter)
{
    auto all = parseStations(parseJson(readFixture("radio/json/stations/search")));
    CHECK_EQ(all.size(), size_t(5));   // entry without uuid dropped
    CHECK_EQ(all[0].name, std::string("Radio Lua Cheia FM"));   // trimmed
    CHECK_EQ(all[0].url, std::string("http://127.0.0.1:8765/tone.mp3"));   // falls back to "url"
    CHECK_EQ(all[0].votes, 512);   // string number accepted
    CHECK_EQ(all[4].bitrate, 96);
    CHECK(all[0].playable());
    CHECK(!all[1].playable());   // AAC
    CHECK(!all[2].playable());   // HLS
    CHECK(!all[3].playable());   // last check failed
    CHECK(all[4].playable());    // lower-case codec, bool flags
}

TEST(radio_query_path)
{
    Query q;
    q.tag = "sertanejo";
    q.countryCode = "BR";
    std::string p = searchPath(q);
    CHECK(p.find("/json/stations/search?codec=MP3&hidebroken=true") == 0);
    CHECK(p.find("&tag=sertanejo") != std::string::npos);
    CHECK(p.find("&countrycode=BR") != std::string::npos);
    CHECK(p.find("&order=votes&reverse=true") != std::string::npos);
    Query n;
    n.name = "jovem pan & cia";
    n.order = "name";
    n.limit = 9999;
    std::string p2 = searchPath(n);
    CHECK(p2.find("&name=jovem%20pan%20%26%20cia") != std::string::npos);
    CHECK(p2.find("reverse") == std::string::npos);
    CHECK(p2.find("&limit=500") != std::string::npos);
}

TEST(radio_favorites_persist)
{
    host::HostFileSystem fs(t::scratch);
    fs.makeDirs("/radio");
    JsonStore store(fs, "/radio");
    auto st = parseStations(parseJson(readFixture("radio/json/stations/search")));
    {
        Favorites f(store);
        f.load();
        f.toggle(st[0]);
        f.toggle(st[4]);
        f.toggle(st[0]);   // removed again
    }
    Favorites again(store);
    again.load();
    CHECK_EQ(again.list().size(), size_t(1));
    CHECK(again.contains("aaaa-5"));
    CHECK(again.list()[0].playable());
}

namespace {
class NoTlsHttp : public HttpClient {   // a client without TLS: the real directory is refused cleanly
public:
    HttpResponse get(const HttpRequest &) override { HttpResponse r; r.error = "sem rede"; return r; }
    bool supportsHttps() const override { return false; }
    std::string name() const override { return "sem-tls"; }
};
}  // namespace

TEST(radio_directory_without_https)
{
    NoTlsHttp http;
    RadioBrowser rb(&http);
    std::vector<Station> out;
    CHECK(!rb.search(Query{}, out).empty());
    RadioBrowser none(nullptr);
    CHECK(none.search(Query{}, out).find("rede") != std::string::npos);
}

namespace {
// Serves an in-memory MP3 as a "live" stream that ends after the bytes run out.
class MemStreamHttp : public HttpClient {
public:
    std::string data;
    bool refuse = false;
    HttpResponse get(const HttpRequest &) override { return {}; }
    std::unique_ptr<HttpStream> openStream(const HttpRequest &, std::string *error) override
    {
        if (refuse) { *error = "conexao recusada"; return nullptr; }
        class S : public HttpStream {
        public:
            const std::string *d;
            size_t pos = 0;
            long read(void *buf, size_t len) override
            {
                size_t n = std::min(len, d->size() - pos);
                std::memcpy(buf, d->data() + pos, n);
                pos += n;
                return long(n);
            }
            int status() const override { return 200; }
            std::string contentType() const override { return "audio/mpeg"; }
        };
        auto *s = new S;
        s->d = &data;
        return std::unique_ptr<HttpStream>(s);
    }
    bool supportsHttps() const override { return true; }
    std::string name() const override { return "memstream"; }
};
}  // namespace

TEST(radio_stream_playback)
{
    host::HostPlatform plat(t::scratch, "");
    MemStreamHttp http;
    http.data = readFixture("tone.mp3");
    AudioPlayer p(plat);
    std::string err;
    CHECK(!p.openStream("javascript:x", &http, &err));
    CHECK(!p.openStream("http://radio.example/stream", nullptr, &err));
    CHECK(p.openStream("http://radio.example/stream", &http, &err));
    CHECK(p.live());
    CHECK_EQ(p.getDuration(), int64_t(0));
    p.play();
    for (int i = 0; i < 400 && p.state() == PlayState::Playing; i++) plat.sleepMs(5);
    CHECK(p.state() == PlayState::Error);            // a live stream that ends = dropped
    CHECK(p.lastError().find("caiu") != std::string::npos);
    CHECK(p.getPosition() > 2500);                    // ~3 s of audio decoded
    p.seek(1000);                                     // ignored for live streams
    http.refuse = true;
    CHECK(p.openStream("http://radio.example/stream", &http, &err));
    p.play();
    for (int i = 0; i < 200 && p.state() != PlayState::Error; i++) plat.sleepMs(5);
    CHECK(p.lastError().find("nao conectou") != std::string::npos);
    http.data = "<html>isto nao e audio</html>";
    http.refuse = false;
    CHECK(p.openStream("http://radio.example/stream", &http, &err));
    p.play();
    for (int i = 0; i < 200 && p.state() == PlayState::Playing; i++) plat.sleepMs(5);
    CHECK(p.state() == PlayState::Error);
}

TEST(radio_live_local_server)
{
    const char *base = std::getenv("ML_RADIO_URL");   // set by tools/run_live_test.sh
    if (!base) { std::printf("(skipped: ML_RADIO_URL not set) "); return; }
    host::HostHttp http;
    RadioBrowser rb(&http);
    rb.servers = {base};
    std::vector<Station> out;
    std::string e = rb.search(Query{}, out);
    CHECK(e.empty());
    CHECK_EQ(out.size(), size_t(2));
    host::HostPlatform plat(t::scratch, "");
    AudioPlayer p(plat);
    std::string err;
    CHECK(p.openStream(std::string(base) + "/tone.mp3", &http, &err));
    p.play();
    for (int i = 0; i < 600 && p.state() == PlayState::Playing; i++) plat.sleepMs(5);
    CHECK(p.getPosition() > 2500);   // real socket stream decoded to the end
}
