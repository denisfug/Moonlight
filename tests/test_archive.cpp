// Internet Archive music source: URL building, search/item parsing, and (make live) the whole path
// search -> album -> track playing against a local fixture server. Not tested against archive.org itself.
#include <cstdlib>

#include "app/app.hpp"
#include "music/archive.hpp"
#include "platform/host/host_platform.hpp"
#include "test.hpp"

using namespace ml;
using namespace ml::archive;

TEST(archive_urls)
{
    std::string u = searchUrl("pink (floyd) \"live\": x", 30);
    CHECK(u.find("https://archive.org/advancedsearch.php?q=") == 0);
    CHECK(u.find("output=json") != std::string::npos && u.find("rows=30") != std::string::npos);
    CHECK(u.find("fl%5B%5D=identifier") != std::string::npos);
    // the user's text cannot close the parenthesis or add a field to the query
    std::string q = u.substr(u.find("q=") + 2, u.find("&fl") - u.find("q=") - 2);
    CHECK(q.find("%28pink%20%20floyd%20%20%20live%20%20%20x%29%20AND%20mediatype") == 0);
    CHECK(u.find(' ') == std::string::npos);
    CHECK_EQ(metadataUrl("gd1977-05-08.sbd_x"), std::string("https://archive.org/metadata/gd1977-05-08.sbd_x"));
    CHECK(metadataUrl("a/../b").empty());
    CHECK(metadataUrl("").empty());
    CHECK(validId("abc-1.2_3") && !validId("a b") && !validId("a?b"));
}

TEST(archive_lengths)
{
    CHECK_EQ(parseLength("215.4"), 215);
    CHECK_EQ(parseLength("3:35"), 215);
    CHECK_EQ(parseLength("1:02:03"), 3723);
    CHECK_EQ(parseLength("03:35.50"), 215);
    CHECK_EQ(parseLength(""), 0);
    CHECK_EQ(parseLength("abc"), 0);
    CHECK_EQ(parseLength("3:xx"), 0);
}

TEST(archive_parse_search_and_item)
{
    std::string body;
    host::HostFileSystem fs("");
    CHECK(fs.readFile(t::fixtures + "/archive/advancedsearch.php", body, 1 << 20));
    auto items = parseSearch(parseJson(body));
    CHECK_EQ(items.size(), size_t(2));                    // bad identifier and missing identifier dropped
    CHECK_EQ(items[0].id, std::string("moonlight-test-album"));
    CHECK_EQ(items[0].creator, std::string("Moonlight"));   // first of an array
    CHECK_EQ(items[1].title, std::string("Somente FLAC"));
    CHECK(parseSearch(parseJson("{}")).empty());
    CHECK(parseSearch(parseJson("[1,2]")).empty());

    CHECK(fs.readFile(t::fixtures + "/archive/metadata/moonlight-test-album", body, 1 << 20));
    auto songs = parseItem(parseJson(body));
    CHECK_EQ(songs.size(), size_t(2));                    // 64 kbps copy, FLAC, private and metadata files skipped
    CHECK_EQ(songs[0].title, std::string("Tom de teste"));   // track 1 first although listed second
    CHECK_EQ(songs[0].artist, std::string("Gerador"));
    CHECK_EQ(songs[0].album, std::string("Disco"));
    CHECK_EQ(songs[0].durationSec, 3);
    CHECK_EQ(songs[0].url, std::string("https://archive.org/download/moonlight-test-album/01%20Tom%20de%20teste.mp3"));
    CHECK_EQ(songs[1].title, std::string("02-second"));     // no title: from the file name
    CHECK_EQ(songs[1].artist, std::string("Moonlight"));    // no artist: item creator
    CHECK_EQ(songs[1].album, std::string("Album de teste"));
    CHECK_EQ(songs[1].track, 2);

    CHECK(fs.readFile(t::fixtures + "/archive/metadata/only-flac_item.2", body, 1 << 20));
    CHECK(parseItem(parseJson(body)).empty());            // nothing the MP3 decoder can play
    CHECK(parseItem(parseJson(R"({"metadata":{"identifier":"x y"},"files":[{"name":"a.mp3"}]})")).empty());
    // without any "VBR MP3" file every .mp3 counts
    auto low = parseItem(parseJson(R"({"metadata":{"identifier":"x"},"files":[{"name":"dir/a b.MP3","format":"64Kbps MP3"}]})"));
    CHECK_EQ(low.size(), size_t(1));
    if (!low.empty()) CHECK_EQ(low[0].url, std::string("https://archive.org/download/x/dir/a%20b.MP3"));
}

