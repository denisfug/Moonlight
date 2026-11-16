// Music: playlists, track list, search, and the now-playing panel (progress, volume, shuffle, repeat).
#include <algorithm>
#include <cmath>

#include "app/app.hpp"
#include "app/multi_fetch.hpp"
#include "music/archive.hpp"
#include "ui/theme.hpp"

namespace ml {

using namespace ui;

namespace {

class MusicScreen : public Screen {
public:
    using Screen::Screen;
    std::string title() const override { return "Musica"; }
    std::string describe() const override
    {
        std::string s = "albums=" + std::to_string(arch_.size()) + " list=" + std::to_string(pl_) + " zone=" + std::to_string(zone_);
        if (int a = albumAt(pl_); a >= 0) s += " tracks=" + std::to_string(arch_[size_t(a)].tracks.size());
        return s;
    }

    void onEnter() override
    {
        // the user's music server is loaded once per app start (after a "loading" frame)
        if (app.server.configured() && app.serverStatus.empty()) { serverPending_ = true; drawnLoading_ = false; }
        if (pl_ >= int(lists())) pl_ = 0;
    }

    // Left column while a search is active: [0] local/server results, [1..] Internet Archive albums,
    // then the playlists. Without a search: only the playlists.
    int albums() const { return search_.empty() ? 0 : int(arch_.size()); }
    size_t lists() const { return app.music.playlists().size() + (search_.empty() ? 0 : 1) + size_t(albums()); }
    // Index into arch_ when row i is an Internet Archive album, else -1.
    int albumAt(int i) const
    {
        if (search_.empty()) return -1;
        i--;
        return i >= 0 && i < albums() ? i : -1;
    }

    std::vector<size_t> tracksOf(int i) const
    {
        if (!search_.empty()) {
            if (i == 0) return results_;
            i--;
            if (i < albums()) return arch_[size_t(i)].tracks;
            i -= albums();
        }
        if (i < 0 || i >= int(app.music.playlists().size())) return {};
        return app.music.playlists()[size_t(i)].tracks;
    }
    std::string nameOf(int i) const
    {
        if (!search_.empty()) {
            if (i == 0) return "Busca: " + search_;
            i--;
            if (i < albums()) return arch_[size_t(i)].item.title;
            i -= albums();
        }
        return app.music.playlists()[size_t(i)].name;
    }
    std::string countOf(int i) const
    {
        int a = albumAt(i);
        if (a < 0) return std::to_string(tracksOf(i).size()) + " faixas";
        const Album &al = arch_[size_t(a)];
        std::string who = al.item.creator.empty() ? "Internet Archive" : al.item.creator;
        switch (al.state) {
        case Album::Loading: return "abrindo...";
        case Album::Loaded: return std::to_string(al.tracks.size()) + " faixas - " + who;
        case Album::Failed: return "nao abriu (X tenta de novo)";
        default: return who;
        }
    }

    // Asks the Archive for the album's files (background thread); the tracks appear when it answers.
    void openAlbum(int a)
    {
        Album &al = arch_[size_t(a)];
        if (al.state == Album::Loading || al.state == Album::Loaded) return;
        std::string url = archive::metadataUrl(al.item.id, app.archiveBase);
        if (url.empty()) { al.state = Album::Failed; return; }
        MultiFetch::Item it;
        it.label = al.item.id;
        it.url = url;
        it.policy.allowPlainHttpRemote = app.archiveBase.compare(0, 7, "http://") == 0;   // tests only
        al.fetch = MultiFetch::start(app.platform, {it});
        al.state = Album::Loading;
    }

    void startArchiveSearch(const std::string &q)
    {
        arch_.clear();
        archSearch_.reset();
        archBusy_ = false;
        if (q.empty() || !app.settings.archive || !app.platform.http()) return;
        MultiFetch::Item it;
        it.label = "Internet Archive";
        it.url = archive::searchUrl(q, 30, app.archiveBase);
        it.policy.allowPlainHttpRemote = app.archiveBase.compare(0, 7, "http://") == 0;   // tests only
        archSearch_ = MultiFetch::start(app.platform, {it});
        archBusy_ = true;
    }

