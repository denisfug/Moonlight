// Home: hero banner for the focused item + horizontal card rows.
#include <algorithm>
#include <cmath>

#include "app/app.hpp"
#include "app/demo_data.hpp"
#include "ui/theme.hpp"

namespace ml {

using namespace ui;

void drawSectionHeader(App &app, Canvas &c, Rect area, const std::string &title, const std::string &subtitle)
{
    app.fonts.bold->draw(c, area.x + theme::Margin, 52, 52, title, theme::Text);
    if (!subtitle.empty())
        app.fonts.regular->draw(c, area.x + theme::Margin, 122, 24, subtitle, theme::TextDim);
}

void drawPosterCard(App &app, Canvas &c, Rect r, const std::string &seed, const std::string &title,
                    const std::string &subtitle, bool focused, double progress, const std::string &posterUrl)
{
    if (focused) {
        int g = 10;   // grow
        r = {r.x - g / 2, r.y - g, r.w + g, r.h + g};
        drawFocusRing(c, r, 18);
    }
    if (const Image *img = app.images.get(posterUrl, r.w, r.h)) c.drawImage(*img, r.x, r.y, 16);
    else drawGeneratedArt(c, app.fonts, r, 16, seed, title, true);
    if (progress >= 0) drawProgress(c, r.x + 14, r.bottom() - 16, r.w - 28, 6, progress, theme::Blue, rgba(255, 255, 255, 50));
    if (!subtitle.empty()) {
        Font *f = app.fonts.regular;
        f->draw(c, r.x + 4, r.bottom() + 14, 20, f->ellipsize(20, subtitle, r.w - 8), focused ? theme::Text : theme::TextMute);
    }
}

namespace {

enum class Shape { Poster, Landscape, Square };

struct Card {
    std::string id, type, title, subtitle, description, meta, poster;
    double progress = -1;
    size_t index = 0;   // playlist / channel index
};

struct Row {
    std::string title;
    Shape shape = Shape::Poster;
    std::vector<Card> cards;
    int sel = 0;
    float scroll = 0, scrollTarget = 0;
};

Rect cardSize(Shape s)
{
    switch (s) {
    case Shape::Poster: return {0, 0, 184, 276};
    case Shape::Landscape: return {0, 0, 340, 191};
    default: return {0, 0, 228, 228};
    }
}

class HomeScreen : public Screen {
public:
    using Screen::Screen;
    std::string title() const override { return "Inicio"; }

    void onEnter() override
    {
        std::string keepTitle = rows_.empty() ? "" : rows_[size_t(rowSel_)].title;
        rows_.clear();
        auto cw = app.library.continueWatching(12);
        if (!cw.empty()) {
            Row r{"Continuar assistindo", Shape::Landscape};
            for (const auto &i : cw) {
                Card cd{i.id, i.type, i.name};
                cd.subtitle = i.type == "series" ? "T" + std::to_string(i.season) + " E" + std::to_string(i.episode) + " - " + formatTime(i.positionMs)
                                                 : formatTime(i.positionMs) + " de " + formatTime(i.durationMs);
                cd.progress = i.progress();
                r.cards.push_back(cd);
            }
            rows_.push_back(r);
        }
        addMetaRow("Filmes em destaque", Shape::Poster, app.settings.showDemo ? demo::movies() : std::vector<addon::MetaPreview>{});
        addMetaRow("Series para maratonar", Shape::Poster, app.settings.showDemo ? demo::series() : std::vector<addon::MetaPreview>{});
        // catalogs from the user's sources (synchronous for now; see docs/ROADMAP.md)
        for (const auto &ref : app.sources.browsableCatalogs()) {
            std::vector<addon::MetaPreview> metas;
            if (!app.sources.fetchCatalog(ref, metas).empty() || metas.empty()) continue;
            addMetaRow(ref.decl->name + " - " + app.sources.sources()[ref.source].manifest.name, Shape::Poster, metas);
        }
        Row mus{"Sua musica", Shape::Square};
        for (size_t i = 0; i < app.music.playlists().size(); i++) {
            const auto &p = app.music.playlists()[i];
            Card cd{"playlist:" + p.name, "music", p.name, std::to_string(p.tracks.size()) + " faixas" + (p.source == "demo" ? " (demo)" : "")};
            cd.index = i;
            mus.cards.push_back(cd);
        }
        if (!mus.cards.empty()) rows_.push_back(mus);
        Row live{"Canais ao vivo", Shape::Landscape};
        for (size_t i = 0; i < app.channels.size() && i < 20; i++) {
            Card cd{"channel:" + app.channels[i].name, "tv", app.channels[i].name, app.channels[i].group};
            cd.index = i;
            live.cards.push_back(cd);
        }
        if (!live.cards.empty()) rows_.push_back(live);
        rowSel_ = 0;
        for (size_t i = 0; i < rows_.size(); i++) if (rows_[i].title == keepTitle) rowSel_ = int(i);
    }

