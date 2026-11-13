// Playback flow on the PC with local servers and a scripted video engine: per-source tabs, playback
// options (hidden links, web browser links), problem log, video test screen.
#include <string>

#include "app/app.hpp"
#include "app/problem_log.hpp"
#include "platform/host/host_platform.hpp"
#include "test.hpp"
#include "test_servers.hpp"

using namespace ml;

namespace {
std::string assetsDir()
{
    std::string f = t::fixtures;   // tests/fixtures -> assets
    return f.substr(0, f.rfind("/tests/")) + "/assets";
}

// A video engine that behaves as told: refuses the file, never shows a picture, or "plays".
struct FakeEngine : VideoEngine {
    bool failOpen, neverFrame;
    bool open_ = false, log_ = false;
    std::string st = "novo", location;
    std::vector<std::pair<std::string, std::string>> headers;
    static std::vector<std::string> &opened()
    {
        static std::vector<std::string> v;
        return v;
    }
    FakeEngine(bool f, bool n) : failOpen(f), neverFrame(n) {}
    void setRequestHeaders(const std::vector<std::pair<std::string, std::string>> &h) override { headers = h; }
    void setInternalLog(bool on) override { log_ = on; }
    bool open(const std::string &loc, std::string *error) override
    {
        location = loc;
        opened().push_back(loc + (headers.empty() ? "" : " +" + headers[0].first + "=" + headers[0].second));
        if (failOpen) { st = "recusado"; if (error) *error = "recusado de teste (0x806A0002)"; return false; }
        open_ = true;
        st = "aberto";
        return true;
    }
    void pause() override {}
    void resume() override {}
    void stop() override { open_ = false; }
    bool seek(int64_t) override { return true; }
    int64_t positionMs() override { return 0; }
    int64_t durationMs() override { return 20000; }
    bool paused() override { return false; }
    bool finished() override { return false; }
    bool updateFrame(Framebuffer fb) override
    {
        if (!open_ || neverFrame) return false;
        for (int i = 0; i < fb.width * fb.height; i++) fb.pixels[i] = 0xFF2040A0u;
        st = "tocando";
        return true;
    }
    std::string status() override { return st; }
    std::string diagnostics() override { return std::string("diag de teste: AddSource 0x00000000\nlog interno: ") + (log_ ? "ligado" : "desligado"); }
};

struct TestPlatform : host::HostPlatform {
    using host::HostPlatform::HostPlatform;
    bool noEngine = false, failOpen = false, neverFrame = false;
    std::unique_ptr<VideoEngine> makeVideoEngine() override
    {
        if (noEngine) return nullptr;
        return std::unique_ptr<VideoEngine>(new FakeEngine(failOpen, neverFrame));
    }
    std::string videoEngineError() override { return "modulo AvPlayer: carregar 0x80020002"; }
};

struct Rig {
    TestPlatform plat;
    App app;
    explicit Rig(const std::string &sb) : plat(sb, assetsDir()), app(plat) {}
    void press(uint32_t b)
    {
        plat.input_.state.buttons = b;
        app.frame();
        plat.input_.state.buttons = 0;
        app.frame();
    }
    // runs frames until the current screen's state contains `what` (or the time is over)
    bool waitState(const std::string &what, int ms = 4000)
    {
        for (int i = 0; i < ms / 10; i++) {
            if (app.currentState().find(what) != std::string::npos) return true;
            plat.sleepMs(10);
            app.frame();
        }
        std::printf("[estado: %s] ", app.currentState().c_str());
        return false;
    }
};

std::string sandbox(const char *name)
{
    std::string sb = t::scratch + "/" + name;
    host::HostFileSystem(sb).makeDirs("/");
    return sb;
}
}  // namespace

