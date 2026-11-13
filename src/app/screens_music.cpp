// Music: playlists, track list, search, and the now-playing panel (progress, volume, shuffle, repeat).
#include <algorithm>
#include <cmath>

#include "app/app.hpp"
#include "ui/theme.hpp"

namespace ml {

using namespace ui;

namespace {

class MusicScreen : public Screen {
public:
    using Screen::Screen;
    std::string title() const override { return "Musica"; }

    void onEnter() override
    {
        // the user's music server is loaded once per app start (after a "loading" frame)
        if (app.server.configured() && app.serverStatus.empty()) { serverPending_ = true; drawnLoading_ = false; }
        if (pl_ >= int(lists())) pl_ = 0;
    }

    size_t lists() const { return app.music.playlists().size() + (search_.empty() ? 0 : 1); }

    // index 0 = search results when a search is active
    std::vector<size_t> tracksOf(int i) const
    {
        if (!search_.empty()) {
            if (i == 0) return results_;
            i--;
        }
        if (i < 0 || i >= int(app.music.playlists().size())) return {};
        return app.music.playlists()[size_t(i)].tracks;
    }
    std::string nameOf(int i) const
    {
        if (!search_.empty()) {
            if (i == 0) return "Busca: " + search_;
            i--;
        }
        return app.music.playlists()[size_t(i)].name;
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
                pl_ = 0;
                tr_ = 0;
                zone_ = search_.empty() ? 0 : 1;
                if (!search_.empty() && results_.empty()) app.toast("Nada encontrado para \"" + q + "\"");
            });
            return true;
        }
        int n = int(lists());
        if (zone_ == 0) {
            if (p & BtnUp) { if (pl_ > 0) { pl_--; tr_ = 0; } return true; }
            if (p & BtnDown) { if (pl_ + 1 < n) { pl_++; tr_ = 0; } return true; }
            if (p & (BtnRight | BtnCross)) { if (!tr.empty()) zone_ = 1; return true; }
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
        float target = float(std::max(0, tr_ - 5) * 74);
        float k = app.settings.animations ? std::min(1.0f, dt * 14.0f) : 1.0f;
        anim_ = std::fabs(target - scroll_) > 0.5f;
        scroll_ = anim_ ? scroll_ + (target - scroll_) * k : target;
    }
    bool animating() const override { return anim_ || serverPending_; }

    void draw(Canvas &c, Rect area) override
    {
        size_t localTracks = 0;
        for (const auto &t : app.music.tracks()) if (!t.demo && !t.remote) localTracks++;
        std::string sub = localTracks ? std::to_string(localTracks) + " faixas no HD/pendrive" : "Nenhum MP3 local";
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
            app.fonts.regular->draw(c, r.x + 66, r.y + 32, 17, std::to_string(tracksOf(i).size()) + " faixas", on ? theme::Text : theme::TextMute);
        }
        // tracks
        int tx = x + pw + 28, tw = area.right() - theme::Margin - tx;
        c.fillRoundRect(tx, top, tw, listH, 22, theme::Surface);
        auto tr = tracksOf(pl_);
        app.fonts.bold->draw(c, tx + 26, top + 22, 22, lists() ? nameOf(pl_) : "", theme::TextMute);
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
            else if (t.remote) app.fonts.regular->draw(c, r.right() - 104, r.y + 22, 17, t.durationMs > 0 ? formatTime(t.durationMs) : "rede", theme::TextMute);
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
