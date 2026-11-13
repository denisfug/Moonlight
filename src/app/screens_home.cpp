// Home: hero banner for the focused item + horizontal card rows.
#include <algorithm>
#include <cmath>

#include "app/app.hpp"
#include "app/catalog_loader.hpp"
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

struct Cont {
    std::string id, type, name, sub;
    double progress = 0;
};

class HomeScreen : public Screen {
public:
    explicit HomeScreen(App &a) : Screen(a), loader_(new CatalogLoader(a.platform, a.platform.http())) {}
    std::string title() const override { return "Inicio"; }

    // Source catalogs load in the background: 01.11 fetched them here synchronously, before the first
    // frame, and the console stayed on the splash screen while the network answered (or timed out).
    void onEnter() override
    {
        if (seenVersion_ != app.sourcesVersion) {
            seenVersion_ = app.sourcesVersion;
            loader_->cancelAll();
            srcs_.clear();
            std::vector<int> perSource(app.sources.sources().size(), 0);
            for (const auto &ref : app.sources.browsableCatalogs()) {
                if (++perSource[ref.source] > 2) continue;
                Src s;
                s.title = ref.decl->name + " - " + app.sources.sources()[ref.source].manifest.name;
                UrlPolicy pol;
                std::string url = app.sources.catalogUrl(ref, {}, &pol);
                if (url.empty()) continue;
                s.ticket = loader_->request(url, pol);
                if (s.ticket) srcs_.push_back(s);
            }
        }
        rebuild();
        // entering Home: focus the continue panel when there is history (Left then reaches the sidebar),
        // otherwise the hero. Background rebuilds (new catalogs) do not touch focus.
        focus_ = continue_.empty() ? Zone::Hero : Zone::Cont;
        contSel_ = 0;
    }

    void rebuild()
    {
        std::string keepTitle = rows_.empty() ? "" : rows_[size_t(rowSel_)].title;
        // continue watching -> left panel
        continue_.clear();
        for (const auto &i : app.library.continueWatching(3)) {
            Cont cd{i.id, i.type, i.name};
            cd.sub = i.type == "series" ? "T" + std::to_string(i.season) + " E" + std::to_string(i.episode)
                                        : formatTime(i.durationMs - i.positionMs) + " restantes";
            cd.progress = i.progress();
            continue_.push_back(cd);
        }
        // demo mode with no real history: seed the panel so it matches the design (real use fills it in)
        if (continue_.empty() && app.videoDemo()) {
            const double pr[] = {0.48, 0.22, 0.71};
            const char *sub[] = {"32 min restantes", "1 h 05 min restantes", "18 min restantes"};
            auto dm = demo::movies();
            for (size_t i = 0; i < dm.size() && i < 3; i++) {
                Cont cd{dm[i].id, dm[i].type, dm[i].name};
                cd.sub = sub[i];
                cd.progress = pr[i];
                continue_.push_back(cd);
            }
        }
        if (contSel_ >= int(continue_.size())) contSel_ = std::max(0, int(continue_.size()) - 1);
        // rows -> strip below the hero (no "Continuar assistindo" row: it is the left panel now)
        rows_.clear();
        addMetaRow("Filmes em destaque", Shape::Poster, app.videoDemo() ? demo::movies() : std::vector<addon::MetaPreview>{});
        addMetaRow("Series para maratonar", Shape::Poster, app.videoDemo() ? demo::series() : std::vector<addon::MetaPreview>{});
        for (const auto &s : srcs_)
            if (!s.metas.empty()) addMetaRow(s.title, Shape::Poster, s.metas);
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
        // featured = first poster card available
        hasFeatured_ = false;
        for (const auto &r : rows_)
            if (!r.cards.empty() && (r.shape == Shape::Poster)) { featured_ = r.cards[0]; featuredSrc_ = r.title; hasFeatured_ = true; break; }
        if (focus_ == Zone::Cont && continue_.empty()) focus_ = Zone::Hero;
    }

