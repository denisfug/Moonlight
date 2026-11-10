#include "app/app.hpp"

#include <algorithm>
#include <cmath>

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
      radio(p.http()), radioFavorites(store), images(p, p.http()), remote(new RemoteControl(p)),
      updater(new Updater(p, "denisfug/Moonlight", ML_VERSION))
{
}

App::~App()
{
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
    loadSettings();
    library.load();
    sources.load();
    radioFavorites.load();
    server = subsonic::loadConfig(store);
    rescanMusic();
    loadChannels();
    audio.setVolume(settings.volume);
    images.start();

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
    now_ = last_ = platform.nowMs();
    if (settings.remote) {
        std::string e;
        if (remote->start(&e)) toast("Controle pelo celular: " + remote->address() + "  PIN " + remote->pin());
        else toast("Controle pelo celular nao ligou: " + e);
    }
    if (platform.name() == "ps4") updater->check();   // quiet unless a new version exists
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

void App::playVideo(const std::string &location, const std::string &title)
{
    std::unique_ptr<VideoEngine> engine = platform.makeVideoEngine();
    if (!engine) { modal("Player de video", VideoPlayer::unavailableReason()); return; }
    audio.stop();   // one audio source at a time
    push(makePlayer(*this, std::move(engine), location, title));
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
    if (fade_ > 0) { fade_ = std::max(0.0f, fade_ - dt * 6.0f); dirty(); }
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
static void drawLogo(App &app, Canvas &c, int x, int y)
{
    // crescent: violet->blue disc with a background-coloured disc cut out
    c.shadow(x, y, 56, 56, 28, 22, withAlpha(theme::Violet, 90));
    c.fillGradient(x, y, 56, 56, 28, theme::Glow, theme::Blue, 2);
    c.fillCircle(x + 40, y + 18, 24, theme::Sidebar);
    c.fillCircle(x + 46, y + 44, 2.5f, rgba(255, 255, 255, 200));
    app.fonts.bold->draw(c, x + 72, y + 2, 30, "MOONLIGHT", theme::Text);
    int w = app.fonts.bold->measure(30, "MOONLIGHT");
    c.fillGradient(x + 72, y + 40, 46, 22, 11, theme::Violet, theme::Blue, 0);
    app.fonts.bold->draw(c, x + 82, y + 41, 16, "HB", rgba(255, 255, 255));
    (void)w;
}

void App::drawSidebar(Canvas &c)
{
    c.fillRect(0, 0, theme::SidebarW, theme::H, theme::Sidebar);
    c.fillRect(theme::SidebarW - 1, 0, 1, theme::H, theme::Line);
    drawLogo(*this, c, 32, 44);
    int y = 170;
    for (const auto &it : kNav) {
        if (!visible(it.s)) continue;
        bool cur = it.s == section_;
        bool foc = sidebarFocus_ && int(it.s) == sidebarSel_;
        Rect r{20, y, theme::SidebarW - 40, 64};
        if (foc) {
            c.shadow(r.x, r.y, r.w, r.h, 18, 18, withAlpha(theme::Violet, 120));
            c.fillGradient(r.x, r.y, r.w, r.h, 18, theme::Violet, theme::Indigo, 0);
        } else if (cur) {
            c.fillRoundRect(r.x, r.y, r.w, r.h, 18, theme::SurfaceHi);
            c.fillRoundRect(r.x, r.y + 16, 5, 32, 2, theme::Glow);
        }
        Color col = foc ? theme::Text : (cur ? theme::Text : theme::TextDim);
        drawIcon(c, it.icon, float(r.x + 42), float(r.y + 32), 28, col);
        (foc || cur ? fonts.bold : fonts.regular)->draw(c, r.x + 78, r.y + (64 - fonts.regular->lineHeight(25)) / 2, 25, it.label, col);
        y += 74;
    }
    // now playing mini status
    size_t t = queue.current();
    bool liveOn = audio.live() && (audio.state() == PlayState::Playing || audio.state() == PlayState::Paused);
    if (liveOn) {
        Rect r{20, theme::H - 150, theme::SidebarW - 40, 112};
        c.fillRoundRect(r.x, r.y, r.w, r.h, 18, theme::Surface);
        drawIcon(c, audio.state() == PlayState::Playing ? Icon::Live : Icon::Pause, float(r.x + 34), float(r.y + 38), 24, theme::Glow);
        fonts.bold->draw(c, r.x + 62, r.y + 18, 20, fonts.bold->ellipsize(20, station.name, r.w - 76), theme::Text);
        fonts.regular->draw(c, r.x + 62, r.y + 46, 17, "AO VIVO  -  " + std::to_string(station.bitrate) + " kbps", theme::TextDim);
        c.fillRoundRect(r.x + 18, r.y + 86, r.w - 36, 6, 3, theme::Violet);
    } else if ((audio.state() == PlayState::Playing || audio.state() == PlayState::Paused) && t < music.tracks().size()) {
        Rect r{20, theme::H - 150, theme::SidebarW - 40, 112};
        c.fillRoundRect(r.x, r.y, r.w, r.h, 18, theme::Surface);
        drawIcon(c, audio.state() == PlayState::Playing ? Icon::Music : Icon::Pause, float(r.x + 34), float(r.y + 38), 24, theme::Glow);
        fonts.bold->draw(c, r.x + 62, r.y + 18, 20, fonts.bold->ellipsize(20, music.tracks()[t].title, r.w - 76), theme::Text);
        fonts.regular->draw(c, r.x + 62, r.y + 46, 17, fonts.regular->ellipsize(17, music.tracks()[t].artist, r.w - 76), theme::TextDim);
        double d = double(audio.getDuration());
        drawProgress(c, r.x + 18, r.y + 86, r.w - 36, 6, d > 0 ? audio.getPosition() / d : 0, theme::Blue, theme::SurfaceHi);
    } else {
        fonts.regular->draw(c, 32, theme::H - 56, 18, std::string("v") + ML_VERSION + "  -  " + platform.name(), theme::TextMute);
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
        bc.fillCircle(1780, -40, 260, rgba(139, 92, 246, 14));
        bc.fillCircle(1780, -40, 170, rgba(139, 92, 246, 12));
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
