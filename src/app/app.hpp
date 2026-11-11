// Application shell: services, sidebar navigation, screen stack, modal/toast/keyboard overlays,
// and the frame loop. Screens only talk to the platform through App's services.
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "addons/source_manager.hpp"
#include "library/library.hpp"
#include "live/m3u.hpp"
#include "music/music.hpp"
#include "platform/platform.hpp"
#include "player/player.hpp"
#include "radio/radio.hpp"
#include "app/remote.hpp"
#include "app/updater.hpp"
#include "music/subsonic.hpp"
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
    virtual bool animating() const { return false; }
    virtual void draw(ui::Canvas &c, ui::Rect area) = 0;
    virtual Hints hints() const { return {{BtnCross, "Selecionar"}, {BtnCircle, "Voltar"}}; }
    virtual bool fullscreen() const { return false; }   // true: no sidebar/hint bar (video player)
    virtual std::string describe() const { return ""; }   // short machine-readable state (tests)

protected:
    App &app;
};

enum class Section { Home, Music, Radio, Movies, Series, Videos, Live, Library, Sources, Settings, Count };

struct Settings {
    bool animations = true;
    bool showDemo = true;
    bool showRadio = false;   // Radios section is opt-in (Configuracoes)
    bool remote = false;      // phone remote server (opt-in)
    int volume = 80;
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
    AudioPlayer audio;
    radio::RadioBrowser radio;
    radio::Favorites radioFavorites;
    radio::Station station;   // station being played when audio.live()
    subsonic::Config server;  // user's music server (Navidrome etc.)
    std::string serverStatus; // "" = not loaded yet
    Settings settings;
    ui::Fonts fonts;
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
    std::string addSourceByUser(const std::string &url);
    int sourcesVersion = 0;   // bumped when sources change, screens reload their catalogs

    // music helpers shared by screens
    void playTracks(const std::vector<size_t> &tracks, size_t start);
    void togglePause();
    void playStation(const radio::Station &s);
    // Opens the full-screen video player, or explains why video is not available here.
    void playVideo(const std::string &location, const std::string &title,
                   const std::vector<std::pair<std::string, std::string>> &requestHeaders = {});
    bool startTrack(size_t t, std::string *err);   // local file or server song
    std::string loadServerMusic();                 // playlists + random mix from the server (blocking)
    std::vector<size_t> searchServer(const std::string &q, std::string *err);
    void setServer(const subsonic::Config &c);
    void skip(int dir);
    void saveSettings();
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
std::unique_ptr<Screen> makePlayer(App &, std::unique_ptr<VideoEngine> engine, const std::string &location, const std::string &title);
std::unique_ptr<Screen> makeLive(App &);
std::unique_ptr<Screen> makeLibrary(App &);
std::unique_ptr<Screen> makeSources(App &);
std::unique_ptr<Screen> makeSettings(App &);

// shared drawing helpers (screens_home.cpp)
void drawPosterCard(App &app, ui::Canvas &c, ui::Rect r, const std::string &seed, const std::string &title,
                    const std::string &subtitle, bool focused, double progress = -1, const std::string &posterUrl = "");
void drawSectionHeader(App &app, ui::Canvas &c, ui::Rect area, const std::string &title, const std::string &subtitle);

}  // namespace ml