    // Collects answers of the background requests (called every frame; cheap when nothing is pending).
    void pollArchive()
    {
        if (archSearch_) {
            Json j;
            std::string e;
            MultiFetch::State st = archSearch_->poll(0, &j, &e, nullptr);
            if (st != MultiFetch::State::Pending) {
                archSearch_.reset();
                archBusy_ = false;
                if (st == MultiFetch::State::Done) {
                    for (auto &it : archive::parseSearch(j)) {
                        Album al;
                        al.item = std::move(it);
                        arch_.push_back(std::move(al));
                    }
                    if (arch_.empty() && results_.empty()) app.toast("Nada encontrado para \"" + search_ + "\"");
                } else {
                    app.toast("Internet Archive: " + e);
                }
                app.dirty();
            }
        }
        for (size_t a = 0; a < arch_.size(); a++) {
            Album &al = arch_[a];
            if (al.state != Album::Loading || !al.fetch) continue;
            Json j;
            std::string e;
            MultiFetch::State st = al.fetch->poll(0, &j, &e, nullptr);
            if (st == MultiFetch::State::Pending) continue;
            al.fetch.reset();
            if (st == MultiFetch::State::Done) {
                std::vector<music::Track> tracks;
                for (const auto &s : archive::parseItem(j, app.archiveBase)) {
                    music::Track t;
                    t.url = s.url;
                    t.title = s.title;
                    t.artist = s.artist;
                    t.album = s.album;
                    t.durationMs = int64_t(s.durationSec) * 1000;
                    tracks.push_back(std::move(t));
                }
                al.tracks = app.music.addRemote("", tracks, false);
                al.state = al.tracks.empty() ? Album::Failed : Album::Loaded;
                if (al.tracks.empty()) app.toast("\"" + al.item.title + "\" nao tem faixas em MP3");
                else if (albumAt(pl_) == int(a) && zone_ == 0 && !app.sidebarFocused()) { zone_ = 1; tr_ = 0; }
            } else {
                al.state = Album::Failed;
                app.toast("Internet Archive: " + e);
            }
            app.dirty();
        }
    }

    bool handle(uint32_t p) override
    {
        auto tr = tracksOf(pl_);
        if (p & BtnSquare) { app.togglePause(); return true; }
        if (p & BtnR1) { app.skip(+1); return true; }
        if (p & BtnL1) { app.skip(-1); return true; }
        if (p & (BtnL2 | BtnR2)) {
            app.settings.volume = std::clamp(app.settings.volume + ((p & BtnR2) ? 10 : -10), 0, 100);
            app.audio.setVolume(app.settings.volume);
            app.saveSettings();
            return true;
        }
        if (p & BtnTriangle) {
            app.queue.setShuffle(!app.queue.shuffle(), uint32_t(app.now()));
            app.toast(app.queue.shuffle() ? "Aleatorio ligado" : "Aleatorio desligado");
            return true;
        }
        if (p & BtnR3) {
            music::Repeat r = app.queue.repeat();
            r = r == music::Repeat::All ? music::Repeat::One : r == music::Repeat::One ? music::Repeat::Off : music::Repeat::All;
            app.queue.setRepeat(r);
            app.toast(r == music::Repeat::All ? "Repetir: tudo" : r == music::Repeat::One ? "Repetir: uma" : "Repetir: desligado");
            return true;
        }
        if (p & BtnOptions) {
            app.openKeyboard("Buscar musica", search_, false, [this](const std::string &q) {
                search_ = q;
                results_ = app.music.search(q);
                if (!q.empty() && app.server.configured()) {
                    std::string e;
                    auto remote = app.searchServer(q, &e);
                    results_.insert(results_.end(), remote.begin(), remote.end());
                    if (!e.empty()) app.toast("Busca no servidor: " + e);
                }
                startArchiveSearch(q);
                pl_ = 0;
                tr_ = 0;
                zone_ = (search_.empty() || results_.empty()) ? 0 : 1;
                if (!search_.empty() && results_.empty() && !archBusy_) app.toast("Nada encontrado para \"" + q + "\"");
            });
            return true;
        }
        int n = int(lists());
        if (zone_ == 0) {
            if (p & BtnUp) { if (pl_ > 0) { pl_--; tr_ = 0; } return true; }
            if (p & BtnDown) { if (pl_ + 1 < n) { pl_++; tr_ = 0; } return true; }
            if (p & (BtnRight | BtnCross)) {
                int a = albumAt(pl_);
                if (a >= 0 && arch_[size_t(a)].state != Album::Loaded) openAlbum(a);
                else if (!tr.empty()) zone_ = 1;
                return true;
            }
            return false;
        }
        if (p & BtnUp) { if (tr_ > 0) tr_--; return true; }
        if (p & BtnDown) { if (tr_ + 1 < int(tr.size())) tr_++; return true; }
        if (p & BtnLeft) { zone_ = 0; return true; }
        if (p & BtnCircle) { zone_ = 0; return true; }
        if ((p & BtnCross) && tr_ < int(tr.size())) { app.playTracks(tr, size_t(tr_)); return true; }
        return false;
    }

