// Music: a home of shelves (recently played, recommendations, trending, playlists), a search that
// returns songs and albums with covers, a detail view (playlist / album track list) and the
// now-playing panel. Online sources (Audius, Internet Archive) are asked in background threads.
#include <algorithm>
#include <cmath>

#include "app/app.hpp"
#include "app/multi_fetch.hpp"
#include "music/archive.hpp"
#include "music/audius.hpp"
#include "music/deezer.hpp"
#include "music/jamendo.hpp"
#include "music/openverse.hpp"
#include "music/rank.hpp"
#include "ui/theme.hpp"

namespace ml {

using namespace ui;

namespace {

std::string lowered(std::string s)
{
    for (char &ch : s) if (ch >= 'A' && ch <= 'Z') ch = char(ch - 'A' + 'a');
    return s;
}

class MusicScreen : public Screen {
public:
    using Screen::Screen;
    std::string title() const override { return "Musica"; }
    std::string describe() const override
    {
        std::string s = std::string("view=") + (view_ == View::Detail ? "detail" : "shelves") + " row=" + std::to_string(row_) +
                        " col=" + std::to_string(row_ < int(shelves_.size()) ? shelves_[size_t(row_)].col : 0) +
                        " shelves=" + std::to_string(shelves_.size()) + " albums=" + std::to_string(arch_.size()) +
                        " songs=" + std::to_string(results_.size()) + " trend=" + std::to_string(app.musicHome.trending.size()) +
                        " foryou=" + std::to_string(app.musicHome.forYou.size()) + " recent=" + std::to_string(recent_.size());
        if (view_ == View::Detail) s += " tracks=" + std::to_string(detailTracks().size());
        if (row_ < int(shelves_.size())) s += " shelf=" + shelves_[size_t(row_)].title;
        return s;
    }

    void onEnter() override
    {
        // the user's music server is loaded once per app start (after a "loading" frame)
        if (app.server.configured() && app.serverStatus.empty()) { serverPending_ = true; drawnLoading_ = false; }
        enteredMs_ = app.now();
        homeAsked_ = false;
        rebuild();
    }

    bool handle(uint32_t p) override
    {
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
            app.openKeyboard("Buscar musica, artista ou album", search_, false, [this](const std::string &q) { runSearch(q); });
            return true;
        }
        if (view_ == View::Detail) {
            auto tr = detailTracks();
            if (p & BtnUp) { if (detail_.sel > 0) detail_.sel--; return true; }
            if (p & BtnDown) { if (detail_.sel + 1 < int(tr.size())) detail_.sel++; return true; }
            if (p & (BtnCircle | BtnLeft)) { view_ = View::Shelves; return true; }
            if (p & BtnCross) {
                if (detail_.album >= 0 && arch_[size_t(detail_.album)].state == Album::Failed) openAlbum(detail_.album);
                else if (detail_.sel < int(tr.size())) app.playTracks(tr, size_t(detail_.sel));
                return true;
            }
            return true;
        }
        if (p & BtnCircle) {
            if (!search_.empty()) { runSearch(""); return true; }   // leaves the results, back to the home
            return false;
        }
        if (shelves_.empty()) return false;
        Shelf &sh = shelves_[size_t(row_)];
        if (p & (BtnUp | BtnDown | BtnLeft | BtnRight)) moved_ = true;
        if (p & BtnUp) { if (row_ > 0) row_--; return true; }
        if (p & BtnDown) { if (row_ + 1 < int(shelves_.size())) row_++; return true; }
        if (p & BtnLeft) { if (sh.col > 0) { sh.col--; return true; } return false; }   // first card: to the menu
        if (p & BtnRight) { if (sh.col + 1 < int(sh.cards.size())) sh.col++; return true; }
        if ((p & BtnCross) && sh.col < int(sh.cards.size())) {
            const Card &cd = sh.cards[size_t(sh.col)];
            if (cd.kind == Card::Song) {
                app.playTracks(songsOf(sh), size_t(sh.col));   // the shelf becomes the queue
            } else if (cd.kind == Card::Playlist) {
                const auto &pl = app.music.playlists()[cd.ref];
                detail_ = Detail{};
                detail_.title = pl.name;
                detail_.kind = "PLAYLIST";
                detail_.playlist = int(cd.ref);
                view_ = View::Detail;
            } else {
                detail_ = Detail{};
                detail_.album = int(cd.ref);
                detail_.kind = "ALBUM  -  INTERNET ARCHIVE";
                detail_.title = arch_[cd.ref].item.title;
                view_ = View::Detail;
                openAlbum(int(cd.ref));
            }
            return true;
        }
        return false;
    }

