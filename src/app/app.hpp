// Application shell: services, sidebar navigation, screen stack, modal/toast/keyboard overlays,
// and the frame loop. Screens only talk to the platform through App's services.
#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "addons/source_manager.hpp"
#include "library/library.hpp"
#include "live/m3u.hpp"
#include "music/music.hpp"
#include "music/taste.hpp"
#include "platform/platform.hpp"
#include "player/player.hpp"
#include "radio/radio.hpp"
#include "app/problem_log.hpp"
#include "app/remote.hpp"
#include "app/updater.hpp"
#include "music/subsonic.hpp"
#include "music/ytauth.hpp"
#include "storage/store.hpp"
#include "ui/canvas.hpp"
#include "ui/font.hpp"
#include "ui/image_loader.hpp"
#include "ui/input_map.hpp"
#include "ui/widgets.hpp"

namespace ml {

class App;

using Hints = std::vector<std::pair<uint32_t, std::string>>;

class Screen {
public:
    explicit Screen(App &app) : app(app) {}
    virtual ~Screen() = default;
    virtual std::string title() const = 0;
    virtual void onEnter() {}
    // Returns true when the event was used; unused Circle on a pushed screen pops it,
    // unused Left at the content edge / Circle on a section root moves focus to the sidebar.
    virtual bool handle(uint32_t pressed) = 0;
    virtual void update(float dt) {}
    virtual bool animating() const { return false; }   // something is moving: redraw every frame
    // Waiting for the network with nothing moving: the screen is redrawn a few times a second only,
    // so the software renderer does not compete with the download/TLS threads for the CPU.
    virtual bool busy() const { return false; }
    virtual void draw(ui::Canvas &c, ui::Rect area) = 0;
    virtual Hints hints() const { return {{BtnCross, "Selecionar"}, {BtnCircle, "Voltar"}}; }
    virtual bool fullscreen() const { return false; }   // true: no sidebar/hint bar (video player)
    virtual std::string describe() const { return ""; }   // short machine-readable state (tests)

protected:
    App &app;
};

enum class Section { Home, Music, Radio, Movies, Series, Videos, Live, Library, Games, Support, Sources, Settings, Count };

struct Settings {
    bool animations = true;
    bool showDemo = true;
    bool showRadio = false;   // Radios section is opt-in (Configuracoes)
    bool remote = false;      // phone remote server (opt-in)
    bool archive = true;      // music search also asks the Internet Archive (free/public-domain audio)
    bool audius = true;       // music search and "em alta" also ask Audius (independent artists)
    bool deezer = true;       // music search also asks Deezer (30s previews; covers + metadata)
    bool jamendo = true;      // music search also asks Jamendo (Creative Commons, full-length MP3) first
    std::string jamendoClientId;  // empty = Jamendo disabled; set via Configuracoes or settings.json
    bool openverse = true;    // music search also asks Openverse (Wikimedia CC audio, full-length MP3)
    bool youtube = true;      // music search also asks YouTube Data API v3 (metadata only — no audio stream)
    std::string youtubeApiKey;  // empty = YouTube disabled; user must supply a key from console.cloud.google.com
    bool jiosaavn = true;     // music search also asks JioSaavn (metadata + covers only; no stream)
    bool ytmusic = false;     // music search also asks YouTube Music InnerTube (metadata only; no stream — off by default; no API key needed)
    bool lightTheme = false;  // false = dark (default), true = light; applied with App::applyTheme()
    int volume = 80;
    bool introSound = true;   // play startup audio signature (assets/audio/moonlight_signature.mp3)
    int introVolume = 80;     // 0–100; mixed with main volume
    bool introPlayed = false; // transient: true once per session so it doesn't replay on re-enter
    bool introAnim = true;    // show animated intro vignette on startup
    bool introAnimPlayed = false; // transient: pushed once per session
};

class App {
public:
    explicit App(Platform &p);
    ~App();
    bool init(std::string *error);   // fonts + data; false = fatal
    // One iteration: input -> update -> draw (only when something changed) -> present.
    // Returns false when the app should exit (never on PS4).
    bool frame();
    bool renderedLastFrame() const { return rendered_; }