    void update(float dt) override
    {
        if (serverPending_ && drawnLoading_) {
            serverPending_ = false;
            std::string e = app.loadServerMusic();
            if (!e.empty()) app.toast("Servidor de musica: " + e);
            if (pl_ >= int(lists())) pl_ = 0;
            app.dirty();
        }
        pollArchive();
        float target = float(std::max(0, tr_ - 5) * 74);
        float k = app.settings.animations ? std::min(1.0f, dt * 14.0f) : 1.0f;
        anim_ = std::fabs(target - scroll_) > 0.5f;
        scroll_ = anim_ ? scroll_ + (target - scroll_) * k : target;
    }
    bool animating() const override { return anim_ || serverPending_; }
    bool busy() const override
    {
        if (archBusy_) return true;
        for (const auto &al : arch_) if (al.state == Album::Loading) return true;
        return false;
    }

    void draw(Canvas &c, Rect area) override
    {
        size_t localTracks = 0;
        for (const auto &t : app.music.tracks()) if (!t.demo && !t.remote) localTracks++;
        std::string sub = localTracks ? std::to_string(localTracks) + " faixas no HD/pendrive" : "Nenhum MP3 local";
        if (archBusy_) sub += "  -  procurando no Internet Archive...";
        if (serverPending_) { sub += "  -  conectando ao servidor..."; drawnLoading_ = true; }
        else if (app.server.configured()) sub += "  -  servidor " + app.serverStatus;
        else sub += "  -  configure seu servidor (Navidrome) em Configuracoes";
        drawSectionHeader(app, c, area, "Musica", sub);
        int x = area.x + theme::Margin, top = 176, bottomPanel = 170;
        int listH = area.h - top - bottomPanel - 20;
        // playlists column
        int pw = 420;
        c.fillRoundRect(x, top, pw, listH, 22, theme::Surface);
        app.fonts.bold->draw(c, x + 26, top + 22, 22, "PLAYLISTS", theme::TextMute);
        int y = top + 66;
        for (int i = 0; i < int(lists()) && y < top + listH - 60; i++, y += 66) {
            bool on = i == pl_;
            Rect r{x + 12, y, pw - 24, 58};
            if (on && zone_ == 0 && !app.sidebarFocused()) c.fillGradient(r.x, r.y, r.w, r.h, 14, theme::Violet, theme::Indigo, 0);
            else if (on) c.fillRoundRect(r.x, r.y, r.w, r.h, 14, theme::SurfaceHi);
            drawGeneratedArt(c, app.fonts, {r.x + 8, r.y + 7, 44, 44}, 8, "pl:" + nameOf(i), "", false);
            app.fonts.bold->draw(c, r.x + 66, r.y + 6, 22, app.fonts.bold->ellipsize(22, nameOf(i), r.w - 80), theme::Text);
            app.fonts.regular->draw(c, r.x + 66, r.y + 32, 17, app.fonts.regular->ellipsize(17, countOf(i), r.w - 80), on ? theme::Text : theme::TextMute);
        }
        // tracks
        int tx = x + pw + 28, tw = area.right() - theme::Margin - tx;
        c.fillRoundRect(tx, top, tw, listH, 22, theme::Surface);
        auto tr = tracksOf(pl_);
        app.fonts.bold->draw(c, tx + 26, top + 22, 22, lists() ? nameOf(pl_) : "", theme::TextMute);
        if (int a = albumAt(pl_); a >= 0 && arch_[size_t(a)].state != Album::Loaded) {
            const Album &al = arch_[size_t(a)];
            app.fonts.regular->draw(c, tx + 26, top + 80, 22,
                                    al.state == Album::Loading ? "Abrindo o album no Internet Archive..."
                                    : al.state == Album::Failed ? "Nao foi possivel abrir este album. Aperte X para tentar de novo."
                                                                : "Album do Internet Archive. Aperte X para ver as faixas.",
                                    theme::TextDim);
        }
        c.pushClip({tx, top + 62, tw, listH - 70});
        int ty = top + 66 - int(scroll_);
        size_t playing = app.queue.current();
        for (int i = 0; i < int(tr.size()); i++, ty += 74) {
            if (ty + 74 < top + 60 || ty > top + listH) continue;
            const music::Track &t = app.music.tracks()[tr[size_t(i)]];
            bool f = zone_ == 1 && i == tr_ && !app.sidebarFocused();
            bool isPlaying = tr[size_t(i)] == playing && app.audio.state() != PlayState::Idle && !t.demo;
            Rect r{tx + 12, ty, tw - 24, 66};
            if (f) { c.fillRoundRect(r.x, r.y, r.w, r.h, 14, theme::SurfaceHi); c.strokeRoundRect(r.x, r.y, r.w, r.h, 14, 2, rgba(196, 181, 253)); }
            if (isPlaying) drawIcon(c, app.audio.state() == PlayState::Playing ? Icon::Music : Icon::Pause, float(r.x + 30), float(r.y + 33), 22, theme::Glow);
            else app.fonts.regular->draw(c, r.x + 18, r.y + 20, 20, std::to_string(i + 1), theme::TextMute);
            app.fonts.bold->draw(c, r.x + 66, r.y + 8, 23, app.fonts.bold->ellipsize(23, t.title, r.w / 2 - 80), isPlaying ? theme::Glow : theme::Text);
            app.fonts.regular->draw(c, r.x + 66, r.y + 38, 18, app.fonts.regular->ellipsize(18, t.artist, r.w / 2 - 80), theme::TextDim);
            app.fonts.regular->draw(c, r.x + r.w / 2, r.y + 22, 19, app.fonts.regular->ellipsize(19, t.album, r.w / 2 - 140), theme::TextMute);
            if (t.demo) app.fonts.bold->draw(c, r.right() - 104, r.y + 22, 16, "DEMO", theme::Warn);
            else if (t.remote || !t.url.empty()) app.fonts.regular->draw(c, r.right() - 104, r.y + 22, 17, t.durationMs > 0 ? formatTime(t.durationMs) : "rede", theme::TextMute);
        }
        c.popClip();
        drawNowPlaying(c, {x, area.bottom() - bottomPanel, area.right() - theme::Margin - x, bottomPanel - 16});
    }