    void update(float dt) override
    {
        if (serverPending_ && drawnLoading_) {
            serverPending_ = false;
            std::string e = app.loadServerMusic();
            if (!e.empty()) app.toast("Servidor de musica: " + e);
            rebuild();
            app.dirty();
        }
        // The home asks the network only after the section stayed open for a moment (passing over it
        // in the menu costs nothing), and only once per visit.
        if (!homeAsked_ && app.now() - enteredMs_ > 500) { homeAsked_ = true; startHomeFetches(); }
        poll();
        float k = app.settings.animations ? std::min(1.0f, dt * 14.0f) : 1.0f;
        float target = shelfScrollTarget();
        float dTarget = float(std::max(0, detail_.sel - 3) * 74);
        anim_ = std::fabs(target - scroll_) > 0.5f || std::fabs(dTarget - detail_.scroll) > 0.5f;
        scroll_ = std::fabs(target - scroll_) > 0.5f ? scroll_ + (target - scroll_) * k : target;
        detail_.scroll = std::fabs(dTarget - detail_.scroll) > 0.5f ? detail_.scroll + (dTarget - detail_.scroll) * k : dTarget;
    }
    bool animating() const override { return anim_ || serverPending_; }
    bool busy() const override
    {
        if (searching() || fetchTrend_ || fetchForYou_ || fetchMore_) return true;
        for (const auto &al : arch_) if (al.state == Album::Loading) return true;
        return false;
    }

    void draw(Canvas &c, Rect area) override
    {
        size_t localTracks = 0;
        for (const auto &t : app.music.tracks()) if (!t.demo && !t.remote && t.url.empty()) localTracks++;
        std::string sub;
        if (!search_.empty()) sub = "Resultados para \"" + search_ + "\"" + (searching() ? "  -  procurando online..." : "") + "  -  O volta ao inicio";
        else {
            sub = localTracks ? std::to_string(localTracks) + " faixas no HD/pendrive" : "Nenhum MP3 local";
            if (serverPending_) sub += "  -  conectando ao servidor...";
            else if (app.server.configured()) sub += "  -  servidor " + app.serverStatus;
            sub += "  -  OPTIONS busca";
        }
        if (serverPending_) drawnLoading_ = true;
        drawSectionHeader(app, c, area, "Musica", sub);
        int bottomPanel = 170;
        Rect view{area.x + theme::Margin, kTop, area.right() - theme::Margin - (area.x + theme::Margin), area.h - kTop - bottomPanel - 20};
        if (view_ == View::Detail) drawDetail(c, view);
        else drawShelves(c, view);
        drawNowPlaying(c, {view.x, area.bottom() - bottomPanel, view.w, bottomPanel - 16});
    }

    Hints hints() const override
    {
        if (view_ == View::Detail)
            return {{BtnCross, "Tocar"}, {BtnCircle, "Voltar"}, {BtnSquare, "Pausar"}, {BtnL1 | BtnR1, "Faixa"}, {BtnL2 | BtnR2, "Volume"},
                    {BtnTriangle, "Aleatorio"}, {BtnOptions, "Buscar"}};
        return {{BtnCross, "Tocar / abrir"}, {BtnSquare, "Pausar"}, {BtnL1 | BtnR1, "Faixa"}, {BtnL2 | BtnR2, "Volume"},
                {BtnTriangle, "Aleatorio"}, {BtnR3, "Repetir"}, {BtnOptions, "Buscar"}};
    }

private:
    static constexpr int kTop = 176, kPitch = 322, kCover = 190, kGap = 22;

    struct Album {
        enum State { Closed, Loading, Loaded, Failed };
        archive::Item item;
        State state = Closed;
        std::vector<size_t> tracks;          // indices into the music library once loaded
        std::shared_ptr<MultiFetch> fetch;   // request for the album's files
    };
    struct Card {
        enum Kind { Song, Playlist, AlbumCard } kind;
        size_t ref;   // Song: track index; Playlist: playlist index; AlbumCard: index into arch_
    };
    struct Shelf {
        std::string title;
        std::vector<Card> cards;
        int col = 0;
    };
    enum class View { Shelves, Detail };
    struct Detail {
        std::string kind, title;
        int playlist = -1, album = -1;
        int sel = 0;
        float scroll = 0;
    };

    View view_ = View::Shelves;
    std::vector<Shelf> shelves_;
    int row_ = 0;
    float scroll_ = 0;
    Detail detail_;
    bool anim_ = false, serverPending_ = false, drawnLoading_ = false, homeAsked_ = false, moved_ = false;
    uint64_t enteredMs_ = 0;
    std::string search_;
    std::vector<size_t> results_;             // songs of the current search (HD, server, Audius, Deezer)
    std::vector<Album> arch_;                 // Internet Archive albums of the current search
    std::vector<size_t> recent_;              // recently played, as library indices
    std::shared_ptr<MultiFetch> archSearch_, songSearch_, deezerSearch_, jamendoSearch_, openverseSearch_;
    size_t localCount_ = 0;                   // results_ that came from the HD / server (online ones follow)
    std::shared_ptr<MultiFetch> fetchTrend_, fetchForYou_, fetchMore_;
    std::string pendingGenre_, pendingArtist_;