    bool handle(uint32_t p) override
    {
        if (focus_ == Zone::Hero) {
            if (p & BtnLeft) { if (!continue_.empty()) { focus_ = Zone::Cont; return true; } return false; }
            if (p & BtnDown) { if (!rows_.empty()) { focus_ = Zone::Rows; rowSel_ = 0; return true; } return false; }
            if (p & BtnCross) { if (hasFeatured_) open(featured_); return true; }
            return false;
        }
        if (focus_ == Zone::Cont) {
            if (p & BtnUp) { if (contSel_ > 0) contSel_--; return true; }
            if (p & BtnDown) {
                if (contSel_ + 1 < int(continue_.size())) { contSel_++; return true; }
                if (!rows_.empty()) { focus_ = Zone::Rows; rowSel_ = 0; }
                return true;
            }
            if (p & BtnRight) { focus_ = Zone::Hero; return true; }
            if (p & BtnLeft) return false;   // to the sidebar
            if (p & BtnCross) { openCont(contSel_); return true; }
            return false;
        }
        // Zone::Rows
        if (rows_.empty()) return false;
        Row &r = rows_[size_t(rowSel_)];
        if (p & BtnUp) { if (rowSel_ > 0) { rowSel_--; return true; } focus_ = Zone::Hero; return true; }
        if (p & BtnDown) { if (rowSel_ + 1 < int(rows_.size())) rowSel_++; return true; }
        if (p & BtnLeft) { if (r.sel == 0) return false; r.sel--; return true; }
        if (p & BtnRight) { if (r.sel + 1 < int(r.cards.size())) r.sel++; return true; }
        if (p & BtnCross) { open(r.cards[size_t(r.sel)]); return true; }
        return false;
    }

    void update(float dt) override
    {
        bool got = false;
        for (auto &res : loader_->take())
            for (auto &s : srcs_)
                if (s.ticket == res.ticket) { s.metas = std::move(res.metas); got = true; }
        if (got) { rebuild(); app.dirty(); }
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
    }
    bool animating() const override { return anim_ || loader_->busy(); }

    // ---- layout -------------------------------------------------------------
    void draw(Canvas &c, Rect area) override
    {
        const int L = area.x + 52, R = theme::W - 56;
        drawTopBar(c, L, R);
        const int bandY = 134, bandH = 544, panelW = 372, gap = 28;
        if (!continue_.empty()) drawContinuePanel(c, {L, bandY, panelW, bandH});
        int heroX = continue_.empty() ? L : L + panelW + gap;
        drawHeroCard(c, {heroX, bandY, R - heroX, bandH});
        drawStrip(c, area, bandY + bandH + 26, R);
    }

    Hints hints() const override { return {{BtnUp | BtnDown | BtnLeft | BtnRight, "Navegar"}, {BtnCross, "Abrir"}, {BtnCircle, "Menu"}}; }

private:
    enum class Zone { Cont, Hero, Rows };
    std::vector<Row> rows_;
    std::vector<Cont> continue_;
    Card featured_;
    std::string featuredSrc_;
    bool hasFeatured_ = false;
    Zone focus_ = Zone::Hero;
    int contSel_ = 0;
    int rowSel_ = 0;
    bool anim_ = false;
    struct Src {
        std::string title;
        int ticket = 0;
        std::vector<addon::MetaPreview> metas;
    };
    std::unique_ptr<CatalogLoader> loader_;
    std::vector<Src> srcs_;
    int seenVersion_ = -1;

    static int rowHeight(const Row &r) { return cardSize(r.shape).h + 96; }

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

    void glassChip(Canvas &c, int x, int y, int w, int h, const std::string &text, int sz, bool solid)
    {
        if (solid) c.fillRoundRect(x, y, w, h, h / 2, withAlpha(theme::Text, 235));
        else { c.fillRoundRect(x, y, w, h, h / 2, theme::Surface); c.strokeRoundRect(x, y, w, h, h / 2, 1, theme::Line); }
        Color col = solid ? theme::Bg : theme::TextDim;
        app.fonts.bold->draw(c, x + (w - app.fonts.bold->measure(sz, text)) / 2, y + (h - app.fonts.bold->lineHeight(sz)) / 2, sz, text, col);
    }

    void drawTopBar(Canvas &c, int L, int R)
    {
        int y = 40, h = 60;
        // search field
        int sw = 520;
        c.fillRoundRect(L, y, sw, h, 30, theme::Surface);
        c.strokeRoundRect(L, y, sw, h, 30, 1, theme::Line);
        c.strokeCircle(float(L + 34), float(y + h / 2 - 2), 9, 2, theme::TextMute);
        c.line(float(L + 41), float(y + h / 2 + 5), float(L + 48), float(y + h / 2 + 12), 2, theme::TextMute);
        app.fonts.regular->draw(c, L + 62, y + (h - app.fonts.regular->lineHeight(22)) / 2, 22, "Buscar filmes e series", theme::TextMute);
        // top tabs (content filters; the sidebar is the main nav)
        const char *tabs[] = {"Inicio", "Filmes", "Series", "Musica"};
        int tx = L + sw + 22;
        for (int i = 0; i < 4; i++) {
            int tw = app.fonts.bold->measure(21, tabs[i]) + 52;
            glassChip(c, tx, y, tw, h, tabs[i], 21, i == 0);
            tx += tw + 10;
        }
        // profile chip (no fake name)
        int pw = 170, px = R - pw;
        c.fillRoundRect(px, y, pw, h, 30, theme::Surface);
        c.strokeRoundRect(px, y, pw, h, 30, 1, theme::Line);
        c.fillGradient(px + 8, y + 8, 44, 44, 22, rgba(255, 154, 98), rgba(255, 95, 143), 2);
        app.fonts.bold->draw(c, px + 62, y + 10, 19, "PS4", theme::Text);
        app.fonts.regular->draw(c, px + 62, y + 33, 15, "GoldHEN", theme::TextMute);
    }

