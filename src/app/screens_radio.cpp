// Radios: Radio Browser directory (MP3 stations), genre tabs, search, favorites, live playback.
#include <algorithm>
#include <cmath>

#include "app/app.hpp"
#include "ui/theme.hpp"

namespace ml {

using namespace ui;

namespace {

struct Tab {
    const char *label;
    const char *tag;
    const char *country;
};
const Tab kTabs[] = {
    {"Brasil", "", "BR"}, {"Mundo", "", ""},      {"Sertanejo", "sertanejo", "BR"}, {"Funk", "funk", "BR"},
    {"Pop", "pop", ""},   {"Rock", "rock", ""},   {"Eletronica", "electronic", ""}, {"Gospel", "gospel", "BR"},
    {"Favoritas", "", ""}, {"Busca", "", ""},
};
constexpr int kTabCount = int(sizeof kTabs / sizeof kTabs[0]);
constexpr int kFavTab = kTabCount - 2, kSearchTab = kTabCount - 1;

class RadioScreen : public Screen {
public:
    using Screen::Screen;
    std::string title() const override { return "Radios"; }

    void onEnter() override
    {
        if (!loadedOnce_) { loadedOnce_ = true; requestLoad(); }
    }

    // Network requests are synchronous for now, so the "loading" frame is drawn first and the
    // request runs on the following update().
    void requestLoad()
    {
        pending_ = true;
        drawnLoading_ = false;
        error_.clear();
        sel_ = 0;
    }

    void update(float dt) override
    {
        if (pending_ && drawnLoading_) {
            pending_ = false;
            load();
            app.dirty();
        }
        float target = float(std::max(0, sel_ - 6) * 92);
        float k = app.settings.animations ? std::min(1.0f, dt * 14.0f) : 1.0f;
        anim_ = std::fabs(target - scroll_) > 0.5f;
        scroll_ = anim_ ? scroll_ + (target - scroll_) * k : target;
    }
    bool animating() const override { return anim_ || pending_; }

    void load()
    {
        stations_.clear();
        if (tab_ == kFavTab) { stations_ = app.radioFavorites.list(); return; }
        radio::Query q;
        if (tab_ == kSearchTab) {
            if (query_.empty()) return;
            q.name = query_;
        } else {
            q.tag = kTabs[tab_].tag;
            q.countryCode = kTabs[tab_].country;
        }
        error_ = app.radio.search(q, stations_);
    }

    bool handle(uint32_t p) override
    {
        if (p & (BtnL1 | BtnR1)) {
            tab_ = (tab_ + ((p & BtnR1) ? 1 : kTabCount - 1)) % kTabCount;
            if (tab_ == kSearchTab && query_.empty()) { stations_.clear(); error_.clear(); sel_ = 0; return true; }
            requestLoad();
            return true;
        }
        if (p & BtnOptions) {
            app.openKeyboard("Buscar radio pelo nome", query_, false, [this](const std::string &q) {
                query_ = q;
                tab_ = kSearchTab;
                requestLoad();
            });
            return true;
        }
        if (p & BtnSquare) {
            if (app.audio.live()) {
                if (app.audio.state() == PlayState::Playing) app.audio.stop();
                else if (!app.station.uuid.empty()) app.playStation(app.station);
            }
            return true;
        }
        int n = int(stations_.size());
        if (p & BtnUp) { if (sel_ > 0) sel_--; return true; }
        if (p & BtnDown) { if (sel_ + 1 < n) sel_++; return true; }
        if (n == 0) return false;
        const radio::Station &s = stations_[size_t(sel_)];
        if (p & BtnCross) { app.playStation(s); return true; }
        if (p & BtnTriangle) {
            app.radioFavorites.toggle(s);
            app.toast(app.radioFavorites.contains(s.uuid) ? "Favoritada: " + s.name : "Removida das favoritas");
            if (tab_ == kFavTab) { stations_ = app.radioFavorites.list(); sel_ = std::min(sel_, std::max(0, int(stations_.size()) - 1)); }
            return true;
        }
        return false;
    }