    bool searching() const
    {
        return archSearch_ != nullptr || songSearch_ != nullptr || deezerSearch_ != nullptr || jamendoSearch_ != nullptr || openverseSearch_ != nullptr;
    }

    std::vector<size_t> songsOf(const Shelf &sh) const
    {
        std::vector<size_t> v;
        for (const auto &cd : sh.cards) if (cd.kind == Card::Song) v.push_back(cd.ref);
        return v;
    }

    std::vector<size_t> detailTracks() const
    {
        if (detail_.album >= 0 && detail_.album < int(arch_.size())) return arch_[size_t(detail_.album)].tracks;
        if (detail_.playlist >= 0 && detail_.playlist < int(app.music.playlists().size())) return app.music.playlists()[size_t(detail_.playlist)].tracks;
        return {};
    }

    // Library index of an online song, adding it once (the same address is never added twice).
    size_t ensureTrack(const music::Track &t)
    {
        const auto &all = app.music.tracks();
        for (size_t i = 0; i < all.size(); i++) if (all[i].url == t.url) return i;
        auto idx = app.music.addRemote("", {t}, false);
        return idx.empty() ? size_t(-1) : idx[0];
    }

    std::vector<size_t> addSongs(const std::vector<catalog::Song> &songs)
    {
        std::vector<size_t> out;
        for (const auto &s : songs) {
            if (s.url.empty()) continue;
            music::Track t;
            t.url = s.url;
            t.title = s.title;
            t.artist = s.artist;
            t.album = s.album;
            t.genre = s.genre;
            t.art = s.art;
            t.source = s.source;
            // When a source only provides a short preview (e.g. Deezer: 30 s),
            // cap durationMs to the preview length so the player's end-of-stream
            // handler sets positionMs_ to the real clip length instead of the
            // full track duration, preventing premature auto-advance.
            int64_t dur = int64_t(s.durationSec) * 1000;
            if (s.previewSec > 0 && int64_t(s.previewSec) * 1000 < dur)
                dur = int64_t(s.previewSec) * 1000;
            t.durationMs = dur;
            size_t i = ensureTrack(t);
            if (i != size_t(-1) && std::find(out.begin(), out.end(), i) == out.end()) out.push_back(i);   // no repeats in a list
        }
        return out;
    }

    // Recently played songs as library indices; songs that are no longer reachable (pendrive removed,
    // server off) are skipped.
    void loadRecent()
    {
        recent_.clear();
        const auto &all = app.music.tracks();
        for (const auto &p : app.taste.recent()) {
            if (!p.url.empty()) {
                music::Track t;
                t.url = p.url;
                t.title = p.title;
                t.artist = p.artist;
                t.album = p.album;
                t.genre = p.genre;
                t.art = p.art;
                t.source = p.source;
                t.durationMs = p.durationMs;
                size_t i = ensureTrack(t);
                if (i != size_t(-1)) recent_.push_back(i);
            } else {
                for (size_t i = 0; i < all.size(); i++)
                    if (all[i].url.empty() && all[i].path == p.path && all[i].remote == p.remote && !all[i].demo) { recent_.push_back(i); break; }
            }
        }
    }

    void addShelf(std::vector<Shelf> &out, const std::string &title, Card::Kind kind, const std::vector<size_t> &refs, size_t max = 40)
    {
        if (refs.empty()) return;
        Shelf sh;
        sh.title = title;
        for (size_t i = 0; i < refs.size() && i < max; i++) sh.cards.push_back({kind, refs[i]});
        for (const auto &old : shelves_) if (old.title == title) sh.col = std::min(old.col, int(sh.cards.size()) - 1);
        out.push_back(std::move(sh));
    }

    // Rebuilds the shelves from the current state, keeping the selected card of each shelf.
    void rebuild()
    {
        std::string focused = row_ < int(shelves_.size()) ? shelves_[size_t(row_)].title : "";
        std::vector<Shelf> out;
        if (!search_.empty()) {
            addShelf(out, "Musicas", Card::Song, results_, 60);
            std::vector<size_t> al;
            for (size_t i = 0; i < arch_.size(); i++) al.push_back(i);
            addShelf(out, "Albuns no Internet Archive", Card::AlbumCard, al);
        } else {
            loadRecent();
            const auto &h = app.musicHome;
            addShelf(out, "Tocadas recentemente", Card::Song, recent_, 20);
            if (app.settings.audius) {
                if (!h.forYouGenre.empty()) addShelf(out, "Porque voce ouve " + h.forYouGenre, Card::Song, h.forYou, 20);
                if (!h.moreOfArtist.empty()) addShelf(out, "Mais de " + h.moreOfArtist, Card::Song, h.moreOf, 20);
                addShelf(out, "Em alta no Audius", Card::Song, h.trending);
            }
            std::vector<size_t> pls;
            for (size_t i = 0; i < app.music.playlists().size(); i++) pls.push_back(i);
            addShelf(out, "Suas playlists", Card::Playlist, pls);
        }
        shelves_ = std::move(out);
        row_ = 0;
        // Shelves arriving while the user has not moved yet do not steal the first position.
        if (moved_) for (size_t i = 0; i < shelves_.size(); i++) if (shelves_[i].title == focused) row_ = int(i);
    }