    bool handle(uint32_t p) override
    {
        if (rows_.empty()) return false;
        Row &r = rows_[size_t(rowSel_)];
        if (p & BtnUp) { if (rowSel_ > 0) rowSel_--; return true; }
        if (p & BtnDown) { if (rowSel_ + 1 < int(rows_.size())) rowSel_++; return true; }
        if (p & BtnLeft) { if (r.sel == 0) return false; r.sel--; return true; }
        if (p & BtnRight) { if (r.sel + 1 < int(r.cards.size())) r.sel++; return true; }
        if (p & BtnCross) { open(r.cards[size_t(r.sel)]); return true; }
        return false;
    }

    void update(float dt) override
    {
        float k = app.settings.animations ? std::min(1.0f, dt * 14.0f) : 1.0f;
        anim_ = false;
        auto ease = [&](float &v, float t) {
            if (std::fabs(t - v) > 0.5f) { v += (t - v) * k; anim_ = true; } else v = t;
        };
        for (auto &r : rows_) {
            Rect cs = cardSize(r.shape);
            r.scrollTarget = float(std::max(0, r.sel - 1) * (cs.w + 28));
            ease(r.scroll, r.scrollTarget);
        }
        float target = 0;
        for (int i = 0; i < rowSel_; i++) target += rowHeight(rows_[size_t(i)]);
        ease(vscroll_, target);
    }
    bool animating() const override { return anim_; }

    void draw(Canvas &c, Rect area) override
    {
        int x0 = area.x + theme::Margin;
        if (rows_.empty()) {
            drawSectionHeader(app, c, area, "Bem-vindo", "Nada para mostrar ainda: adicione musicas ou fontes.");
            return;
        }
        const Card &f = rows_[size_t(rowSel_)].cards[size_t(rows_[size_t(rowSel_)].sel)];
        drawHero(c, area, f);
        // rows
        const int top = 486;
        c.pushClip({area.x, top - 40, area.w, area.bottom() - (top - 40)});
        float y = top - vscroll_;
        for (size_t i = 0; i < rows_.size(); i++) {
            Row &r = rows_[i];
            int h = rowHeight(r);
            if (y + h > top - 60 && y < area.bottom()) {
                bool rowFocus = int(i) == rowSel_ && !app.sidebarFocused();
                app.fonts.bold->draw(c, x0, int(y), 28, r.title, rowFocus ? theme::Text : theme::TextDim);
                Rect cs = cardSize(r.shape);
                int cy = int(y) + 54;
                c.pushClip({area.x, cy - 30, area.w, h});
                for (size_t k = 0; k < r.cards.size(); k++) {
                    int cx = x0 + int(k) * (cs.w + 28) - int(r.scroll);
                    if (cx > area.right() || cx + cs.w < area.x) continue;
                    const Card &cd = r.cards[k];
                    drawPosterCard(app, c, {cx, cy, cs.w, cs.h}, cd.id, cd.title, r.shape == Shape::Poster ? cd.meta : cd.subtitle,
                                   rowFocus && int(k) == r.sel, cd.progress, cd.poster);
                }
                c.popClip();
            }
            y += h;
        }
        c.popClip();
    }