TEST(problem_log_keeps_newest_first_and_persists)
{
    host::HostFileSystem fs(t::scratch);
    fs.makeDirs("/plog");
    JsonStore store(fs, "/plog");
    {
        ProblemLog log(store);
        log.load();
        CHECK(log.entries().empty());
        Problem a;
        a.title = "Filme A";
        a.poster = "https://img/a.jpg";
        a.reason = "o servidor nao respondeu";
        a.details = "linha 1\nlinha 2";
        a.when = 100;
        log.add(a);
        Problem b = a;
        b.title = "Filme B";
        log.add(b);
        b.details = "de novo";
        log.add(b);   // same title + reason as the newest: refreshed, not duplicated
        CHECK_EQ(log.entries().size(), size_t(2));
        CHECK_EQ(log.entries()[0].title, std::string("Filme B"));
        CHECK_EQ(log.entries()[0].details, std::string("de novo"));
        for (int i = 0; i < 60; i++) {
            Problem p;
            p.title = "T" + std::to_string(i);
            p.reason = "r";
            log.add(p);
        }
        CHECK_EQ(log.entries().size(), ProblemLog::kMax);
    }
    ProblemLog again(store);
    again.load();
    CHECK_EQ(again.entries().size(), ProblemLog::kMax);
    CHECK_EQ(again.entries()[0].title, std::string("T59"));
    again.clear();
    ProblemLog empty(store);
    empty.load();
    CHECK(empty.entries().empty());
    CHECK_EQ(hostForLog("https://cdn.example.com:8443/a/b.mp4?token=segredo"), std::string("cdn.example.com"));
    CHECK_EQ(hostForLog("/data/moonlight/teste_video.mp4"), std::string("arquivo local"));
}

TEST(catalog_shows_one_source_at_a_time)
{
    t::PathServer a, b;
    a.files["/manifest.json"] = R"({"id":"t.a","version":"1.0.0","name":"Fonte Mostarda","resources":["catalog"],"types":["movie"],"catalogs":[
        {"type":"movie","id":"top","name":"Em alta","extra":[{"name":"genre","options":["Acao","Comedia"]}]},{"type":"movie","id":"novos","name":"Novidades"}]})";
    a.files["/catalog/movie/top.json"] = R"({"metas":[{"id":"zz1","type":"movie","name":"Filme Um"}]})";
    a.files["/catalog/movie/novos.json"] = R"({"metas":[]})";
    a.files["/catalog/movie/top/genre=Acao.json"] = R"({"metas":[{"id":"zz2","type":"movie","name":"Filme de Acao"}]})";
    b.files["/manifest.json"] = R"({"id":"t.b","version":"1.0.0","name":"Fonte Ketchup","resources":["catalog"],"types":["movie","series"],"catalogs":[
        {"type":"movie","id":"k","name":"Catalogo K"},{"type":"series","id":"s","name":"Series K"}]})";
    b.files["/catalog/movie/k.json"] = R"({"metas":[{"id":"zz9","type":"movie","name":"Filme K"}]})";
    CHECK(a.start(52500));
    CHECK(b.start(53000));
    Rig h(sandbox("appsb_tabs"));
    std::string err;
    CHECK(h.app.init(&err));
    CHECK(h.app.addSourceByUser(a.url("/manifest.json")).empty());
    CHECK(h.app.addSourceByUser(b.url("/manifest.json")).empty());

    h.app.goSection(Section::Movies, true);
    h.app.frame();
    // first source selected: its 2 catalogs + 2 genre rows; nothing of the other source was requested
    CHECK(h.waitState("aba=Fonte Mostarda abas=2 linhas=4"));
    for (int i = 0; i < 30; i++) { h.plat.sleepMs(10); h.app.frame(); }
    CHECK_EQ(b.hits.load(), 1);   // only its manifest
    h.press(BtnR1);
    CHECK(h.waitState("aba=Fonte Ketchup abas=2 linhas=1"));
    for (int i = 0; i < 60 && b.hits.load() < 2; i++) { h.plat.sleepMs(10); h.app.frame(); }
    CHECK_EQ(b.hits.load(), 2);   // now its movie catalog
    h.press(BtnUp);               // from the first row up to the row of sources
    CHECK(h.app.currentState().find("foco=abas") != std::string::npos);
    h.press(BtnLeft);
    CHECK(h.waitState("aba=Fonte Mostarda"));
    h.press(BtnDown);
    CHECK(h.app.currentState().find("foco=abas") == std::string::npos);

    h.app.goSection(Section::Series, true);   // series: only the source that has series
    h.app.frame();
    CHECK(h.waitState("aba=Fonte Ketchup abas=1 linhas=1"));
}