    float shelfScrollTarget() const
    {
        if (shelves_.empty()) return 0;
        return float(row_ * kPitch);   // the selected shelf sits at the top, the next one shows below it
    }

    std::shared_ptr<MultiFetch> fetchOne(const char *label, const std::string &url, const std::string &base)
    {
        MultiFetch::Item it;
        it.label = label;
        it.url = url;
        it.policy.allowPlainHttpRemote = base.compare(0, 7, "http://") == 0;   // tests only (local fixture server)
        return MultiFetch::start(app.platform, {it});
    }

    // 0 = still running, 1 = answered (j filled), 2 = failed (e filled). Clears the request when done.
    int take(std::shared_ptr<MultiFetch> &f, Json &j, std::string &e)
    {
        if (!f) return 0;
        MultiFetch::State st = f->poll(0, &j, &e, nullptr);
        if (st == MultiFetch::State::Pending) return 0;
        f.reset();
        return st == MultiFetch::State::Done ? 1 : 2;
    }

    // Shelves that come from the network. Failures here are silent: the shelf simply does not appear.
    void startHomeFetches()
    {
        if (!app.settings.audius || !app.platform.http()) return;
        auto &h = app.musicHome;
        if (!h.trendingAsked) {
            h.trendingAsked = true;
            fetchTrend_ = fetchOne(audius::kName, audius::trendingUrl(app.audiusBase), app.audiusBase);
        }
        std::string g = app.taste.topGenre();
        if (!g.empty() && g != h.forYouGenre && !fetchForYou_) {
            pendingGenre_ = g;
            fetchForYou_ = fetchOne(audius::kName, audius::trendingUrl(app.audiusBase, g), app.audiusBase);
        }
        std::string a = app.taste.topArtist();
        if (!a.empty() && a != h.moreOfArtist && !fetchMore_) {
            pendingArtist_ = a;
            fetchMore_ = fetchOne(audius::kName, audius::searchUrl(a, app.audiusBase), app.audiusBase);
        }
    }

    bool recentlyPlayed(const std::string &url) const
    {
        for (const auto &p : app.taste.recent()) if (p.url == url) return true;
        return false;
    }

    void runSearch(const std::string &q)
    {
        search_ = q;
        results_.clear();
        arch_.clear();
        archSearch_.reset();
        songSearch_.reset();
        deezerSearch_.reset();
        jamendoSearch_.reset();
        openverseSearch_.reset();
        localCount_ = 0;
        view_ = View::Shelves;
        if (!q.empty()) {
            results_ = app.music.search(q);
            if (app.server.configured()) {
                std::string e;
                auto remote = app.searchServer(q, &e);
                results_.insert(results_.end(), remote.begin(), remote.end());
                if (!e.empty()) app.toast("Busca no servidor: " + e);
            }
            localCount_ = results_.size();
            if (app.platform.http()) {
                // Jamendo first: full-length Creative Commons MP3s.
                // Uses the user's client_id when set; falls back to the public demo key.
                if (app.settings.jamendo) {
                    const std::string &cid = app.settings.jamendoClientId.empty()
                                             ? std::string(jamendo::kPublicClientId)
                                             : app.settings.jamendoClientId;
                    jamendoSearch_ = fetchOne(jamendo::kName, jamendo::searchUrl(q, cid, jamendo::kLimit, app.jamendoBase), app.jamendoBase);
                }
                if (app.settings.archive) archSearch_ = fetchOne(archive::kName, archive::searchUrl(q, 30, app.archiveBase), app.archiveBase);
                if (app.settings.audius) songSearch_ = fetchOne(audius::kName, audius::searchUrl(q, app.audiusBase), app.audiusBase);
                if (app.settings.deezer) deezerSearch_ = fetchOne(deezer::kName, deezer::searchUrl(q, deezer::kLimit, app.deezerBase), app.deezerBase);
                if (app.settings.openverse) openverseSearch_ = fetchOne(openverse::kName, openverse::searchUrl(q, openverse::kLimit, app.openverseBase), app.openverseBase);
            }
        }
        shelves_.clear();
        row_ = 0;
        scroll_ = 0;
        moved_ = false;
        rebuild();
        if (!q.empty()) searchFinished();   // nothing online pending: say so now
        else if (homeAsked_) startHomeFetches();   // back at the home: recommendations follow what was just played
    }