    void drawContinuePanel(Canvas &c, Rect r)
    {
        c.fillRoundRect(r.x, r.y, r.w, r.h, 30, theme::Surface);
        c.strokeRoundRect(r.x, r.y, r.w, r.h, 30, 1, theme::Line);
        int pad = 24;
        app.fonts.bold->draw(c, r.x + pad, r.y + 22, 24, "Continuar assistindo", theme::Text);
        app.fonts.regular->draw(c, r.right() - pad - 20, r.y + 24, 18, std::to_string(continue_.size()), theme::TextMute);
        int y = r.y + 70;
        for (size_t i = 0; i < continue_.size(); i++) {
            const Cont &it = continue_[i];
            bool foc = focus_ == Zone::Cont && int(i) == contSel_ && !app.sidebarFocused();
            Rect row{r.x + 12, y, r.w - 24, 86};
            if (foc) { c.shadow(row.x, row.y, row.w, row.h, 16, 12, withAlpha(theme::Violet, 90)); c.fillRoundRect(row.x, row.y, row.w, row.h, 16, theme::SurfaceHi); }
            Rect thumb{row.x + 12, row.y + 10, 116, 66};
            drawGeneratedArt(c, app.fonts, thumb, 12, it.id + "#t", it.name, true);
            int tx = thumb.right() + 16, tw = row.right() - tx - 12;
            app.fonts.bold->draw(c, tx, row.y + 12, 19, app.fonts.bold->ellipsize(19, it.name, tw), theme::Text);
            app.fonts.regular->draw(c, tx, row.y + 36, 15, it.sub, theme::TextMute);
            drawProgress(c, tx, row.y + 60, tw, 6, it.progress, theme::Violet, withAlpha(theme::Text, 36));
            y += 94;
        }
        // now playing chip
        size_t t = app.queue.current();
        bool liveOn = app.audio.live() && (app.audio.state() == PlayState::Playing || app.audio.state() == PlayState::Paused);
        bool musicOn = (app.audio.state() == PlayState::Playing || app.audio.state() == PlayState::Paused) && t < app.music.tracks().size();
        if (liveOn || musicOn) {
            Rect np{r.x + 12, r.bottom() - 84, r.w - 24, 72};
            c.fillRoundRect(np.x, np.y, np.w, np.h, 18, withAlpha(theme::Text, 16));
            c.fillGradient(np.x + 14, np.y + 14, 46, 46, 12, theme::Violet, theme::Blue, 2);
            app.fonts.regular->draw(c, np.x + 74, np.y + 14, 15, "Tocando agora", theme::TextMute);
            std::string nm = liveOn ? app.station.name : app.music.tracks()[t].title;
            app.fonts.bold->draw(c, np.x + 74, np.y + 36, 18, app.fonts.bold->ellipsize(18, nm, np.w - 90), theme::Text);
        }
    }