    // services
    Platform &platform;
    JsonStore store;
    Library library;
    SourceManager sources;
    music::MusicLibrary music;
    music::Queue queue;
    music::Taste taste;        // what was played (local file only): recents + recommendations
    // Shelves of the music home loaded from the network; kept here so leaving and re-entering the
    // section does not ask again.
    struct MusicHome {
        std::vector<size_t> trending, forYou, moreOf;   // indices into music.tracks()
        std::string forYouGenre, moreOfArtist;          // what forYou / moreOf were loaded for
        bool trendingAsked = false;
    } musicHome;
    AudioPlayer audio;
    radio::RadioBrowser radio;
    radio::Favorites radioFavorites;
    radio::Station station;   // station being played when audio.live()
    subsonic::Config server;  // user's music server (Navidrome etc.)
    std::string serverStatus; // "" = not loaded yet
    Settings settings;
    ytauth::Session ytSession;  // shared Google/YTMusic auth session (loaded on startup, saved by ytauth::saveSession)
    ui::Fonts fonts;
    ProblemLog problems;
    // Waiting times of the player and of the video test (tests shorten them), and where the known-good
    // test video lives (published next to the updates by tools/publish_update.sh).
    struct Tuning {
        uint64_t firstFrameMs = 25000;   // player: no picture after this -> reported as a problem
        uint64_t testWaitMs = 15000;     // video test: wait for the first picture
        uint64_t testPlayMs = 4000;      // video test: let it play a little
    } tuning;
    std::string videoTestUrl = "https://raw.githubusercontent.com/denisfug/Moonlight/claude/updates/moonlight_teste.mp4";
    std::string archiveBase = "https://archive.org";   // tests point it at a local fixture server
    std::string audiusBase = "https://api.audius.co";  // idem
    std::string deezerBase  = "https://api.deezer.com"; // idem
    std::string jamendoBase = "https://api.jamendo.com"; // idem (tests override)
    std::string openverseBase = "https://api.openverse.org"; // idem
    std::string youtubeBase = "https://www.googleapis.com"; // idem
    std::string jiosaavnBase = "https://www.jiosaavn.com";  // idem
    std::string ytmusicBase  = "https://music.youtube.com"; // idem
    ui::ImageLoader images;
    ui::Keyboard keyboard;
    std::unique_ptr<RemoteControl> remote;
    std::unique_ptr<Updater> updater;

    // navigation
    void goSection(Section s, bool focusContent);
    Section section() const { return section_; }
    bool visible(Section s) const { return s != Section::Radio || settings.showRadio; }
    Section step(Section from, int dir) const;   // next/previous visible section
    void push(std::unique_ptr<Screen> s);
    void pop();
    bool sidebarFocused() const { return sidebarFocus_; }
    void focusSidebar() { sidebarFocus_ = true; dirty(); }

    // overlays
    void toast(const std::string &text);
    void modal(const std::string &title, const std::string &body, std::vector<std::string> buttons = {"OK"},
               std::function<void(int)> onChoice = nullptr);
    void openKeyboard(const std::string &title, const std::string &initial, bool url,
                      std::function<void(const std::string &)> onDone);
    void dirty() { dirty_ = true; }
    bool modalOpen() const { return modal_.open; }
    const std::string &modalTitle() const { return modal_.title; }   // tests
    const std::string &modalBody() const { return modal_.body; }
    std::string currentTitle() { return current().title(); }
    std::string currentState() { return current().describe(); }
    // Demo movies/series only while there is no active source of the user's.
    bool videoDemo() const;
    // Source the user typed/imported/sent from the phone. An http:// address the user chose himself is
    // accepted (plain HTTP opt-in for that source); TLS verification for https:// is never relaxed.
    // Blocking version (used by remote control, which already runs off the UI thread):
    std::string addSourceByUser(const std::string &url);
    // Non-blocking version: fetches the manifest on a background thread, then calls onDone(error)
    // on the UI thread (via pendingSourceResult_). onDone("") means success. The UI must check
    // drainPendingSource() every frame and call onDone if it returns true.
    void addSourceAsync(const std::string &url,
                        std::function<void(const std::string &)> onDone);
    // Called by App::frame(); returns true and fills *result if a background source add finished.
    bool drainPendingSource(std::string *name, std::string *error);
    int sourcesVersion = 0;   // bumped when sources change, screens reload their catalogs

    // Playback resilience (Ember-style dead-tracks + error-streak guard).
    // deadTracks: URLs that failed permanently this session (e.g. server 404, file removed).
    //   Auto-advance skips these so the queue does not get stuck retrying a broken track.
    // errorStreak: consecutive auto-advance failures. Stops auto-advance after kMaxAutoSkip
    //   to prevent an infinite loop when most tracks in a queue are broken.
    std::unordered_set<std::string> deadTracks;
    int errorStreak = 0;
    static constexpr int kMaxAutoSkip = 3;

    // music helpers shared by screens
    void playTracks(const std::vector<size_t> &tracks, size_t start);
    void togglePause();
    void playStation(const radio::Station &s);
    // Opens the full-screen video player, or explains why video is not available here.
    // ctx.title is what the screen and the problem log show; a failure is recorded with ctx.
    void playVideo(const std::string &location, const PlayContext &ctx);
    void playVideo(const std::string &location, const std::string &title)
    {
        PlayContext ctx;
        ctx.title = title;
        playVideo(location, ctx);
    }
    // Records a playback problem (console clock) and tells the user where to find it.
    void reportProblem(Problem p);
    bool startTrack(size_t t, std::string *err);   // local file or server song
    std::string loadServerMusic();                 // playlists + random mix from the server (blocking)
    std::vector<size_t> searchServer(const std::string &q, std::string *err);
    void setServer(const subsonic::Config &c);
    void skip(int dir);
    void saveSettings();
    void applyTheme();   // settings.lightTheme -> theme::activeTheme (+ redraws the cached background)
    // Play the startup audio signature once per session (no-op if disabled or already played).
    void playIntroSound();
    void rescanMusic();

