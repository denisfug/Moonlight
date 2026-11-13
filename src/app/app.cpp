#include "app/app.hpp"
#include "app/multi_fetch.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>

#include "app/demo_data.hpp"
#include "ui/theme.hpp"

#ifndef ML_VERSION
#define ML_VERSION "dev"
#endif

namespace ml {

using namespace ui;

namespace {
struct NavItem { Section s; Icon icon; const char *label; };
const NavItem kNav[] = {
    {Section::Home, Icon::Home, "Inicio"},       {Section::Music, Icon::Music, "Musica"},
    {Section::Radio, Icon::Live, "Radios"},
    {Section::Movies, Icon::Film, "Filmes"},     {Section::Series, Icon::Series, "Series"},   {Section::Videos, Icon::Play, "Videos"},
    {Section::Live, Icon::Live, "Canais"},       {Section::Library, Icon::Library, "Biblioteca"},
    {Section::Sources, Icon::Sources, "Fontes"}, {Section::Settings, Icon::Settings, "Configuracoes"},
};
}  // namespace

App::App(Platform &p)
    : platform(p), store(p.fs(), p.dataDir()), library(store), sources(store, p.http()), audio(p),
      radio(p.http()), radioFavorites(store), problems(store), images(p, p.http()), remote(new RemoteControl(p)),
      updater(new Updater(p, "denisfug/Moonlight", ML_VERSION))
{
}

namespace {
Platform *gCrumbPlatform = nullptr;   // plain pointer: no static constructor
std::atomic_flag gCrumbLock = ATOMIC_FLAG_INIT;
const char kStable[] = "estavel";

void writeCrumb(const char *step, bool done)
{
    if (!gCrumbPlatform) return;
    while (gCrumbLock.test_and_set(std::memory_order_acquire)) {}
    gCrumbPlatform->fs().writeFile(gCrumbPlatform->dataDir() + "/startup.txt", done ? kStable : step);
    gCrumbLock.clear(std::memory_order_release);
}
}  // namespace

void App::crumb(const std::string &step) { writeCrumb(step.c_str(), false); }

App::~App()
{
    // background requests (title details, stream links) use the platform's HTTP client: let them end
    for (int i = 0; i < 6000 && MultiFetch::activeThreads() > 0; i++) platform.sleepMs(10);
    setNetTrace(nullptr);
    gCrumbPlatform = nullptr;
    remote->stop();
    audio.stop();
}


bool App::init(std::string *error)
{
    std::string r, b;
    platform.fs().readFile(platform.assetDir() + "/fonts/Outfit-Regular.ttf", r, 4 << 20);
    platform.fs().readFile(platform.assetDir() + "/fonts/Outfit-Bold.ttf", b, 4 << 20);
    regular_.reset(new Font(std::move(r)));
    bold_.reset(new Font(std::move(b)));
    if (!regular_->ok() || !bold_->ok()) {
        if (error) *error = "fontes nao carregaram de " + platform.assetDir() + "/fonts";
        return false;
    }
    fonts = {regular_.get(), bold_.get()};
    platform.afterAssetsLoaded();
    platform.fs().makeDirs(platform.dataDir());
    platform.fs().makeDirs(platform.dataDir() + "/music");
    {
        std::string prev;
        if (platform.fs().readFile(platform.dataDir() + "/startup.txt", prev, 4096) && !prev.empty() && prev != kStable)
            crashedAt_ = prev;
        gCrumbPlatform = &platform;
        crumb("abrindo o app (lendo dados e telas)");
        setNetTrace(&writeCrumb);
    }
    loadSettings();
    library.load();
    problems.load();
    sources.load();
    radioFavorites.load();
    crumb("abrindo o app (musicas e canais)");
    server = subsonic::loadConfig(store);
    rescanMusic();
    loadChannels();
    audio.setVolume(settings.volume);
    images.start();

    crumb("abrindo o app (montando as telas)");
    roots_.resize(size_t(Section::Count));
    roots_[size_t(Section::Home)] = makeHome(*this);
    roots_[size_t(Section::Music)] = makeMusic(*this);
    roots_[size_t(Section::Radio)] = makeRadio(*this);
    roots_[size_t(Section::Movies)] = makeCatalog(*this, "movie");
    roots_[size_t(Section::Series)] = makeCatalog(*this, "series");
    roots_[size_t(Section::Videos)] = makeVideos(*this);
    roots_[size_t(Section::Live)] = makeLive(*this);
    roots_[size_t(Section::Library)] = makeLibrary(*this);
    roots_[size_t(Section::Sources)] = makeSources(*this);
    roots_[size_t(Section::Settings)] = makeSettings(*this);
    roots_[0]->onEnter();
    crumb("abrindo o app (primeira tela)");
    now_ = last_ = startMs_ = platform.nowMs();
    if (!crashedAt_.empty()) {
        // Safe start: what runs on its own (phone server, update check) stays off this time.
        modal("O Moonlight fechou da ultima vez",
              "Ele parou durante: " + crashedAt_ +
                  ".\nNesta abertura a busca de atualizacao e o controle pelo celular ficam desligados."
                  "\nMande esta mensagem (foto) para quem esta desenvolvendo o app.");
        updateCheckDone_ = true;
    } else if (settings.remote) {
        std::string e;
        if (remote->start(&e)) toast("Controle pelo celular: " + remote->address() + "  PIN " + remote->pin());
        else toast("Controle pelo celular nao ligou: " + e);
    }
    if (platform.name() != "ps4") updateCheckDone_ = true;   // the PC build never checks on its own
    return true;
}

void App::loadSettings()
{
    Json j = store.load("settings.json");
    settings.animations = j["animations"].asBool(true);
    settings.showDemo = j["showDemo"].asBool(true);
    settings.showRadio = j["showRadio"].asBool(false);
    settings.remote = j["remote"].asBool(false);
    settings.volume = int(j["volume"].asInt(80));
}

void App::saveSettings()
{
    Json j = Json::object();
    j.set("animations", Json::boolean(settings.animations));
    j.set("showDemo", Json::boolean(settings.showDemo));
    j.set("showRadio", Json::boolean(settings.showRadio));
    j.set("remote", Json::boolean(settings.remote));
    j.set("volume", Json::number(settings.volume));
    store.save("settings.json", j);
}

void App::rescanMusic()
{
    audio.stop();
    music = music::MusicLibrary();
    std::vector<std::string> dirs = platform.musicDirs();
    music.scan(platform.fs(), dirs);
    if (settings.showDemo) music.addDemo();
    serverStatus.clear();   // server playlists are fetched again by the Music screen
}

void App::loadChannels()
{
    std::string text, path = platform.dataDir() + "/channels.m3u";
    channels.clear();
    if (platform.fs().exists(path) && platform.fs().readFile(path, text, 16 << 20)) {
        M3uResult r = parseM3u(text);
        if (r.error.empty()) {
            channels = std::move(r.channels);
            channelsSource = "channels.m3u (" + std::to_string(channels.size()) + " canais)";
            return;
        }
        toast("channels.m3u invalido: " + r.error);
    }
    if (settings.showDemo) channels = demo::channels();
    channelsSource = "demonstracao (coloque sua lista em " + path + ")";
}

Screen &App::current() { return stack_.empty() ? *roots_[size_t(section_)] : *stack_.back(); }

void App::goSection(Section s, bool focusContent)
{
    stack_.clear();
    if (s != section_) {
        section_ = s;
        fade_ = settings.animations ? 1.0f : 0.0f;
    }
    sidebarSel_ = int(s);
    sidebarFocus_ = !focusContent;
    current().onEnter();
    dirty();
}

void App::push(std::unique_ptr<Screen> s)
{
    stack_.push_back(std::move(s));
    stack_.back()->onEnter();
    sidebarFocus_ = false;
    fade_ = settings.animations ? 1.0f : 0.0f;
    dirty();
}

void App::pop()
{
    if (!stack_.empty()) stack_.pop_back();
    current().onEnter();
    dirty();
}

void App::toast(const std::string &text)
{
    toast_.text = text;
    toast_.until = platform.nowMs() + 3500;
    dirty();
}

void App::modal(const std::string &title, const std::string &body, std::vector<std::string> buttons,
                std::function<void(int)> onChoice)
{
    modal_.open = true;
    modal_.title = title;
    modal_.body = body;
    modal_.buttons = buttons.empty() ? std::vector<std::string>{"OK"} : std::move(buttons);
    modal_.sel = 0;
    modal_.cb = std::move(onChoice);
    dirty();
}

void App::openKeyboard(const std::string &title, const std::string &initial, bool url,
                       std::function<void(const std::string &)> onDone)
{
    kbDone_ = std::move(onDone);
    // While the phone is in use (last 10 min), keep the app's own keyboard: the phone can fill it.
    bool phone = remote->running() && remote->lastSeenMs() > 0 && platform.nowMs() - remote->lastSeenMs() < 10 * 60 * 1000;
    if (!phone && platform.startTextInput(title, initial, url)) { systemKeyboard_ = true; dirty(); return; }
    keyboard.open(title, initial, url);
    dirty();
}

void App::playTracks(const std::vector<size_t> &tracks, size_t start)
{
    queue.set(tracks, start);
    size_t t = queue.current();
    if (t >= music.tracks().size()) return;
    const music::Track &tr = music.tracks()[t];
    if (tr.demo) {
        toast("\"" + tr.title + "\" e uma faixa de demonstracao (sem audio). Copie MP3 para " + platform.dataDir() + "/music");
        return;
    }
    std::string err;
    if (!startTrack(t, &err)) toast("Nao foi possivel tocar: " + err);
}

bool App::startTrack(size_t t, std::string *err)
{
    if (t >= music.tracks().size() || music.tracks()[t].demo) { if (err) *err = "faixa sem audio"; return false; }
    const music::Track &tr = music.tracks()[t];
    bool ok;
    if (tr.remote) {
        subsonic::Client cl(platform.http(), server);
        ok = audio.openStream(cl.streamUrl(tr.path), platform.http(), err, false, tr.durationMs);
    } else {
        ok = audio.open(tr.path, err);
    }
    if (ok) audio.play();
    return ok;
}

std::string App::loadServerMusic()
{
    music.removeRemote();
    if (!server.configured()) { serverStatus.clear(); return ""; }
    subsonic::Client cl(platform.http(), server);
    auto toTracks = [](const std::vector<subsonic::Song> &songs) {
        std::vector<music::Track> v;
        for (const auto &s : songs) {
            music::Track t;
            t.path = s.id;
            t.title = s.title;
            t.artist = s.artist.empty() ? "Artista desconhecido" : s.artist;
            t.album = s.album;
            t.durationMs = int64_t(s.durationSec) * 1000;
            v.push_back(t);
        }
        return v;
    };
    std::vector<subsonic::Playlist> pls;
    std::string e = cl.playlists(pls);
    if (!e.empty()) { serverStatus = "erro: " + e; return e; }
    int loaded = 0;
    for (const auto &p : pls) {
        if (loaded >= 30) break;   // keeps the first load short; more on demand later
        std::vector<subsonic::Song> songs;
        if (cl.playlist(p.id, songs).empty() && !songs.empty()) { music.addRemote(p.name, toTracks(songs), true); loaded++; }
    }
    std::vector<subsonic::Song> mix;
    if (cl.randomSongs(60, mix).empty() && !mix.empty()) music.addRemote("Mix aleatorio (servidor)", toTracks(mix), true);
    serverStatus = "conectado: " + std::to_string(loaded) + " playlists";
    return "";
}

std::vector<size_t> App::searchServer(const std::string &q, std::string *err)
{
    if (!server.configured()) return {};
    subsonic::Client cl(platform.http(), server);
    std::vector<subsonic::Song> songs;
    std::string e = cl.search(q, songs);
    if (!e.empty()) { if (err) *err = e; return {}; }
    std::vector<music::Track> tracks;
    for (const auto &s : songs) {
        music::Track t;
        t.path = s.id;
        t.title = s.title;
        t.artist = s.artist;
        t.album = s.album;
        t.durationMs = int64_t(s.durationSec) * 1000;
        tracks.push_back(t);
    }
    return music.addRemote("", tracks, false);
}

void App::setServer(const subsonic::Config &c)
{
    server = c;
    subsonic::saveConfig(store, c);
    serverStatus.clear();
}

void App::playStation(const radio::Station &s)
{
    std::string err;
    station = s;
    if (!audio.openStream(s.url, platform.http(), &err)) { toast("Radio: " + err); return; }
    audio.play();
    radio.countClick(s.uuid);
    toast("Conectando em " + s.name + "...");
}

void App::reportProblem(Problem p)
{
    p.when = int64_t(std::time(nullptr));
    problems.add(std::move(p));
}

void App::playVideo(const std::string &location, const PlayContext &ctx)
{
    std::unique_ptr<VideoEngine> engine = platform.makeVideoEngine();
    if (!engine) {
        std::string why = platform.videoEngineError();
        Problem p;
        p.title = ctx.title;
        p.poster = ctx.poster;
        p.source = ctx.source;
        p.where = hostForLog(location);
        p.reason = "o player de video do sistema nao carregou";
        p.details = why.empty() ? std::string("sem detalhes") : why;
        reportProblem(p);
        modal("Player de video", std::string(VideoPlayer::unavailableReason()) + (why.empty() ? "" : "\n" + why));
        return;
    }
    engine->setRequestHeaders(ctx.requestHeaders);
    audio.stop();   // one audio source at a time
    push(makePlayer(*this, std::move(engine), location, ctx));
}

std::string App::addSourceByUser(const std::string &url)
{
    std::string low = url.substr(0, 7);
    for (auto &ch : low) ch = char(ch >= 'A' && ch <= 'Z' ? ch + 32 : ch);
    bool plain = low == "http://";
    std::string e = sources.add(url, plain, int64_t(now_));
    if (e.empty()) sourcesVersion++;
    return e;
}

void App::togglePause()
{
    if (audio.live() && audio.state() == PlayState::Error) { playStation(station); return; }
    if (audio.state() == PlayState::Playing) audio.pause();
    else if (audio.state() == PlayState::Paused || audio.state() == PlayState::Ready || audio.state() == PlayState::Ended) audio.play();
    dirty();
}

void App::skip(int dir)
{
    if (queue.empty() || audio.live()) return;
    if (dir > 0) queue.next(true);
    else {
        if (audio.getPosition() > 4000) { audio.seek(0); return; }
        queue.previous();
    }
    // skip over demo entries when real files exist in the queue
    for (size_t n = 0; n < queue.size(); n++) {
        size_t t = queue.current();
        if (t < music.tracks().size() && !music.tracks()[t].demo) break;
        if (dir > 0) queue.next(true); else queue.previous();
    }
    size_t t = queue.current();
    if (t >= music.tracks().size() || music.tracks()[t].demo) return;
    std::string err;
    if (!startTrack(t, &err)) toast("Nao foi possivel tocar: " + err);
    dirty();
}

// ---------------------------------------------------------------- frame
bool App::frame()
{
    now_ = platform.nowMs();
    float dt = std::min(0.1f, float(now_ - last_) / 1000.0f);
    last_ = now_;
    animT_ += dt;

    if (systemKeyboard_) {   // the system dialog owns the controller until it closes
        std::string text;
        int r = platform.pollTextInput(&text);
        if (r != 0) {
            systemKeyboard_ = false;
            if (r == 1 && kbDone_) { auto cb = kbDone_; kbDone_ = nullptr; cb(text); }
            else kbDone_ = nullptr;
        }
        dirty();
    }
    // the on-screen keyboard repeats faster than the rest of the UI
    input_.repeatDelayMs = keyboard.active() ? 220 : 380;
    input_.repeatRateMs = keyboard.active() ? 45 : 85;
    uint32_t pressed = systemKeyboard_ ? 0 : input_.update(platform.input().read(), now_);
    if (pressed) { handleInput(pressed); dirty(); }
    if (remote->running()) serviceRemote();
    if (!stableWritten_ && renderedLastFrame()) { writeCrumb(kStable, true); stableWritten_ = true; }
    // Update check 6 s after start, once the screen is up (so a failure there is distinguishable).
    if (!updateCheckDone_ && now_ > 6000 + startMs_) { updateCheckDone_ = true; updater->check(); }
    Updater::State us = updater->state();
    if (us.phase != lastUpdatePhase_) {
        if (us.phase == Updater::Phase::Available)
            toast("Nova versao " + us.latest + " do Moonlight! Configuracoes > Atualizar o Moonlight");
        if (us.phase == Updater::Phase::Downloaded) toast("Versao " + us.latest + " baixada. Configuracoes > Atualizar para instalar");
        lastUpdatePhase_ = us.phase;
        dirty();
    }
    if (us.phase == Updater::Phase::Downloading && section_ == Section::Settings) dirty();

    // music: auto-advance at end of track, redraw while the position changes
    PlayState st = audio.state();
    if (st == PlayState::Error && lastAudioState_ != PlayState::Error && audio.live())
        toast(station.name + ": " + audio.lastError());
    if (st == PlayState::Ended && lastAudioState_ == PlayState::Playing && !audio.live()) {
        if (queue.next(false)) {
            size_t t = queue.current();
            std::string err;
            if (t < music.tracks().size() && !music.tracks()[t].demo) startTrack(t, &err);
        }
    }
    lastAudioState_ = st;
    int64_t pos = audio.getPosition() / 1000;
    if (pos != lastAudioPos_) { lastAudioPos_ = pos; dirty(); }

    images.trim();
    if (images.takeUpdated()) dirty();
    current().update(dt);
    if (fade_ > 0) { fade_ = std::max(0.0f, fade_ - dt * 11.0f); dirty(); }   // shorter transition = fewer heavy redraws
    if (current().animating()) dirty();
    if (!toast_.text.empty() && now_ > toast_.until) { toast_.text.clear(); dirty(); }

    rendered_ = false;
    if (dirty_) {
        Canvas c(platform.display().backBuffer());
        draw(c);
        platform.display().present();
        dirty_ = false;
        rendered_ = true;
    }
    return true;
}

// Commands from the phone page (remote.cpp), applied on the UI thread.
void App::serviceRemote()
{
    for (RemoteControl::Command &c : remote->take()) {
        switch (c.kind) {
        case RemoteControl::Command::Buttons:
            if (!systemKeyboard_) handleInput(c.buttons);
            break;
        case RemoteControl::Command::Text:
            if (keyboard.active() && kbDone_) {
                keyboard.close();
                auto cb = kbDone_;
                kbDone_ = nullptr;
                cb(c.text);
            } else if (systemKeyboard_) {
                toast("Feche o teclado da TV e tente de novo pelo celular");
            } else {
                toast("Texto do celular recebido, mas nenhum campo de texto esta aberto");
            }
            break;
        case RemoteControl::Command::Source: {
            std::string e = addSourceByUser(c.text);
            if (e.empty()) { toast("Fonte adicionada pelo celular: " + sources.sources().back().manifest.name); }
            else toast("Fonte do celular nao entrou: " + e);
            break;
        }
        }
        dirty();
    }
    bool prompt = keyboard.active();
    std::string key = current().title() + (prompt ? "|" + keyboard.title() + "|" + keyboard.text() : "");
    if (key != remoteState_) {
        remoteState_ = key;
        remote->setState(current().title(), prompt, prompt ? keyboard.title() : "", prompt ? keyboard.text() : "", false);
    }
}

void App::handleInput(uint32_t p)
{
    if (keyboard.active()) {
        Keyboard::Result r = keyboard.handle(p);
        if (r == Keyboard::Result::Done && kbDone_) { auto cb = kbDone_; kbDone_ = nullptr; cb(keyboard.text()); }
        if (r == Keyboard::Result::Cancelled) kbDone_ = nullptr;
        return;
    }
    if (modal_.open) {
        int n = int(modal_.buttons.size());
        if (p & BtnLeft) modal_.sel = (modal_.sel + n - 1) % n;
        if (p & BtnRight) modal_.sel = (modal_.sel + 1) % n;
        if (p & (BtnCross | BtnCircle)) {
            int choice = (p & BtnCross) ? modal_.sel : -1;
            modal_.open = false;
            if (modal_.cb) { auto cb = modal_.cb; modal_.cb = nullptr; cb(choice); }
        }
        return;
    }
    if (sidebarFocus_) {
        if (p & BtnUp) goSection(step(Section(sidebarSel_), -1), false);
        else if (p & BtnDown) goSection(step(Section(sidebarSel_), +1), false);
        if (p & (BtnRight | BtnCross)) { sidebarFocus_ = false; current().onEnter(); }
        return;
    }
    if (current().handle(p)) return;
    if (p & BtnCircle) {
        if (!stack_.empty()) pop();
        else focusSidebar();
        return;
    }
    if ((p & BtnLeft) && stack_.empty()) { focusSidebar(); return; }
    if (p & (BtnL2 | BtnR2)) {   // global section switch
        goSection(step(section_, (p & BtnR2) ? 1 : -1), true);
    }
}

bool App::videoDemo() const
{
    if (!settings.showDemo) return false;
    for (const auto &s : sources.sources()) if (s.enabled) return false;
    return true;
}

Section App::step(Section from, int dir) const
{
    int n = int(Section::Count), s = int(from);
    do { s = (s + (dir > 0 ? 1 : n - 1)) % n; } while (!visible(Section(s)));
    return Section(s);
}

// ---------------------------------------------------------------- drawing

void App::drawSidebar(Canvas &c)
{
    // Floating glass pill with icon-only buttons (the new interface). Labels are dropped to match the
    // design; the current section is also shown in each screen's own header, so the place stays clear.
    const int px = 18, pw = 96, pr = 48;
    const int pcx = px + pw / 2;
    const int top = 24, bot = theme::H - 24;
    // No soft shadow on the whole pill: blurring a 96x1032 region every frame was a big cost on the
    // PS4 CPU during animations. The border below gives enough separation.
    c.fillRoundRect(px, top, pw, bot - top, pr, theme::Surface);
    c.strokeRoundRect(px, top, pw, bot - top, pr, 1, theme::Line);

    // moon mark at the top of the pill
    {
        int mx = pcx - 24, my = top + 24;
        c.shadow(mx, my, 48, 48, 24, 16, withAlpha(theme::Violet, 90));
        c.fillGradient(mx, my, 48, 48, 24, theme::Glow, theme::Blue, 2);
        c.fillCircle(float(mx + 34), float(my + 15), 20, theme::Bg);
        c.fillCircle(float(mx + 38), float(my + 38), 2.2f, rgba(255, 255, 255, 200));
    }

    int y = top + 112;
    const int step = 72, btn = 60;
    for (const auto &it : kNav) {
        if (!visible(it.s)) continue;
        bool cur = it.s == section_;
        bool foc = sidebarFocus_ && int(it.s) == sidebarSel_;
        int bx = pcx - btn / 2, by = y;
        if (foc) {
            c.shadow(bx, by, btn, btn, 22, 16, withAlpha(theme::Violet, 130));
            c.fillGradient(bx, by, btn, btn, 22, theme::Violet, theme::Blue, 2);
        } else if (cur) {
            c.fillRoundRect(bx, by, btn, btn, 22, theme::SurfaceHi);
        }
        drawIcon(c, it.icon, float(pcx), float(by + btn / 2), 26, (foc || cur) ? theme::Text : theme::TextDim);
        y += step;
    }

    // compact now-playing chip at the bottom of the pill
    size_t t = queue.current();
    bool liveOn = audio.live() && (audio.state() == PlayState::Playing || audio.state() == PlayState::Paused);
    bool musicOn = (audio.state() == PlayState::Playing || audio.state() == PlayState::Paused) && t < music.tracks().size();
    if (liveOn || musicOn) {
        int cy = bot - 90;
        c.fillRoundRect(pcx - 34, cy, 68, 68, 22, theme::SurfaceHi);
        Icon ic = audio.state() == PlayState::Paused ? Icon::Pause : (liveOn ? Icon::Live : Icon::Music);
        drawIcon(c, ic, float(pcx), float(cy + 28), 24, theme::Glow);
        double frac = 1;
        if (musicOn) { double d = double(audio.getDuration()); frac = d > 0 ? audio.getPosition() / d : 0; }
        drawProgress(c, pcx - 24, cy + 52, 48, 5, frac, theme::Blue, withAlpha(theme::Text, 40));
    }
}

void App::drawModal(Canvas &c)
{
    c.fillRect(0, 0, c.width(), c.height(), rgba(0, 0, 0, 160));
    int w = 900;
    auto lines = fonts.regular->wrap(26, modal_.body, w - 120, 9);
    int h = 230 + int(lines.size()) * fonts.regular->lineHeight(26);
    int x = (c.width() - w) / 2, y = (c.height() - h) / 2;
    c.shadow(x, y, w, h, 28, 40, rgba(0, 0, 0, 170));
    c.fillRoundRect(x, y, w, h, 28, theme::Surface);
    c.strokeRoundRect(x, y, w, h, 28, 1, theme::Line);
    fonts.bold->draw(c, x + 60, y + 44, 34, modal_.title, theme::Text);
    int ly = y + 108;
    for (const auto &l : lines) { fonts.regular->draw(c, x + 60, ly, 26, l, theme::TextDim); ly += fonts.regular->lineHeight(26); }
    int bx = x + w - 60;
    for (int i = int(modal_.buttons.size()) - 1; i >= 0; i--) {
        int bw = fonts.bold->measure(24, modal_.buttons[size_t(i)]) + 64;
        bx -= bw;
        bool f = i == modal_.sel;
        if (f) c.fillGradient(bx, y + h - 96, bw, 56, 28, theme::Violet, theme::Blue, 0);
        else c.fillRoundRect(bx, y + h - 96, bw, 56, 28, theme::SurfaceHi);
        fonts.bold->draw(c, bx + 32, y + h - 96 + (56 - fonts.bold->lineHeight(24)) / 2, 24, modal_.buttons[size_t(i)], theme::Text);
        bx -= 16;
    }
}

void App::draw(Canvas &c)
{
    // background: night gradient + faint moon glow in the corner (rendered once, then copied)
    if (background_.empty()) {
        background_.w = theme::W;
        background_.h = theme::H;
        background_.px.assign(size_t(theme::W) * theme::H, 0xFF000000u);
        Canvas bc(background_.fb());
        bc.fillGradient(0, 0, theme::W, theme::H, 0, theme::BgTop, theme::Bg, 1);
        // dual radial glow: violet top-right, blue bottom-left (soft concentric discs)
        for (int i = 0; i < 7; i++) bc.fillCircle(1770, 30, 560.0f - i * 68, rgba(139, 108, 255, 9));
        for (int i = 0; i < 7; i++) bc.fillCircle(90, 1050, 470.0f - i * 66, rgba(79, 140, 255, 7));
    }
    c.drawImage(background_, 0, 0);
    if (current().fullscreen()) {   // video player: the screen owns the whole display
        current().draw(c, {0, 0, theme::W, theme::H});
        if (!toast_.text.empty()) {
            int w = std::min(1100, fonts.regular->measure(24, toast_.text) + 80);
            c.fillRoundRect(theme::W - 56 - w, 60, w, 70, 20, withAlpha(theme::SurfaceHi, 230));
            fonts.regular->draw(c, theme::W - 56 - w + 30, 60 + (70 - fonts.regular->lineHeight(24)) / 2, 24, toast_.text, theme::Text);
        }
        if (modal_.open) drawModal(c);
        return;
    }
    drawSidebar(c);
    Rect area{theme::SidebarW, 0, theme::W - theme::SidebarW, theme::H - 72};
    c.pushClip(area);
    current().draw(c, area);
    c.popClip();
    if (fade_ > 0) c.fillRect(area.x, area.y, area.w, area.h, withAlpha(theme::Bg, int(fade_ * 200)));
    // hint bar
    c.fillRect(theme::SidebarW, theme::H - 72, theme::W - theme::SidebarW, 72, rgba(8, 8, 20, 230));
    c.fillRect(theme::SidebarW, theme::H - 72, theme::W - theme::SidebarW, 1, theme::Line);
    Hints h = sidebarFocus_ ? Hints{{BtnUp | BtnDown | BtnLeft | BtnRight, "Navegar"}, {BtnCross, "Entrar"}}
                            : current().hints();
    bool usesL2R2 = false;
    for (const auto &x : h) if (x.first & (BtnL2 | BtnR2)) usesL2R2 = true;
    if (!sidebarFocus_ && stack_.empty() && !usesL2R2) h.push_back({BtnL2 | BtnR2, "Secao"});
    drawHints(c, fonts, h, theme::W - theme::Margin, theme::H - 54);
    if (!sidebarFocus_ || true) fonts.regular->draw(c, theme::SidebarW + theme::Margin, theme::H - 50, 20, current().title(), theme::TextMute);
    if (!toast_.text.empty()) {
        int w = std::min(1100, fonts.regular->measure(24, toast_.text) + 80);
        int x = theme::W - theme::Margin - w, y = theme::H - 170;
        c.shadow(x, y, w, 70, 20, 24, rgba(0, 0, 0, 150));
        c.fillRoundRect(x, y, w, 70, 20, theme::SurfaceHi);
        c.fillRoundRect(x, y + 18, 5, 34, 2, theme::Glow);
        fonts.regular->draw(c, x + 30, y + (70 - fonts.regular->lineHeight(24)) / 2, 24, fonts.regular->ellipsize(24, toast_.text, w - 50), theme::Text);
    }
    if (modal_.open) drawModal(c);
    keyboard.draw(c, fonts);
}

}  // namespace ml