    void searchFinished()
    {
        if (searching()) return;
        if (results_.empty() && arch_.empty()) app.toast("Nada encontrado para \"" + search_ + "\"");
    }

    // Asks the Archive for the album's files (background thread); the tracks appear when it answers.
    void openAlbum(int a)
    {
        Album &al = arch_[size_t(a)];
        if (al.state == Album::Loading || al.state == Album::Loaded) return;
        std::string url = archive::metadataUrl(al.item.id, app.archiveBase);
        if (url.empty()) { al.state = Album::Failed; return; }
        al.fetch = fetchOne(archive::kName, url, app.archiveBase);
        al.state = Album::Loading;
    }

    // Collects answers of the background requests (called every frame; cheap when nothing is pending).
    void poll()
    {
        Json j;
        std::string e;
        bool changed = false;
        if (int r = take(archSearch_, j, e)) {
            if (r == 1) {
                for (auto &it : archive::parseSearch(j)) {
                    Album al;
                    al.item = std::move(it);
                    arch_.push_back(std::move(al));
                }
            } else {
                app.toast(std::string(archive::kName) + ": " + e);
            }
            searchFinished();
            changed = true;
        }
        if (int r = take(jamendoSearch_, j, e)) {
            if (r == 1) {
                // primary online source: placed right after the HD / server results, ahead of
                // whatever Audius / Deezer already answered
                std::vector<std::string> seen;
                for (size_t i : results_) seen.push_back(lowered(app.music.tracks()[i].title + "|" + app.music.tracks()[i].artist));
                std::vector<catalog::Song> fresh;
                for (auto &sng : jamendo::parseSearch(j)) {
                    std::string k = lowered(sng.title + "|" + sng.artist);
                    if (std::find(seen.begin(), seen.end(), k) != seen.end()) continue;
                    seen.push_back(k);
                    fresh.push_back(std::move(sng));
                }
                rank::sortFreeText(search_, fresh);
                auto idx = addSongs(fresh);
                size_t at = std::min(localCount_, results_.size());
                results_.insert(results_.begin() + long(at), idx.begin(), idx.end());
            } else {
                app.toast(std::string(jamendo::kName) + ": " + e);
            }
            searchFinished();
            changed = true;
        }
        if (int r = take(songSearch_, j, e)) {
            if (r == 1) {
                // the same song already found on the HD / server / twice in the answer is not repeated
                std::vector<std::string> seen;
                for (size_t i : results_) seen.push_back(lowered(app.music.tracks()[i].title + "|" + app.music.tracks()[i].artist));
                std::vector<catalog::Song> fresh;
                for (auto &sng : audius::parseTracks(j, app.audiusBase)) {
                    std::string k = lowered(sng.title + "|" + sng.artist);
                    if (std::find(seen.begin(), seen.end(), k) != seen.end()) continue;
                    seen.push_back(k);
                    fresh.push_back(std::move(sng));
                }
                rank::sortFreeText(search_, fresh);
                auto idx = addSongs(fresh);
                results_.insert(results_.end(), idx.begin(), idx.end());
            } else {
                app.toast(std::string(audius::kName) + ": " + e);
            }
            searchFinished();
            changed = true;
        }
        if (int r = take(deezerSearch_, j, e)) {
            if (r == 1) {
                // Deezer previews (30s) are added only when the same title+artist is not already
                // present from a full-length source (Audius, Archive, local). They enrich covers
                // and metadata for items the user may already know.
                std::vector<std::string> seen;
                for (size_t i : results_) seen.push_back(lowered(app.music.tracks()[i].title + "|" + app.music.tracks()[i].artist));
                std::vector<catalog::Song> fresh;
                for (auto &sng : deezer::parseSearch(j)) {
                    std::string k = lowered(sng.title + "|" + sng.artist);
                    if (std::find(seen.begin(), seen.end(), k) != seen.end()) continue;
                    seen.push_back(k);
                    fresh.push_back(std::move(sng));
                }
                rank::sortFreeText(search_, fresh);
                auto idx = addSongs(fresh);
                results_.insert(results_.end(), idx.begin(), idx.end());
            }
            // Deezer failures are silent: it's supplementary, not essential.
            searchFinished();
            changed = true;
        }
        if (int r = take(openverseSearch_, j, e)) {
            if (r == 1) {
                // Openverse: full-length CC audio — placed after Jamendo, before Audius/Deezer.
                std::vector<std::string> seen;
                for (size_t i : results_) seen.push_back(lowered(app.music.tracks()[i].title + "|" + app.music.tracks()[i].artist));
                std::vector<catalog::Song> fresh;
                for (auto &sng : openverse::parseSearch(j)) {
                    std::string k = lowered(sng.title + "|" + sng.artist);
                    if (std::find(seen.begin(), seen.end(), k) != seen.end()) continue;
                    seen.push_back(k);
                    fresh.push_back(std::move(sng));
                }
                rank::sortFreeText(search_, fresh);
                auto idx = addSongs(fresh);
                // insert after localCount_ + jamendo results but before deezer/audius
                size_t at = std::min(localCount_, results_.size());
                results_.insert(results_.begin() + long(at), idx.begin(), idx.end());
            }
            // Openverse failures are silent: supplementary source.
            searchFinished();
            changed = true;
        }
        if (int r = take(fetchTrend_, j, e)) {
            if (r == 1) app.musicHome.trending = addSongs(audius::parseTracks(j, app.audiusBase));
            changed = true;
        }
        if (int r = take(fetchForYou_, j, e)) {
            auto &h = app.musicHome;
            h.forYouGenre = pendingGenre_;   // also on failure: not asked again for the same genre this session
            h.forYou.clear();
            if (r == 1) {
                std::vector<catalog::Song> keep;
                for (auto &sng : audius::parseTracks(j, app.audiusBase))
                    if (!recentlyPlayed(sng.url)) keep.push_back(std::move(sng));   // recommend what was not just heard
                h.forYou = addSongs(keep);
            }
            changed = true;
        }
        if (int r = take(fetchMore_, j, e)) {
            auto &h = app.musicHome;
            h.moreOfArtist = pendingArtist_;
            h.moreOf.clear();
            if (r == 1) {
                std::vector<catalog::Song> keep;
                std::string want = lowered(pendingArtist_);
                for (auto &sng : audius::parseTracks(j, app.audiusBase))
                    if (lowered(sng.artist) == want && !recentlyPlayed(sng.url)) keep.push_back(std::move(sng));   // really by that artist
                h.moreOf = addSongs(keep);
            }
            changed = true;
        }
        for (size_t a = 0; a < arch_.size(); a++) {
            Album &al = arch_[a];
            if (al.state != Album::Loading) continue;
            int r = take(al.fetch, j, e);
            if (!r) continue;
            if (r == 1) {
                al.tracks = addSongs(archive::parseItem(j, app.archiveBase));
                al.state = al.tracks.empty() ? Album::Failed : Album::Loaded;
                if (al.tracks.empty()) app.toast("\"" + al.item.title + "\" nao tem faixas em MP3");
            } else {
                al.state = Album::Failed;
                app.toast(std::string(archive::kName) + ": " + e);
            }
            app.dirty();
        }
        if (changed) { rebuild(); app.dirty(); }
    }