TEST(playback_options_hide_what_cannot_play_and_log_problems)
{
    // A answers at once (details + links of every kind), B takes 1.2 s, C is broken.
    t::PathServer a, b, c;
    a.files["/manifest.json"] = R"({"id":"t.a","version":"1.0.0","name":"Fonte A","resources":["meta","stream"],"types":["movie"],"idPrefixes":["zz"],"catalogs":[]})";
    a.files["/meta/movie/zz1.json"] = R"({"meta":{"id":"zz1","type":"movie","name":"Filme de Teste","poster":"http://127.0.0.1:9/capa.jpg","description":"Descricao vinda da fonte A."}})";
    a.files["/stream/movie/zz1.json"] = R"({"streams":[
        {"infoHash":"0123456789abcdef0123456789abcdef01234567","name":"A torrent"},
        {"url":"https://cdn.example/f/filme.1080p.mkv","name":"A MKV","description":"filme.1080p.mkv\n2.1 GB"},
        {"externalUrl":"https://site.example/assistir/zz1","name":"A externo"},
        {"url":"https://cdn.example/f/filme.720p.mp4","name":"A MP4","behaviorHints":{"proxyHeaders":{"request":{"Referer":"https://a.example/"}}}}]})";
    a.files["/meta/movie/zz2.json"] = R"({"meta":{"id":"zz2","type":"movie","name":"So Torrent"}})";
    a.files["/stream/movie/zz2.json"] = R"({"streams":[{"infoHash":"0123456789abcdef0123456789abcdef01234567","name":"A torrent"}]})";
    b.files["/manifest.json"] = R"({"id":"t.b","version":"1.0.0","name":"Fonte B","resources":["stream"],"types":["movie"],"idPrefixes":["zz1"],"catalogs":[]})";
    b.files["/stream/movie/zz1.json"] = R"({"streams":[{"url":"https://b.example/play/123","name":"B direto"}]})";
    b.delayMs = 1200;
    b.delayPath = "/stream/";
    c.files["/manifest.json"] = R"({"id":"t.c","version":"1.0.0","name":"Fonte C","resources":["meta","stream"],"types":["movie"],"idPrefixes":["zz1"],"catalogs":[]})";
    c.status["/meta/movie/zz1.json"] = 500;
    c.status["/stream/movie/zz1.json"] = 500;
    CHECK(a.start(53500));
    CHECK(b.start(54000));
    CHECK(c.start(54500));

    Rig h(sandbox("appsb_streams"));
    std::string err;
    CHECK(h.app.init(&err));
    CHECK(h.app.addSourceByUser(c.url("/manifest.json")).empty());   // the broken one first: it must not block the others
    CHECK(h.app.addSourceByUser(a.url("/manifest.json")).empty());
    CHECK(h.app.addSourceByUser(b.url("/manifest.json")).empty());
    h.plat.failOpen = true;   // the "player" refuses every file in this test
    FakeEngine::opened().clear();

    h.app.push(makeDetail(h.app, "movie", "zz1", "Nome provisorio"));
    h.app.frame();
    CHECK_EQ(h.app.currentTitle(), std::string("Nome provisorio"));   // on screen before any answer
    for (int i = 0; i < 300 && h.app.currentTitle() != "Filme de Teste"; i++) { h.plat.sleepMs(10); h.app.frame(); }
    CHECK_EQ(h.app.currentTitle(), std::string("Filme de Teste"));

    h.press(BtnCross);   // "Assistir"
    CHECK_EQ(h.app.currentTitle(), std::string("Opcoes de reproducao"));
    // A's playable links show up while B is still being waited for; C already failed; torrent + MKV hidden
    CHECK(h.waitState("links=2 escondidos=2 pendentes=1 falhas=1", 1000));
    CHECK(h.app.currentState().find("sel=A MP4 fmt=mp4") != std::string::npos);   // what the PS4 can play comes first
    CHECK(h.waitState("links=3 escondidos=2 pendentes=0 falhas=1"));
    CHECK(h.app.problems.entries().empty());

    // play the MP4: the engine refuses it -> error on screen and an entry in the problem log
    h.press(BtnCross);
    for (int i = 0; i < 10; i++) h.app.frame();
    CHECK_EQ(FakeEngine::opened().size(), size_t(1));
    if (!FakeEngine::opened().empty())
        CHECK_EQ(FakeEngine::opened()[0], std::string("https://cdn.example/f/filme.720p.mp4 +Referer=https://a.example/"));
    CHECK_EQ(h.app.problems.entries().size(), size_t(1));
    if (!h.app.problems.entries().empty()) {
        const Problem &p = h.app.problems.entries()[0];
        CHECK(p.title.find("Filme de Teste") == 0);
        CHECK_EQ(p.poster, std::string("http://127.0.0.1:9/capa.jpg"));
        CHECK_EQ(p.where, std::string("cdn.example"));
        CHECK(p.source.find("A MP4 (Fonte A)") != std::string::npos);
        CHECK(p.reason.find("recusado de teste") != std::string::npos);
        CHECK(p.details.find("diag de teste") != std::string::npos);
        CHECK(p.when > 1700000000);
    }
    h.press(BtnCircle);   // leave the player
    CHECK_EQ(h.app.currentTitle(), std::string("Opcoes de reproducao"));

    h.press(BtnDown);
    CHECK(h.app.currentState().find("sel=B direto") != std::string::npos);   // unknown format: second
    h.press(BtnDown);
    CHECK(h.app.currentState().find("sel=A externo") != std::string::npos);  // external: third, as a QR code for the phone
    h.press(BtnCross);
    CHECK_EQ(h.app.currentState(), std::string("link=https://site.example/assistir/zz1"));
    h.press(BtnCircle);

    h.press(BtnSquare);   // show what was hidden
    CHECK(h.app.currentState().find("links=5 escondidos=2") != std::string::npos);
    h.press(BtnDown);
    h.press(BtnDown);
    CHECK(h.app.currentState().find("sel=A torrent") != std::string::npos);   // not supported: last
    h.press(BtnCross);
    CHECK_EQ(h.app.modalTitle(), std::string("Nao suportado"));
    h.press(BtnCross);
    h.press(BtnCircle);   // back to the details

    // a title whose only link is P2P: nothing listed, and it is written down with the reason
    h.app.push(makeDetail(h.app, "movie", "zz2", "So Torrent"));
    for (int i = 0; i < 300 && h.app.currentState().find("meta=ok") == std::string::npos && i < 60; i++) { h.plat.sleepMs(10); h.app.frame(); }
    h.press(BtnCross);
    CHECK(h.waitState("links=0 escondidos=1 pendentes=0"));
    CHECK_EQ(h.app.problems.entries().size(), size_t(2));
    if (h.app.problems.entries().size() == 2) {
        CHECK_EQ(h.app.problems.entries()[0].title, std::string("So Torrent"));
        CHECK_EQ(h.app.problems.entries()[0].reason, std::string("nenhum link que o PS4 abra: 1 P2P"));
    }

    // Configuracoes > Problemas ao reproduzir lists them
    h.app.push(makeProblems(h.app));
    h.app.frame();
    CHECK_EQ(h.app.currentState(), std::string("problemas=2"));
    h.press(BtnCross);
    CHECK_EQ(h.app.currentTitle(), std::string("Detalhes do problema"));
}