    void drawHeroCard(Canvas &c, Rect r)
    {
        c.fillGradient(r.x, r.y, r.w, r.h, 34, rgba(26, 18, 54), rgba(15, 35, 72), 2);
        c.pushClip(r);
        // moon, top-right
        float mx = float(r.right() - 150), my = float(r.y + 150);
        c.fillCircle(mx + 120, my - 120, 330, rgba(124, 92, 255, 26));
        c.fillCircle(mx, my, 150, rgba(236, 232, 255, 150));
        c.fillCircle(mx + 54, my - 26, 140, withAlpha(rgba(26, 18, 54), 210));
        // left fade for text legibility (one gradient instead of ~200 per-column fills)
        c.fillGradient(r.x, r.y, int(r.w * 0.78f), r.h, 0, withAlpha(theme::Bg, 235), withAlpha(theme::Bg, 0), 0);
        c.popClip();
        c.strokeRoundRect(r.x, r.y, r.w, r.h, 34, 1, theme::Line);

        if (!hasFeatured_) {
            app.fonts.bold->draw(c, r.x + 48, r.y + r.h / 2, 34, "Adicione fontes para ver destaques", theme::TextDim);
            return;
        }
        int x = r.x + 48, bottom = r.bottom() - 48;
        // buttons (bottom), measured first so the text stacks above them
        int by = bottom - 64;
        bool foc = focus_ == Zone::Hero && !app.sidebarFocused();
        int bw = app.fonts.bold->measure(22, "Assistir") + 84;
        if (foc) c.shadow(x, by, bw, 64, 32, 16, withAlpha(theme::Violet, 150));
        c.fillRoundRect(x, by, bw, 64, 32, foc ? theme::Text : withAlpha(theme::Text, 235));
        drawIconPlay(c, x + 34, by + 32, 11, theme::Bg);
        app.fonts.bold->draw(c, x + 54, by + (64 - app.fonts.bold->lineHeight(22)) / 2, 22, "Assistir", theme::Bg);
        int lw = app.fonts.bold->measure(22, "Minha lista") + 76;
        c.fillRoundRect(x + bw + 14, by, lw, 64, 32, theme::Surface);
        c.strokeRoundRect(x + bw + 14, by, lw, 64, 32, 1, theme::Line);
        app.fonts.bold->draw(c, x + bw + 14 + 38, by + (64 - app.fonts.bold->lineHeight(22)) / 2, 22, "Minha lista", theme::Text);
        // title + meta + description stacked above the buttons
        int ty = by - 24;
        auto desc = app.fonts.regular->wrap(22, featured_.description, std::min(640, r.w - 96), 2);
        ty -= int(desc.size()) * 32;
        for (size_t i = 0; i < desc.size(); i++) app.fonts.regular->draw(c, x, ty + int(i) * 32, 22, desc[i], theme::TextDim);
        ty -= 86;
        app.fonts.bold->draw(c, x, ty, 72, app.fonts.bold->ellipsize(72, featured_.title, r.w - 96), theme::Text);
        ty -= 44;
        if (!featured_.meta.empty()) app.fonts.regular->draw(c, x, ty, 20, featured_.meta, theme::Glow);
        ty -= 42;
        std::string badge = "Em destaque";
        int bgw = app.fonts.bold->measure(17, badge) + 32;
        c.fillRoundRect(x, ty, bgw, 32, 16, withAlpha(theme::Text, 30));
        app.fonts.bold->draw(c, x + 16, ty + (32 - app.fonts.bold->lineHeight(17)) / 2, 17, badge, theme::Text);
        // carousel dots, bottom-right
        for (int i = 0; i < 4; i++) {
            int dw = i == 0 ? 32 : 8;
            c.fillRoundRect(r.right() - 48 - (i == 0 ? 0 : 24 + (i - 1) * 16) - dw, r.bottom() - 40, dw, 8, 4, i == 0 ? theme::Text : withAlpha(theme::Text, 80));
        }
    }

    void drawStrip(Canvas &c, Rect area, int top, int R)
    {
        if (rows_.empty()) return;
        Row &r = rows_[size_t(rowSel_)];
        bool rowFocus = focus_ == Zone::Rows && !app.sidebarFocused();
        int x0 = area.x + 52;
        app.fonts.bold->draw(c, x0, top, 30, r.title, rowFocus ? theme::Text : theme::TextDim);
        if (rows_.size() > 1)
            app.fonts.regular->draw(c, R - 150, top + 6, 18, "cima/baixo troca", theme::TextMute);
        Rect cs = cardSize(r.shape);
        int cy = top + 50;
        c.pushClip({area.x, cy, area.w, area.bottom() - cy});
        for (size_t k = 0; k < r.cards.size(); k++) {
            int cx = x0 + int(k) * (cs.w + 24) - int(r.scroll);
            if (cx > area.right() || cx + cs.w < area.x) continue;
            const Card &cd = r.cards[k];
            drawPosterCard(app, c, {cx, cy, cs.w, cs.h}, cd.id, cd.title, r.shape == Shape::Poster ? cd.meta : cd.subtitle,
                           rowFocus && int(k) == r.sel, cd.progress, cd.poster);
        }
        c.popClip();
    }

    void drawIconPlay(Canvas &c, int cx, int cy, int s, Color col)
    {
        float xy[6] = {float(cx - s + 3), float(cy - s), float(cx - s + 3), float(cy + s), float(cx + s), float(cy)};
        c.fillConvex(xy, 3, col);
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

    void openCont(int i)
    {
        if (i < 0 || i >= int(continue_.size())) return;
        const Cont &it = continue_[size_t(i)];
        if (it.type == "movie" || it.type == "series") app.push(makeDetail(app, it.type, it.id, it.name));
    }
};

}  // namespace

std::unique_ptr<Screen> makeHome(App &a) { return std::unique_ptr<Screen>(new HomeScreen(a)); }

}  // namespace ml