    // ------------------------------------------------------------------ drawing
    void drawCover(Canvas &c, Rect r, int radius, const std::string &art, bool online, const std::string &seed)
    {
        const Image *img = art.empty() ? nullptr : app.images.get(art, r.w, r.h);
        if (img) { c.drawImage(*img, r.x, r.y, radius); return; }
        if (online) {   // the source has no cover (or it has not arrived): neutral square, never another picture
            c.fillRoundRect(r.x, r.y, r.w, r.h, radius, theme::BgHover());
            drawIcon(c, Icon::Music, float(r.x + r.w / 2), float(r.y + r.h / 2), float(r.w) * 0.36f, theme::TextMute());
            return;
        }
        drawGeneratedArt(c, app.fonts, r, radius, seed, "", false);
    }

    void drawCard(Canvas &c, int x, int y, const Card &cd, bool focused)
    {
        Rect cover{x, y, kCover, kCover};
        std::string title, line2, line3;
        bool playing = false;
        if (cd.kind == Card::Song) {
            const music::Track &t = app.music.tracks()[cd.ref];
            drawCover(c, cover, 14, t.art, !t.source.empty(), "al:" + t.album);
            title = t.title;
            line2 = t.artist;
            line3 = t.source;
            playing = cd.ref == app.queue.current() && app.audio.state() != PlayState::Idle && app.audio.state() != PlayState::Error && !t.demo;
        } else if (cd.kind == Card::Playlist) {
            const auto &pl = app.music.playlists()[cd.ref];
            drawCover(c, cover, 14, "", false, "pl:" + pl.name);
            title = pl.name;
            line2 = std::to_string(pl.tracks.size()) + " faixas";
        } else {
            const Album &al = arch_[cd.ref];
            drawCover(c, cover, 14, archive::coverUrl(al.item.id, app.archiveBase), true, "");
            title = al.item.title;
            line2 = al.item.creator.empty() ? "Artista nao informado" : al.item.creator;
            line3 = archive::kName;
        }
        if (focused) drawFocusRing(c, cover, 14);
        if (playing) {
            c.fillCircle(float(cover.right() - 28), float(cover.bottom() - 28), 20, theme::Red);
            drawIcon(c, app.audio.state() == PlayState::Playing ? Icon::Music : Icon::Pause, float(cover.right() - 28), float(cover.bottom() - 28), 18, theme::AccentText());
        }
        app.fonts.bold->draw(c, x, y + kCover + 10, 20, app.fonts.bold->ellipsize(20, title, kCover), focused ? theme::Text() : theme::TextSub());
        app.fonts.regular->draw(c, x, y + kCover + 36, 17, app.fonts.regular->ellipsize(17, line2, kCover), theme::TextMute());
        if (!line3.empty()) app.fonts.regular->draw(c, x, y + kCover + 58, 14, line3, theme::Red);
    }