    Hints hints() const override
    {
        return {{BtnCross, "Tocar"}, {BtnSquare, "Pausar"}, {BtnL1 | BtnR1, "Faixa"}, {BtnL2 | BtnR2, "Volume"},
                {BtnTriangle, "Aleatorio"}, {BtnR3, "Repetir"}, {BtnOptions, "Buscar"}};
    }

private:
    int pl_ = 0, tr_ = 0, zone_ = 0;
    float scroll_ = 0;
    bool anim_ = false, serverPending_ = false, drawnLoading_ = false;
    std::string search_;
    std::vector<size_t> results_;
    struct Album {
        enum State { Closed, Loading, Loaded, Failed };
        archive::Item item;
        State state = Closed;
        std::vector<size_t> tracks;          // indices into the music library once loaded
        std::shared_ptr<MultiFetch> fetch;   // request for the album's files
    };
    std::vector<Album> arch_;                 // Internet Archive albums of the current search
    std::shared_ptr<MultiFetch> archSearch_;
    bool archBusy_ = false;

    void drawNowPlaying(Canvas &c, Rect r)
    {
        c.fillGradient(r.x, r.y, r.w, r.h, 24, rgba(36, 28, 78), rgba(18, 30, 70), 0);
        c.strokeRoundRect(r.x, r.y, r.w, r.h, 24, 1, theme::Line);
        size_t t = app.queue.current();
        bool active = t < app.music.tracks().size() && app.audio.state() != PlayState::Idle && app.audio.state() != PlayState::Error;
        drawGeneratedArt(c, app.fonts, {r.x + 20, r.y + 20, r.h - 40, r.h - 40}, 14,
                         active ? "al:" + app.music.tracks()[t].album : "idle", "", false);
        int x = r.x + r.h;
        if (!active) {
            app.fonts.bold->draw(c, x, r.y + 30, 26, "Nada tocando", theme::Text);
            app.fonts.regular->draw(c, x, r.y + 70, 20, "Escolha uma faixa e aperte X. A musica para se voce sair do app (veja docs/ROADMAP.md).", theme::TextDim);
        } else {
            const music::Track &tr = app.music.tracks()[t];
            app.fonts.bold->draw(c, x, r.y + 22, 28, app.fonts.bold->ellipsize(28, tr.title, 700), theme::Text);
            app.fonts.regular->draw(c, x, r.y + 60, 21, app.fonts.regular->ellipsize(21, tr.artist + (tr.album.empty() ? "" : "  -  " + tr.album), 700), theme::TextDim);
            int64_t pos = app.audio.getPosition(), dur = app.audio.getDuration();
            drawProgress(c, x, r.y + r.h - 40, 760, 8, dur > 0 ? double(pos) / double(dur) : 0, theme::Blue, rgba(255, 255, 255, 40));
            app.fonts.regular->draw(c, x + 780, r.y + r.h - 52, 19, formatTime(pos) + " / " + formatTime(dur), theme::TextDim);
        }
        // transport + status icons (right side)
        int ix = r.right() - 420;
        float cy = float(r.y + r.h / 2);
        drawIcon(c, Icon::Prev, float(ix), cy, 28, theme::TextDim);
        c.fillCircle(float(ix + 70), cy, 34, theme::Text);
        drawIcon(c, app.audio.state() == PlayState::Playing ? Icon::Pause : Icon::Play, float(ix + 72), cy, 28, theme::Bg);
        drawIcon(c, Icon::Next, float(ix + 140), cy, 28, theme::TextDim);
        drawIcon(c, Icon::Shuffle, float(ix + 210), cy, 26, app.queue.shuffle() ? theme::Glow : theme::TextMute);
        drawIcon(c, Icon::Repeat, float(ix + 262), cy, 26, app.queue.repeat() == music::Repeat::Off ? theme::TextMute : theme::Glow);
        if (app.queue.repeat() == music::Repeat::One) app.fonts.bold->draw(c, ix + 276, int(cy) - 26, 14, "1", theme::Glow);
        drawIcon(c, Icon::Volume, float(ix + 314), cy, 24, theme::TextDim);
        drawProgress(c, ix + 336, int(cy) - 4, 70, 8, app.settings.volume / 100.0, theme::Blue, rgba(255, 255, 255, 40));
    }
};

}  // namespace

std::unique_ptr<Screen> makeMusic(App &a) { return std::unique_ptr<Screen>(new MusicScreen(a)); }

}  // namespace ml