TEST(player_reports_no_picture_and_missing_engine)
{
    Rig h(sandbox("appsb_player"));
    std::string err;
    CHECK(h.app.init(&err));
    h.app.tuning.firstFrameMs = 300;
    h.plat.neverFrame = true;   // accepts the file, never shows a picture
    PlayContext ctx;
    ctx.title = "Video mudo";
    ctx.source = "teste";
    h.app.playVideo("/data/moonlight/videos/x.mp4", ctx);
    for (int i = 0; i < 80 && h.app.problems.entries().empty(); i++) { h.plat.sleepMs(10); h.app.frame(); }
    CHECK_EQ(h.app.problems.entries().size(), size_t(1));
    if (!h.app.problems.entries().empty()) {
        CHECK(h.app.problems.entries()[0].reason.find("nenhuma imagem chegou") != std::string::npos);
        CHECK_EQ(h.app.problems.entries()[0].where, std::string("arquivo local"));
    }
    h.press(BtnCircle);

    h.plat.noEngine = true;     // the system player did not load
    h.app.playVideo("https://x.example/v.mp4", "Outro video");
    CHECK(h.app.modalOpen());
    CHECK(h.app.modalBody().find("0x80020002") != std::string::npos);
    CHECK_EQ(h.app.problems.entries().size(), size_t(2));
    if (h.app.problems.entries().size() == 2) CHECK(h.app.problems.entries()[0].details.find("0x80020002") != std::string::npos);
}