    void drawShelves(Canvas &c, Rect view)
    {
        if (shelves_.empty()) {
            std::string msg = !search_.empty() ? (searching() ? "Procurando..." : "Nada encontrado. Aperte OPTIONS para buscar outra coisa.")
                                               : "Nada por aqui ainda. Aperte OPTIONS para buscar uma musica, um artista ou um album.";
            app.fonts.regular->draw(c, view.x, view.y + 20, 24, msg, theme::TextSub());
            return;
        }
        c.pushClip({view.x - 8, view.y, view.w + 16, view.h});
        int cols = std::max(1, (view.w + kGap) / (kCover + kGap));
        bool contentFocus = !app.sidebarFocused();
        for (int i = 0; i < int(shelves_.size()); i++) {
            int y = view.y + 6 + i * kPitch - int(scroll_);
            if (y + kPitch < view.y || y > view.bottom()) continue;
            const Shelf &sh = shelves_[size_t(i)];
            bool rowFocus = contentFocus && i == row_;
            app.fonts.bold->draw(c, view.x, y, 26, sh.title, rowFocus ? theme::Text() : theme::TextSub());
            int first = std::max(0, sh.col - (cols - 1));
            for (int k = first; k < int(sh.cards.size()) && k < first + cols; k++)
                drawCard(c, view.x + (k - first) * (kCover + kGap), y + 44, sh.cards[size_t(k)], rowFocus && k == sh.col);
            if (first + cols < int(sh.cards.size()))
                app.fonts.regular->draw(c, view.right() - 90, y + 6, 17, "+" + std::to_string(int(sh.cards.size()) - first - cols), theme::TextMute());
        }
        c.popClip();
    }

    void drawDetail(Canvas &c, Rect view)
    {
        c.fillRoundRect(view.x, view.y, view.w, view.h, 22, theme::BgSurface());
        auto tr = detailTracks();
        Rect art{view.x + 28, view.y + 24, 170, 170};
        const Album *al = detail_.album >= 0 && detail_.album < int(arch_.size()) ? &arch_[size_t(detail_.album)] : nullptr;
        if (al) drawCover(c, art, 14, archive::coverUrl(al->item.id, app.archiveBase), true, "");
        else drawCover(c, art, 14, "", false, "pl:" + detail_.title);
        int hx = art.right() + 28, hw = view.w - (hx - view.x) - 28;
        app.fonts.regular->draw(c, hx, view.y + 34, 17, detail_.kind, theme::TextMute());
        app.fonts.bold->draw(c, hx, view.y + 62, 38, app.fonts.bold->ellipsize(38, detail_.title, hw), theme::Text());
        std::string who = al ? (al->item.creator.empty() ? "Artista nao informado" : al->item.creator) : "";
        if (!who.empty()) app.fonts.regular->draw(c, hx, view.y + 116, 22, app.fonts.regular->ellipsize(22, who, hw), theme::TextSub());
        std::string count = std::to_string(tr.size()) + " faixas";
        if (al && al->state == Album::Loading) count = "Abrindo o album no Internet Archive...";
        if (al && al->state == Album::Failed) count = "Nao foi possivel abrir este album. Aperte X para tentar de novo.";
        app.fonts.regular->draw(c, hx, view.y + 152, 19, count, theme::TextMute());
        int head = 214;
        c.pushClip({view.x, view.y + head, view.w, view.h - head - 8});
        int ty = view.y + head + 4 - int(detail_.scroll);
        size_t playing = app.queue.current();
        for (int i = 0; i < int(tr.size()); i++, ty += 74) {
            if (ty + 74 < view.y + head || ty > view.bottom()) continue;
            const music::Track &t = app.music.tracks()[tr[size_t(i)]];
            bool f = i == detail_.sel && !app.sidebarFocused();
            bool isPlaying = tr[size_t(i)] == playing && app.audio.state() != PlayState::Idle && !t.demo;
            Rect r{view.x + 12, ty, view.w - 24, 66};
            if (f) { c.fillRoundRect(r.x, r.y, r.w, r.h, 14, theme::BgHover()); c.strokeRoundRect(r.x, r.y, r.w, r.h, 14, 2, theme::Red); }
            if (isPlaying) drawIcon(c, app.audio.state() == PlayState::Playing ? Icon::Music : Icon::Pause, float(r.x + 30), float(r.y + 33), 22, theme::TextSub());
            else app.fonts.regular->draw(c, r.x + 18, r.y + 20, 20, std::to_string(i + 1), theme::TextMute());
            int textW = r.w / 2 - 90;
            app.fonts.bold->draw(c, r.x + 66, r.y + 8, 23, app.fonts.bold->ellipsize(23, t.title, textW), isPlaying ? theme::TextSub() : theme::Text());
            app.fonts.regular->draw(c, r.x + 66, r.y + 38, 18, app.fonts.regular->ellipsize(18, t.artist, textW), theme::TextSub());
            app.fonts.regular->draw(c, r.x + r.w / 2, r.y + 22, 19, app.fonts.regular->ellipsize(19, t.album.empty() ? t.genre : t.album, r.w / 2 - 140), theme::TextMute());
            if (t.demo) app.fonts.bold->draw(c, r.right() - 104, r.y + 22, 16, "DEMO", theme::Warn);
            else if (t.durationMs > 0) app.fonts.regular->draw(c, r.right() - 104, r.y + 22, 17, formatTime(t.durationMs), theme::TextMute());
        }
        c.popClip();
    }

