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
    std::string words = "pink%20%20floyd%20%20%20live%20%20%20x";
    CHECK(q.find("%28title%3A%28" + words + "%29%20OR%20creator%3A%28" + words + "%29%29%20AND%20mediatype") == 0);
    CHECK(u.find(' ') == std::string::npos);
    CHECK_EQ(metadataUrl("gd1977-05-08.sbd_x"), std::string("https://archive.org/metadata/gd1977-05-08.sbd_x"));
    CHECK(metadataUrl("a/../b").empty());
    CHECK_EQ(coverUrl("abc-1"), std::string("https://archive.org/services/img/abc-1"));
    CHECK(coverUrl("a b").empty());
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
    const char *abase = std::getenv("ML_AUDIUS_URL");
    if (abase) h.app.audiusBase = abase;
    else h.app.settings.audius = false;
    h.app.settings.deezer = false;    // this test is about Archive + Audius against the local server only:
    h.app.settings.jamendo = false;   // the other online sources would reach the real internet
    h.app.goSection(Section::Music, true);
    h.app.frame();
    CHECK(h.app.remote->start(&err));
    std::string pin = h.app.remote->pin();
    size_t coversBefore = h.app.images.cached();
    h.press(BtnOptions);                               // search keyboard
    CHECK(h.app.keyboard.active());
    h.app.remote->handle("POST /api/text HTTP/1.1\r\nX-Pin: " + pin + "\r\n\r\nzzz-sem-resultado-local", h.plat.nowMs());
    h.app.frame();
    CHECK(!h.app.keyboard.active());
    CHECK(h.waitFor("albums=2"));                      // answer arrived from the background thread
    auto shot = [&](const char *name) {                // optional pictures of the screen for a visual check
        const char *dir = std::getenv("ML_SHOT_DIR");
        if (!dir) return;
        for (int i = 0; i < 40; i++) { h.app.frame(); h.plat.sleepMs(16); }
        h.app.dirty();
        h.app.frame();
        h.plat.display_.savePng(std::string(dir) + "/" + name);
    };
    if (abase) {
        CHECK(h.waitFor("songs=2"));                   // Audius: 3 playable in the answer, 1 repeated -> 2
        CHECK(h.waitFor("shelf=Musicas"));             // songs shelf first, selected
        h.press(BtnCross);                             // plays the first Audius song from its stream address
        for (int i = 0; i < 600 && h.app.audio.framesOut() == 0; i++) { h.app.frame(); h.plat.sleepMs(5); }
        CHECK(h.app.audio.framesOut() > 0);
        CHECK(h.app.music.tracks()[h.app.queue.current()].source == "Audius");
        CHECK(!h.app.taste.recent().empty() && !h.app.taste.topGenre().empty());
        shot("07_musica_busca.png");
        h.app.audio.stop();
        h.press(BtnDown);                              // albums shelf
    }
    CHECK(h.app.currentState().find("shelf=Albuns no Internet Archive") != std::string::npos);
    h.press(BtnCross);                                 // open the first album: file list fetched in the background
    CHECK(h.waitFor("view=detail"));
    CHECK(h.waitFor("tracks=2"));
    {   // the album cover is fetched by the image loader (background) and ends up in its cache
        bool got = false;
        for (int i = 0; i < 600 && !got; i++) { h.app.dirty(); h.app.frame(); h.plat.sleepMs(5); got = h.app.images.cached() > coversBefore; }
        CHECK(got);
    }
    h.press(BtnCross);                                 // play track 1 from the "download" address
    shot("06_musica_archive.png");
    for (int i = 0; i < 600 && h.app.audio.framesOut() == 0; i++) { h.app.frame(); h.plat.sleepMs(5); }
    CHECK(h.app.audio.framesOut() > 0);
    CHECK(h.app.audio.remote() && !h.app.audio.live());
    CHECK(h.app.music.tracks()[h.app.queue.current()].url.find("/download/moonlight-test-album/") != std::string::npos);
    h.press(BtnCircle);                                // back to the shelves
    CHECK(h.app.currentState().find("view=shelves") != std::string::npos);
    h.press(BtnRight);                                 // second album: only FLAC -> no tracks, message, no crash
    h.press(BtnCross);
    for (int i = 0; i < 300; i++) { h.app.frame(); h.plat.sleepMs(5); }
    CHECK(h.app.currentState().find("view=detail") != std::string::npos);
    CHECK(h.app.currentState().find("tracks=0") != std::string::npos);
    h.press(BtnCircle);                                // shelves
    h.press(BtnCircle);                                // leaves the search: home with what was just played
    CHECK(h.waitFor("shelf=Tocadas recentemente"));
    CHECK(h.app.taste.recent().size() >= (abase ? 2u : 1u));   // more when the short test songs auto-advanced
    CHECK(h.app.currentState().find("recent=0") == std::string::npos);
    if (abase) {
        // Coming back to the section: trending + recommendations for the most played genre (the song
        // already heard is not recommended again).
        h.app.goSection(Section::Music, true);
        CHECK(h.waitFor("trend=2"));
        for (int i = 0; i < 600 && h.app.musicHome.forYouGenre.empty(); i++) { h.app.frame(); h.plat.sleepMs(5); }
        CHECK_EQ(h.app.musicHome.forYouGenre, h.app.taste.topGenre());   // asked for the most played genre
        for (size_t i : h.app.musicHome.forYou)                           // and never a song just heard
            for (const auto &p : h.app.taste.recent()) CHECK(h.app.music.tracks()[i].url != p.url);
        CHECK(h.app.currentState().find("shelf=Tocadas recentemente") != std::string::npos);
        shot("08_musica_inicio.png");
    }
    h.app.audio.stop();
    h.app.remote->stop();
}