    void draw(Canvas &c, Rect area) override
    {
        std::string sub = "Diretorio Radio Browser (comunitario, gratis)  -  so radios MP3";
        drawSectionHeader(app, c, area, "Radios", sub);
        int x = area.x + theme::Margin, y = 176;
        for (int i = 0; i < kTabCount; i++) {
            std::string label = i == kSearchTab && !query_.empty() ? "Busca: " + query_ : kTabs[i].label;
            label = app.fonts.bold->ellipsize(22, label, 260);
            bool on = i == tab_;
            int w = app.fonts.bold->measure(22, label) + 40;
            if (on) c.fillGradient(x, y, w, 48, 24, theme::Violet, theme::Indigo, 0);
            else c.fillRoundRect(x, y, w, 48, 24, theme::Surface);
            (on ? app.fonts.bold : app.fonts.regular)->draw(c, x + 20, y + (48 - app.fonts.bold->lineHeight(22)) / 2, 22, label, on ? theme::Text : theme::TextDim);
            x += w + 10;
        }
        x = area.x + theme::Margin;
        int top = 256;
        if (pending_) {
            drawnLoading_ = true;
            app.fonts.regular->draw(c, x, top + 20, 28, "Carregando radios...", theme::TextDim);
            return;
        }
        if (stations_.empty()) {
            std::string msg = !error_.empty() ? "Nao foi possivel carregar: " + error_
                            : tab_ == kFavTab ? "Nenhuma favorita. Aperte triangulo numa radio para favoritar."
                            : tab_ == kSearchTab ? "Aperte OPTIONS para buscar uma radio pelo nome."
                                                 : "Nenhuma radio encontrada.";
            auto lines = app.fonts.regular->wrap(26, msg, area.w - 2 * theme::Margin, 3);
            int ly = top + 20;
            for (const auto &l : lines) { app.fonts.regular->draw(c, x, ly, 26, l, error_.empty() ? theme::TextDim : theme::Warn); ly += 38; }
            return;
        }
        int w = area.w - 2 * theme::Margin;
        c.pushClip({area.x, top - 20, area.w, area.bottom() - top + 20});
        int ry = top - int(scroll_);
        for (size_t i = 0; i < stations_.size(); i++, ry += 92) {
            if (ry + 92 < top - 20 || ry > area.bottom()) continue;
            const radio::Station &s = stations_[i];
            bool f = int(i) == sel_ && !app.sidebarFocused();
            bool playing = app.audio.live() && app.station.uuid == s.uuid &&
                           (app.audio.state() == PlayState::Playing || app.audio.state() == PlayState::Paused);
            Rect r{x, ry, w, 80};
            if (f) {
                c.shadow(r.x, r.y, r.w, r.h, 16, 16, withAlpha(theme::Violet, 90));
                c.fillRoundRect(r.x, r.y, r.w, r.h, 16, theme::SurfaceHi);
                c.strokeRoundRect(r.x, r.y, r.w, r.h, 16, 2, rgba(196, 181, 253));
            } else {
                c.fillRoundRect(r.x, r.y, r.w, r.h, 16, theme::Surface);
            }
            drawGeneratedArt(c, app.fonts, {r.x + 10, r.y + 10, 60, 60}, 12, "radio:" + s.uuid, "", false);
            drawIcon(c, playing ? Icon::Music : Icon::Live, float(r.x + 40), float(r.y + 40), 24, rgba(255, 255, 255, 230));
            app.fonts.bold->draw(c, r.x + 92, r.y + 12, 25, app.fonts.bold->ellipsize(25, s.name, r.w - 400), playing ? theme::Glow : theme::Text);
            std::string tags = s.tags;
            std::replace(tags.begin(), tags.end(), ',', ' ');
            app.fonts.regular->draw(c, r.x + 92, r.y + 46, 18, app.fonts.regular->ellipsize(18, s.countryCode + (s.countryCode.empty() || tags.empty() ? "" : "  -  ") + tags, r.w - 400), theme::TextMute);
            std::string q = std::to_string(s.bitrate) + " kbps";
            app.fonts.regular->draw(c, r.right() - 250, r.y + 28, 19, q, theme::TextDim);
            if (app.radioFavorites.contains(s.uuid)) drawIcon(c, Icon::HeartFilled, float(r.right() - 60), float(r.y + 40), 22, theme::Glow);
        }
        c.popClip();
    }

    Hints hints() const override
    {
        return {{BtnCross, "Tocar"}, {BtnSquare, "Parar/Retomar"}, {BtnTriangle, "Favoritar"}, {BtnL1 | BtnR1, "Categoria"},
                {BtnOptions, "Buscar"}};
    }

private:
    int tab_ = 0, sel_ = 0;
    bool loadedOnce_ = false, pending_ = false, drawnLoading_ = false, anim_ = false;
    float scroll_ = 0;
    std::string query_, error_;
    std::vector<radio::Station> stations_;
};

}  // namespace

std::unique_ptr<Screen> makeRadio(App &a) { return std::unique_ptr<Screen>(new RadioScreen(a)); }

}  // namespace ml