    void drawNowPlaying(Canvas &c, Rect r)
    {
        c.fillRoundRect(r.x, r.y, r.w, r.h, theme::Radius, theme::BgCard());
        c.strokeRoundRect(r.x, r.y, r.w, r.h, theme::Radius, 1, theme::Border());
        size_t t = app.queue.current();
        bool active = t < app.music.tracks().size() && app.audio.state() != PlayState::Idle && app.audio.state() != PlayState::Error;
        Rect art{r.x + 20, r.y + 20, r.h - 40, r.h - 40};
        const Image *cover = active && !app.music.tracks()[t].art.empty() ? app.images.get(app.music.tracks()[t].art, art.w, art.h) : nullptr;
        if (cover) c.drawImage(*cover, art.x, art.y, 14);
        else drawGeneratedArt(c, app.fonts, art, 14, active ? "al:" + app.music.tracks()[t].album : "idle", "", false);
        int x = r.x + r.h;
        if (!active) {
            app.fonts.bold->draw(c, x, r.y + 30, 26, "Nada tocando", theme::Text());
            app.fonts.regular->draw(c, x, r.y + 70, 20, "Escolha uma faixa e aperte X. A musica para se voce sair do app (veja docs/ROADMAP.md).", theme::TextSub());
        } else {
            const music::Track &tr = app.music.tracks()[t];
            app.fonts.bold->draw(c, x, r.y + 22, 28, app.fonts.bold->ellipsize(28, tr.title, 700), theme::Text());
            app.fonts.regular->draw(c, x, r.y + 60, 21, app.fonts.regular->ellipsize(21, tr.artist + (tr.album.empty() ? "" : "  -  " + tr.album), 700), theme::TextSub());
            int64_t pos = app.audio.getPosition(), dur = app.audio.getDuration();
            drawProgress(c, x, r.y + r.h - 40, 760, 8, dur > 0 ? double(pos) / double(dur) : 0, theme::Red, withAlpha(theme::Text(), 40));
            app.fonts.regular->draw(c, x + 780, r.y + r.h - 52, 19, formatTime(pos) + " / " + formatTime(dur), theme::TextSub());
        }
        // transport + status icons (right side)
        int ix = r.right() - 420;
        float cy = float(r.y + r.h / 2);
        drawIcon(c, Icon::Prev, float(ix), cy, 28, theme::TextSub());
        c.fillCircle(float(ix + 70), cy, 34, theme::Red);
        drawIcon(c, app.audio.state() == PlayState::Playing ? Icon::Pause : Icon::Play, float(ix + 72), cy, 28, theme::AccentText());
        drawIcon(c, Icon::Next, float(ix + 140), cy, 28, theme::TextSub());
        drawIcon(c, Icon::Shuffle, float(ix + 210), cy, 26, app.queue.shuffle() ? theme::Red : theme::TextMute());
        drawIcon(c, Icon::Repeat, float(ix + 262), cy, 26, app.queue.repeat() == music::Repeat::Off ? theme::TextMute() : theme::Red);
        if (app.queue.repeat() == music::Repeat::One) app.fonts.bold->draw(c, ix + 276, int(cy) - 26, 14, "1", theme::TextSub());
        drawIcon(c, Icon::Volume, float(ix + 314), cy, 24, theme::TextSub());
        drawProgress(c, ix + 336, int(cy) - 4, 70, 8, app.settings.volume / 100.0, theme::Red, withAlpha(theme::Text(), 40));
    }
};

}  // namespace

std::unique_ptr<Screen> makeMusic(App &a) { return std::unique_ptr<Screen>(new MusicScreen(a)); }

}  // namespace ml
