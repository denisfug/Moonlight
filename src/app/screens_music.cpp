// Music: playlists, track list, search, and the now-playing panel (progress, volume, shuffle, repeat).
#include <algorithm>
#include <cmath>

#include "app/app.hpp"
#include "app/multi_fetch.hpp"
#include "music/archive.hpp"
#include "music/audius.hpp"
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
        std::string s = "albums=" + std::to_string(arch_.size()) + " songs=" + std::to_string(results_.size()) + " trend=" +
                        std::to_string(trend_.tracks.size()) + " list=" + std::to_string(pl_) + " zone=" + std::to_string(zone_);
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
    // Last row: "Em alta no Audius" (most played of the week), loaded when it is first selected.
    bool trendRow() const { return app.settings.audius && app.platform.http() != nullptr; }
    size_t lists() const { return app.music.playlists().size() + (search_.empty() ? 0 : 1) + size_t(albums()) + (trendRow() ? 1 : 0); }
    bool isTrend(int i) const { return trendRow() && i == int(lists()) - 1; }
    // Index into arch_ when row i is an Internet Archive album, else -1.
    int albumAt(int i) const
    {
        if (search_.empty()) return -1;
        i--;
        return i >= 0 && i < albums() ? i : -1;
    }

    std::vector<size_t> tracksOf(int i) const
    {
        if (isTrend(i)) return trend_.tracks;
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
        if (isTrend(i)) return "Em alta no Audius";
        if (!search_.empty()) {
            if (i == 0) return "Musicas: " + search_;
            i--;
            if (i < albums()) return arch_[size_t(i)].item.title;
            i -= albums();
        }
        return app.music.playlists()[size_t(i)].name;
    }
    std::string countOf(int i) const
    {
        if (isTrend(i)) {
            switch (trend_.state) {
            case Album::Loading: return "carregando...";
            case Album::Loaded: return std::to_string(trend_.tracks.size()) + " faixas - mais tocadas da semana";
            case Album::Failed: return "nao carregou (X tenta de novo)";
            default: return "mais tocadas da semana";
            }
        }
        int a = albumAt(i);
        if (a < 0 && i == 0 && !search_.empty() && searching()) return std::to_string(tracksOf(i).size()) + " faixas - procurando...";
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

    bool searching() const { return archSearch_ != nullptr || songSearch_ != nullptr; }

    // Songs of an online source become library tracks (streamed, never saved). Returns their indices.
    std::vector<size_t> addSongs(const std::vector<catalog::Song> &songs)
    {
        std::vector<music::Track> tracks;
        for (const auto &s : songs) {
            music::Track t;
            t.url = s.url;
            t.title = s.title;
            t.artist = s.artist;
            t.album = s.album;
            t.art = s.art;
            t.source = s.source;
            t.durationMs = int64_t(s.durationSec) * 1000;
            tracks.push_back(std::move(t));
        }
        return app.music.addRemote("", tracks, false);
    }

    std::shared_ptr<MultiFetch> fetchOne(const char *label, const std::string &url, const std::string &base)
    {
        MultiFetch::Item it;
        it.label = label;
        it.url = url;
        it.policy.allowPlainHttpRemote = base.compare(0, 7, "http://") == 0;   // tests only (local fixture server)
        return MultiFetch::start(app.platform, {it});
    }

    // Online part of a search, all in background threads: songs from Audius, albums from the Archive.
    void startOnlineSearch(const std::string &q)
    {
        arch_.clear();
        archSearch_.reset();
        songSearch_.reset();
        if (q.empty() || !app.platform.http()) return;
        if (app.settings.archive) archSearch_ = fetchOne(archive::kName, archive::searchUrl(q, 30, app.archiveBase), app.archiveBase);
        if (app.settings.audius) songSearch_ = fetchOne(audius::kName, audius::searchUrl(q, app.audiusBase), app.audiusBase);
    }

    void openTrend()
    {
        if (trend_.state == Album::Loading || trend_.state == Album::Loaded) return;
        trend_.fetch = fetchOne(audius::kName, audius::trendingUrl(app.audiusBase), app.audiusBase);
        trend_.state = Album::Loading;
    }

    static std::string keyOf(const std::string &title, const std::string &artist)
    {
        std::string k = title + "|" + artist;
        for (char &ch : k) if (ch >= 'A' && ch <= 'Z') ch = char(ch - 'A' + 'a');
        return k;
    }

    void searchFinished()
    {
        if (searching()) return;
        if (results_.empty() && arch_.empty()) app.toast("Nada encontrado para \"" + search_ + "\"");
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
                if (st == MultiFetch::State::Done) {
                    for (auto &it : archive::parseSearch(j)) {
                        Album al;
                        al.item = std::move(it);
                        arch_.push_back(std::move(al));
                    }
                } else {
                    app.toast(std::string(archive::kName) + ": " + e);
                }
                searchFinished();
                app.dirty();
            }
        }
        if (songSearch_) {
            Json j;
            std::string e;
            MultiFetch::State st = songSearch_->poll(0, &j, &e, nullptr);
            if (st != MultiFetch::State::Pending) {
                songSearch_.reset();
                if (st == MultiFetch::State::Done) {
                    // the same song already found on the HD / server / twice in the answer is not repeated
                    std::vector<std::string> seen;
                    for (size_t i : results_) seen.push_back(keyOf(app.music.tracks()[i].title, app.music.tracks()[i].artist));
                    std::vector<catalog::Song> fresh;
                    for (auto &sng : audius::parseTracks(j, app.audiusBase)) {
                        std::string k = keyOf(sng.title, sng.artist);
                        if (std::find(seen.begin(), seen.end(), k) != seen.end()) continue;
                        seen.push_back(k);
                        fresh.push_back(std::move(sng));
                    }
                    bool wasEmpty = results_.empty();
                    auto idx = addSongs(fresh);
                    results_.insert(results_.end(), idx.begin(), idx.end());
                    if (wasEmpty && !results_.empty() && pl_ == 0 && zone_ == 0 && !app.sidebarFocused()) { zone_ = 1; tr_ = 0; }
                } else {
                    app.toast(std::string(audius::kName) + ": " + e);
                }
                searchFinished();
                app.dirty();
            }
        }
        if (trend_.state == Album::Loading && trend_.fetch) {
            Json j;
            std::string e;
            MultiFetch::State st = trend_.fetch->poll(0, &j, &e, nullptr);
            if (st != MultiFetch::State::Pending) {
                trend_.fetch.reset();
                if (st == MultiFetch::State::Done) trend_.tracks = addSongs(audius::parseTracks(j, app.audiusBase));
                trend_.state = trend_.tracks.empty() ? Album::Failed : Album::Loaded;
                if (st != MultiFetch::State::Done) app.toast(std::string(audius::kName) + ": " + e);
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
                al.tracks = addSongs(archive::parseItem(j, app.archiveBase));
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
                startOnlineSearch(q);
                pl_ = 0;
                tr_ = 0;
                zone_ = (search_.empty() || results_.empty()) ? 0 : 1;
                if (!search_.empty()) searchFinished();   // nothing online pending: say so now
            });
            return true;
        }
        int n = int(lists());
        if (zone_ == 0) {
            if (p & BtnUp) { if (pl_ > 0) { pl_--; tr_ = 0; } return true; }
            if (p & BtnDown) { if (pl_ + 1 < n) { pl_++; tr_ = 0; } return true; }
            if (p & (BtnRight | BtnCross)) {
                int a = albumAt(pl_);
                if (isTrend(pl_) && trend_.state != Album::Loaded) openTrend();
                else if (a >= 0 && arch_[size_t(a)].state != Album::Loaded) openAlbum(a);
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
        if (isTrend(pl_) && trend_.state == Album::Closed && !app.sidebarFocused()) openTrend();   // loads when first selected
        pollArchive();
        float target = float(std::max(0, tr_ - (albumAt(pl_) >= 0 ? 4 : 5)) * 74);   // album header leaves fewer rows
        float k = app.settings.animations ? std::min(1.0f, dt * 14.0f) : 1.0f;
        anim_ = std::fabs(target - scroll_) > 0.5f;
        scroll_ = anim_ ? scroll_ + (target - scroll_) * k : target;
    }
    bool animating() const override { return anim_ || serverPending_; }
    bool busy() const override
    {
        if (searching() || trend_.state == Album::Loading) return true;
        for (const auto &al : arch_) if (al.state == Album::Loading) return true;
        return false;
    }

    void draw(Canvas &c, Rect area) override
    {
        size_t localTracks = 0;
        for (const auto &t : app.music.tracks()) if (!t.demo && !t.remote) localTracks++;
        std::string sub = localTracks ? std::to_string(localTracks) + " faixas no HD/pendrive" : "Nenhum MP3 local";
        if (searching()) sub += "  -  procurando online...";
        if (serverPending_) { sub += "  -  conectando ao servidor..."; drawnLoading_ = true; }
        else if (app.server.configured()) sub += "  -  servidor " + app.serverStatus;
        else sub += "  -  configure seu servidor (Navidrome) em Configuracoes";
        drawSectionHeader(app, c, area, "Musica", sub);
        int x = area.x + theme::Margin, top = 176, bottomPanel = 170;
        int listH = area.h - top - bottomPanel - 20;
        // playlists column
        int pw = 420;
        c.fillRoundRect(x, top, pw, listH, 22, theme::Surface);
        app.fonts.bold->draw(c, x + 26, top + 22, 22, search_.empty() ? "PLAYLISTS" : "RESULTADOS", theme::TextMute);
        // The list follows the selection (a search brings up to 30 albums; before, rows past the panel
        // could be selected without being visible).
        int rows = std::max(1, (listH - 66 - 8) / 66);
        int first = std::clamp(pl_ - rows + 2, 0, std::max(0, int(lists()) - rows));
        int y = top + 66;
        for (int i = first; i < int(lists()) && i < first + rows; i++, y += 66) {
            bool on = i == pl_;
            Rect r{x + 12, y, pw - 24, 58};
            if (on && zone_ == 0 && !app.sidebarFocused()) c.fillGradient(r.x, r.y, r.w, r.h, 14, theme::Violet, theme::Indigo, 0);
            else if (on) c.fillRoundRect(r.x, r.y, r.w, r.h, 14, theme::SurfaceHi);
            const Image *cover = nullptr;
            if (int a = albumAt(i); a >= 0) cover = app.images.get(archive::coverUrl(arch_[size_t(a)].item.id, app.archiveBase), 44, 44);
            if (cover) c.drawImage(*cover, r.x + 8, r.y + 7, 8);
            else drawGeneratedArt(c, app.fonts, {r.x + 8, r.y + 7, 44, 44}, 8, "pl:" + nameOf(i), "", false);
            app.fonts.bold->draw(c, r.x + 66, r.y + 6, 22, app.fonts.bold->ellipsize(22, nameOf(i), r.w - 80), theme::Text);
            app.fonts.regular->draw(c, r.x + 66, r.y + 32, 17, app.fonts.regular->ellipsize(17, countOf(i), r.w - 80), on ? theme::Text : theme::TextMute);
        }
        // tracks
        int tx = x + pw + 28, tw = area.right() - theme::Margin - tx;
        c.fillRoundRect(tx, top, tw, listH, 22, theme::Surface);
        auto tr = tracksOf(pl_);
        int head = 62;   // height of the panel header; taller for an album (cover + names)
        if (int a = albumAt(pl_); a >= 0) {
            const Album &al = arch_[size_t(a)];
            head = 176;
            Rect art{tx + 26, top + 22, 140, 140};
            if (const Image *img = app.images.get(archive::coverUrl(al.item.id, app.archiveBase), art.w, art.h)) c.drawImage(*img, art.x, art.y, 14);
            else drawGeneratedArt(c, app.fonts, art, 14, "pl:" + al.item.title, "", false);
            int hx = art.right() + 26, hw = tw - (hx - tx) - 26;
            app.fonts.regular->draw(c, hx, top + 30, 17, "ALBUM  -  INTERNET ARCHIVE", theme::TextMute);
            app.fonts.bold->draw(c, hx, top + 56, 34, app.fonts.bold->ellipsize(34, al.item.title, hw), theme::Text);
            app.fonts.regular->draw(c, hx, top + 104, 22, app.fonts.regular->ellipsize(22, al.item.creator.empty() ? "Artista nao informado" : al.item.creator, hw), theme::TextDim);
            if (al.state == Album::Loaded) app.fonts.regular->draw(c, hx, top + 136, 19, std::to_string(al.tracks.size()) + " faixas", theme::TextMute);
        } else {
            app.fonts.bold->draw(c, tx + 26, top + 22, 22, lists() ? nameOf(pl_) : "", theme::TextMute);
        }
        if (int a = albumAt(pl_); a >= 0 && arch_[size_t(a)].state != Album::Loaded) {
            const Album &al = arch_[size_t(a)];
            app.fonts.regular->draw(c, tx + 26, top + head + 18, 22,
                                    al.state == Album::Loading ? "Abrindo o album no Internet Archive..."
                                    : al.state == Album::Failed ? "Nao foi possivel abrir este album. Aperte X para tentar de novo."
                                                                : "Album do Internet Archive. Aperte X para ver as faixas.",
                                    theme::TextDim);
        }
        c.pushClip({tx, top + head, tw, listH - head - 8});
        int ty = top + head + 4 - int(scroll_);
        size_t playing = app.queue.current();
        bool inAlbum = albumAt(pl_) >= 0;
        if (isTrend(pl_) && trend_.state != Album::Loaded)
            app.fonts.regular->draw(c, tx + 26, top + head + 18, 22,
                                    trend_.state == Album::Failed ? "Nao foi possivel carregar. Aperte X para tentar de novo." : "Carregando as mais tocadas da semana no Audius...",
                                    theme::TextDim);
        for (int i = 0; i < int(tr.size()); i++, ty += 74) {
            if (ty + 74 < top + head || ty > top + listH) continue;
            const music::Track &t = app.music.tracks()[tr[size_t(i)]];
            bool f = zone_ == 1 && i == tr_ && !app.sidebarFocused();
            bool isPlaying = tr[size_t(i)] == playing && app.audio.state() != PlayState::Idle && !t.demo;
            Rect r{tx + 12, ty, tw - 24, 66};
            if (f) { c.fillRoundRect(r.x, r.y, r.w, r.h, 14, theme::SurfaceHi); c.strokeRoundRect(r.x, r.y, r.w, r.h, 14, 2, rgba(196, 181, 253)); }
            if (isPlaying) drawIcon(c, app.audio.state() == PlayState::Playing ? Icon::Music : Icon::Pause, float(r.x + 30), float(r.y + 33), 22, theme::Glow);
            else app.fonts.regular->draw(c, r.x + 18, r.y + 20, 20, std::to_string(i + 1), theme::TextMute);
            // Online songs in a mixed list show their own cover (or a neutral square when the source has
            // none: never a picture of something else). Inside an album the cover is in the header.
            int textX = r.x + 66;
            if (!t.source.empty() && !inAlbum) {
                Rect art{r.x + 58, r.y + 8, 50, 50};
                const Image *img = t.art.empty() ? nullptr : app.images.get(t.art, art.w, art.h);
                if (img) c.drawImage(*img, art.x, art.y, 8);
                else { c.fillRoundRect(art.x, art.y, art.w, art.h, 8, theme::SurfaceHi); drawIcon(c, Icon::Music, float(art.x + 25), float(art.y + 25), 20, theme::TextMute); }
                textX = r.x + 124;
            }
            int textW = r.x + r.w / 2 - 20 - textX;
            app.fonts.bold->draw(c, textX, r.y + 8, 23, app.fonts.bold->ellipsize(23, t.title, textW), isPlaying ? theme::Glow : theme::Text);
            app.fonts.regular->draw(c, textX, r.y + 38, 18, app.fonts.regular->ellipsize(18, t.artist, textW), theme::TextDim);
            if (t.source.empty() || inAlbum) {
                app.fonts.regular->draw(c, r.x + r.w / 2, r.y + 22, 19, app.fonts.regular->ellipsize(19, t.album, r.w / 2 - 140), theme::TextMute);
            } else {
                app.fonts.regular->draw(c, r.x + r.w / 2, r.y + 10, 19, app.fonts.regular->ellipsize(19, t.album, r.w / 2 - 140), theme::TextMute);
                app.fonts.regular->draw(c, r.x + r.w / 2, r.y + 38, 16, t.source, theme::Indigo);
            }
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
    std::shared_ptr<MultiFetch> archSearch_;  // albums (Internet Archive)
    std::shared_ptr<MultiFetch> songSearch_;  // songs (Audius)
    Album trend_;                             // "Em alta no Audius" (item unused)

    void drawNowPlaying(Canvas &c, Rect r)
    {
        c.fillGradient(r.x, r.y, r.w, r.h, 24, rgba(36, 28, 78), rgba(18, 30, 70), 0);
        c.strokeRoundRect(r.x, r.y, r.w, r.h, 24, 1, theme::Line);
        size_t t = app.queue.current();
        bool active = t < app.music.tracks().size() && app.audio.state() != PlayState::Idle && app.audio.state() != PlayState::Error;
        Rect art{r.x + 20, r.y + 20, r.h - 40, r.h - 40};
        const Image *cover = active && !app.music.tracks()[t].art.empty() ? app.images.get(app.music.tracks()[t].art, art.w, art.h) : nullptr;
        if (cover) c.drawImage(*cover, art.x, art.y, 14);
        else drawGeneratedArt(c, app.fonts, art, 14, active ? "al:" + app.music.tracks()[t].album : "idle", "", false);
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