    // live tv: user list at <dataDir>/channels.m3u, else demo channels
    std::vector<Channel> channels;
    std::string channelsSource;

    uint64_t now() const { return now_; }
    float animT() const { return animT_; }   // seconds since start (for subtle motion)

private:
    Section section_ = Section::Home;
    bool sidebarFocus_ = false;
    int sidebarSel_ = 0;
    std::vector<std::unique_ptr<Screen>> roots_;   // one per section
    std::vector<std::unique_ptr<Screen>> stack_;   // pushed screens on top of the current root
    std::unique_ptr<ui::Font> regular_, bold_;
    ui::InputMap input_;
    bool dirty_ = true, rendered_ = false;
    uint64_t now_ = 0, last_ = 0;
    float animT_ = 0;
    float fade_ = 0;   // screen transition (1 -> 0)
    uint64_t lastDrawMs_ = 0;      // when the last frame was rendered
    bool wasBusy_ = false;         // current().busy() on the previous frame
    bool imagesPending_ = false;   // covers arrived, redraw not done yet (several arrivals share one redraw)
    ui::Image background_;   // pre-rendered page background (copied each frame)

    struct Toast { std::string text; uint64_t until = 0; } toast_;
    struct Modal {
        bool open = false;
        std::string title, body;
        std::vector<std::string> buttons;
        int sel = 0;
        std::function<void(int)> cb;
    } modal_;
    std::function<void(const std::string &)> kbDone_;
    bool systemKeyboard_ = false;   // PS4 system keyboard open: keep presenting frames, poll for the result
    PlayState lastAudioState_ = PlayState::Idle;
    int64_t lastAudioPos_ = -1;
    uint64_t trackStartMs_ = 0;   // now_ when play() was last called; guards against instant auto-advance

    // Async source-add state (addSourceAsync / drainPendingSource).
    // Written by background thread, read on UI thread inside frame().
    struct PendingSource {
        std::atomic<bool> done{false};
        std::string name;   // display name (from manifest title or the URL itself)
        std::string error;  // "" = success
        std::function<void(const std::string &)> cb;
    };
    std::shared_ptr<PendingSource> pendingSource_;

    Screen &current();
    void handleInput(uint32_t pressed);
    void serviceRemote();
    // Start-up diagnostics (see setNetTrace): last risky step in <data>/startup.txt.
    void crumb(const std::string &step);
    std::string crashedAt_;      // step the previous run stopped at ("" = closed normally)
    bool stableWritten_ = false;
    bool updateCheckDone_ = false;
    uint64_t startMs_ = 0;
    std::string remoteState_;
    Updater::Phase lastUpdatePhase_ = Updater::Phase::Idle;
    void draw(ui::Canvas &c);
    void drawSidebar(ui::Canvas &c);
    void drawModal(ui::Canvas &c);
    void loadChannels();
    void loadSettings();
};

// screen factories (screens_*.cpp)
std::unique_ptr<Screen> makeHome(App &);
std::unique_ptr<Screen> makeCatalog(App &, const std::string &type);
std::unique_ptr<Screen> makeDetail(App &, const std::string &type, const std::string &id, const std::string &name);
std::unique_ptr<Screen> makeMusic(App &);
std::unique_ptr<Screen> makeRadio(App &);
std::unique_ptr<Screen> makeVideos(App &);
std::unique_ptr<Screen> makePlayer(App &, std::unique_ptr<VideoEngine> engine, const std::string &location, const PlayContext &ctx);
std::unique_ptr<Screen> makeVideoTest(App &);   // step-by-step check of the system player
std::unique_ptr<Screen> makeProblems(App &);    // Configuracoes > Problemas ao reproduzir
std::unique_ptr<Screen> makeLinkQr(App &, const std::string &title, const std::string &from, const std::string &url);
std::unique_ptr<Screen> makeLive(App &);
std::unique_ptr<Screen> makeLibrary(App &);
std::unique_ptr<Screen> makeSources(App &);
std::unique_ptr<Screen> makeSettings(App &);
std::unique_ptr<Screen> makeGames(App &);
std::unique_ptr<Screen> makeSupport(App &);
std::unique_ptr<Screen> makeIntro(App &);       // animated intro vignette (screens_intro.cpp)
std::unique_ptr<Screen> makeIntroVideo(App &);  // MP4 intro via AvEngine, sem Canvas (screens_intro.cpp)
std::unique_ptr<Screen> makeTester(App &);  // light tester — interactive diagnostics (screens_tester.cpp)

// shared drawing helpers (screens_home.cpp)
void drawPosterCard(App &app, ui::Canvas &c, ui::Rect r, const std::string &seed, const std::string &title,
                    const std::string &subtitle, bool focused, double progress = -1, const std::string &posterUrl = "");
void drawSectionHeader(App &app, ui::Canvas &c, ui::Rect area, const std::string &title, const std::string &subtitle);

}  // namespace ml