namespace {
struct Harness {
    host::HostPlatform plat;
    App app;
    explicit Harness(const std::string &sb) : plat(sb, t::fixtures.substr(0, t::fixtures.rfind("/tests/")) + "/assets"), app(plat) {}
    void press(uint32_t b)
    {
        plat.input_.state.buttons = b;
        app.frame();
        plat.input_.state.buttons = 0;
        app.frame();
    }
    bool waitFor(const std::string &what, int ms = 5000)
    {
        for (int i = 0; i < ms / 5; i++) {
            app.frame();
            if (app.currentState().find(what) != std::string::npos) return true;
            plat.sleepMs(5);
        }
        return false;
    }
};
}  // namespace

TEST(archive_live_local_server)
{
    const char *base = std::getenv("ML_ARCHIVE_URL");   // set by tools/run_live_test.sh
    if (!base) { std::printf("(skipped: ML_ARCHIVE_URL not set) "); return; }
    std::string sb = t::scratch + "/appsb_archive";
    host::HostFileSystem(sb).makeDirs("/");
    Harness h(sb);
    std::string err;
    CHECK(h.app.init(&err));
    h.app.archiveBase = base;
    h.app.goSection(Section::Music, true);
    h.app.frame();
    CHECK(h.app.remote->start(&err));
    std::string pin = h.app.remote->pin();
    h.press(BtnOptions);                               // search keyboard
    CHECK(h.app.keyboard.active());
    h.app.remote->handle("POST /api/text HTTP/1.1\r\nX-Pin: " + pin + "\r\n\r\nzzz-sem-resultado-local", h.plat.nowMs());
    h.app.frame();
    CHECK(!h.app.keyboard.active());
    CHECK(h.waitFor("albums=2"));                      // answer arrived from the background thread
    h.press(BtnDown);                                  // first Archive album
    CHECK(h.app.currentState().find("list=1") != std::string::npos);
    h.press(BtnCross);                                 // open it: file list fetched in the background
    CHECK(h.waitFor("tracks=2"));
    CHECK(h.app.currentState().find("zone=1") != std::string::npos);   // focus moved to the tracks
    h.press(BtnCross);                                 // play track 1 from the "download" address
    if (const char *dir = std::getenv("ML_SHOT_DIR")) {   // optional picture of the screen for a visual check
        for (int i = 0; i < 40; i++) { h.app.frame(); h.plat.sleepMs(16); }
        h.app.dirty();
        h.app.frame();
        h.plat.display_.savePng(std::string(dir) + "/06_musica_archive.png");
    }
    for (int i = 0; i < 600 && h.app.audio.framesOut() == 0; i++) { h.app.frame(); h.plat.sleepMs(5); }
    CHECK(h.app.audio.framesOut() > 0);
    CHECK(h.app.audio.remote() && !h.app.audio.live());
    h.press(BtnCircle);                                // back to the list column
    h.press(BtnDown);                                  // second album: only FLAC -> no tracks, message, no crash
    h.press(BtnCross);
    CHECK(h.waitFor("tracks=0"));
    for (int i = 0; i < 200; i++) { h.app.frame(); h.plat.sleepMs(5); }
    CHECK(h.app.currentState().find("zone=0") != std::string::npos);
    h.app.audio.stop();
    h.app.remote->stop();
}