    Hints hints() const override { return {{BtnUp | BtnDown | BtnLeft | BtnRight, "Navegar"}, {BtnCross, "Abrir"}, {BtnCircle, "Menu"}}; }

private:
    std::vector<Row> rows_;
    int rowSel_ = 0;
    float vscroll_ = 0;
    bool anim_ = false;

    static int rowHeight(const Row &r) { return cardSize(r.shape).h + 120; }

    void addMetaRow(const std::string &title, Shape s, const std::vector<addon::MetaPreview> &metas)
    {
        if (metas.empty()) return;
        Row r{title, s};
        for (const auto &m : metas) {
            Card cd{m.id, m.type, m.name};
            cd.description = m.description;
            cd.poster = m.poster;
            cd.meta = m.releaseInfo;
            if (!m.genres.empty()) cd.meta += (cd.meta.empty() ? "" : "  -  ") + m.genres[0];
            if (!m.imdbRating.empty()) cd.meta += "  -  nota " + m.imdbRating;
            r.cards.push_back(cd);
        }
        rows_.push_back(r);
    }

    void drawHero(Canvas &c, Rect area, const Card &f)
    {
        // backdrop art on the right, fading into the background on the left
        Rect art{area.x + area.w / 3, 0, area.w - area.w / 3, 452};
        drawGeneratedArt(c, app.fonts, art, 0, f.id + "#bg", f.title, false);
        // fade the art into the page background (which is a vertical BgTop -> Bg gradient)
        for (int y = 0; y < art.h; y += 4) {
            Color bg = mix(theme::BgTop, theme::Bg, float(y) / float(theme::H));
            c.fillGradient(art.x, y, art.w / 2, 4, 0, bg, withAlpha(bg, 0), 0);
        }
        Color bottom = mix(theme::BgTop, theme::Bg, float(art.h) / float(theme::H));
        c.fillGradient(art.x, art.h - 160, art.w, 160, 0, withAlpha(bottom, 0), bottom, 1);
        int x = area.x + theme::Margin;
        std::string kind = f.type == "movie" ? "FILME" : f.type == "series" ? "SERIE" : f.type == "music" ? "PLAYLIST" : f.type == "tv" ? "AO VIVO" : "";
        if (!kind.empty()) {
            int w = app.fonts.bold->measure(18, kind) + 28;
            c.fillGradient(x, 64, w, 32, 16, theme::Violet, theme::Blue, 0);
            app.fonts.bold->draw(c, x + 14, 64 + (32 - app.fonts.bold->lineHeight(18)) / 2, 18, kind, theme::Text);
        }
        app.fonts.bold->draw(c, x, 112, 64, app.fonts.bold->ellipsize(64, f.title, 980), theme::Text);
        std::string meta = !f.meta.empty() ? f.meta : f.subtitle;
        app.fonts.regular->draw(c, x, 200, 24, meta, theme::Glow);
        auto lines = app.fonts.regular->wrap(25, f.description, 860, 3);
        int y = 250;
        for (const auto &l : lines) { app.fonts.regular->draw(c, x, y, 25, l, theme::TextDim); y += 36; }
        if (f.progress >= 0) drawProgress(c, x, 380, 420, 8, f.progress, theme::Blue, theme::SurfaceHi);
    }

    void open(const Card &cd)
    {
        if (cd.type == "movie" || cd.type == "series") { app.push(makeDetail(app, cd.type, cd.id, cd.title)); return; }
        if (cd.type == "music") {
            app.goSection(Section::Music, true);
            const auto &pl = app.music.playlists()[cd.index];
            app.playTracks(pl.tracks, 0);
            return;
        }
        if (cd.type == "tv") { app.goSection(Section::Live, true); return; }
    }
};

}  // namespace

std::unique_ptr<Screen> makeHome(App &a) { return std::unique_ptr<Screen>(new HomeScreen(a)); }

}  // namespace ml