TEST(video_test_screen_steps)
{
    // a "video" the screen accepts: > 100 KB with an MP4 signature
    std::string mp4 = std::string("\0\0\0\x20", 4) + "ftypisom" + std::string(150 * 1024, 'v');
    t::PathServer s;
    s.files["/teste.mp4"] = mp4;
    CHECK(s.start(55000));
    {
        Rig h(sandbox("appsb_vtest_ok"));
        std::string err;
        CHECK(h.app.init(&err));
        h.app.videoTestUrl = s.url("/teste.mp4");
        h.app.tuning.testWaitMs = 500;
        h.app.tuning.testPlayMs = 100;
        FakeEngine::opened().clear();
        h.app.push(makeVideoTest(h.app));
        CHECK(h.waitState("passo=fim", 6000));
        CHECK(h.app.currentState().find("do HD e pela internet") != std::string::npos);
        CHECK_EQ(FakeEngine::opened().size(), size_t(2));   // local file, then the URL
        if (FakeEngine::opened().size() == 2) {
            CHECK(FakeEngine::opened()[0].find("/teste_video.mp4") != std::string::npos);
            CHECK_EQ(FakeEngine::opened()[1], s.url("/teste.mp4"));
        }
        std::string saved;
        CHECK(h.plat.fs().readFile(h.plat.dataDir() + "/teste_video.mp4", saved, 1 << 20));
        CHECK(saved == mp4);
        CHECK(h.app.problems.entries().empty());
    }
    {
        Rig h(sandbox("appsb_vtest_bad"));
        std::string err;
        CHECK(h.app.init(&err));
        h.app.videoTestUrl = s.url("/teste.mp4");
        h.app.tuning.testWaitMs = 300;
        h.app.tuning.testPlayMs = 100;
        h.plat.failOpen = true;
        FakeEngine::opened().clear();
        h.app.push(makeVideoTest(h.app));
        CHECK(h.waitState("passo=fim", 6000));
        CHECK(h.app.currentState().find("nem o MP4 de teste gravado no HD") != std::string::npos);
        CHECK_EQ(FakeEngine::opened().size(), size_t(2));   // local, then local again with the internal log
        CHECK_EQ(h.app.problems.entries().size(), size_t(1));
        if (!h.app.problems.entries().empty()) {
            const Problem &p = h.app.problems.entries()[0];
            CHECK_EQ(p.title, std::string("Teste de video"));
            CHECK(p.details.find("recusado de teste (0x806A0002)") != std::string::npos);
            CHECK(p.details.find("log interno: ligado") != std::string::npos);
        }
    }
    {
        Rig h(sandbox("appsb_vtest_net"));   // the download itself fails: nothing is blamed on the player
        std::string err;
        CHECK(h.app.init(&err));
        h.app.videoTestUrl = s.url("/nao-existe.mp4");
        h.app.push(makeVideoTest(h.app));
        CHECK(h.waitState("passo=fim", 6000));
        CHECK(h.app.currentState().find("nao baixou") != std::string::npos);
    }
}
