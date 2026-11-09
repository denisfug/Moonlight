// Movies / Series: catalog grid with genre tabs and search, and the detail page
// (info, favorites, seasons/episodes, stream selection).
#include <algorithm>
#include <cctype>
#include <cmath>

#include "app/app.hpp"
#include "app/demo_data.hpp"
#include "ui/theme.hpp"

namespace ml {

using namespace ui;

namespace {

std::string lower(std::string s)
{
    for (auto &ch : s) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

// ---------------------------------------------------------------- catalog grid
class CatalogScreen : public Screen {
public:
    CatalogScreen(App &a, std::string type) : Screen(a), type_(std::move(type)) {}
    std::string title() const override { return type_ == "movie" ? "Filmes" : "Series"; }

    void onEnter() override
    {
        if (!loaded_) reload();
    }

    void reload()
    {
        loaded_ = true;
        all_.clear();
        notes_.clear();
        if (app.settings.showDemo) all_ = type_ == "movie" ? demo::movies() : demo::series();
        for (const auto &ref : app.sources.browsableCatalogs(type_)) {
            std::vector<addon::MetaPreview> metas;
            std::string e = app.sources.fetchCatalog(ref, metas);
            if (!e.empty()) { notes_ = app.sources.sources()[ref.source].manifest.name + ": " + e; continue; }
            all_.insert(all_.end(), metas.begin(), metas.end());
        }
        genres_ = {"Todos"};
        for (const auto &m : all_)
            for (const auto &g : m.genres)
                if (std::find(genres_.begin(), genres_.end(), g) == genres_.end()) genres_.push_back(g);
        filter();
    }

    void filter()
    {
        shown_.clear();
        for (size_t i = 0; i < all_.size(); i++) {
            const auto &m = all_[i];
            if (genre_ > 0 && std::find(m.genres.begin(), m.genres.end(), genres_[size_t(genre_)]) == m.genres.end()) continue;
            if (!query_.empty() && lower(m.name).find(lower(query_)) == std::string::npos) continue;
            shown_.push_back(i);
        }
        sel_ = 0;
    }

    bool handle(uint32_t p) override
    {
        int n = int(shown_.size());
        if (p & (BtnL1 | BtnR1)) {
            int g = int(genres_.size());
            genre_ = (genre_ + ((p & BtnR1) ? 1 : g - 1)) % g;
            filter();
            return true;
        }
        if (p & BtnTriangle) {
            app.openKeyboard(type_ == "movie" ? "Buscar filmes" : "Buscar series", query_, false, [this](const std::string &q) {
                query_ = q;
                // also ask the user's sources that support search
                if (!q.empty()) {
                    std::vector<addon::MetaPreview> found;
                    if (app.sources.search(type_, q, found).empty())
                        for (auto &m : found)
                            if (std::none_of(all_.begin(), all_.end(), [&](const addon::MetaPreview &x) { return x.id == m.id; }))
                                all_.push_back(m);
                }
                filter();
            });
            return true;
        }
        if (p & BtnSquare) { if (!query_.empty()) { query_.clear(); filter(); } return true; }
        if (n == 0) return false;
        if (p & BtnLeft) { if (sel_ % kCols == 0) return false; sel_--; return true; }
        if (p & BtnRight) { if (sel_ + 1 < n && sel_ % kCols != kCols - 1) sel_++; return true; }
        if (p & BtnUp) { if (sel_ >= kCols) sel_ -= kCols; return true; }
        if (p & BtnDown) { if (sel_ + kCols < n) sel_ += kCols; else if (sel_ / kCols < (n - 1) / kCols) sel_ = n - 1; return true; }
        if (p & BtnCross) {
            const auto &m = all_[shown_[size_t(sel_)]];
            app.push(makeDetail(app, m.type.empty() ? type_ : m.type, m.id, m.name));
            return true;
        }
        return false;
    }

    void update(float dt) override
    {
        float target = float(std::max(0, sel_ / kCols - 1) * kRowH);
        float k = app.settings.animations ? std::min(1.0f, dt * 14.0f) : 1.0f;
        anim_ = std::fabs(target - scroll_) > 0.5f;
        scroll_ = anim_ ? scroll_ + (target - scroll_) * k : target;
    }
    bool animating() const override { return anim_; }

    void draw(Canvas &c, Rect area) override
    {
        std::string sub = std::to_string(shown_.size()) + " titulos";
        if (!query_.empty()) sub += "  -  busca: \"" + query_ + "\"";
        if (!notes_.empty()) sub += "  -  " + notes_;
        drawSectionHeader(app, c, area, title(), sub);
        // genre tabs
        int x = area.x + theme::Margin, y = 176;
        for (size_t i = 0; i < genres_.size(); i++) {
            bool on = int(i) == genre_;
            int w = app.fonts.bold->measure(22, genres_[i]) + 44;
            if (on) c.fillGradient(x, y, w, 48, 24, theme::Violet, theme::Indigo, 0);
            else c.fillRoundRect(x, y, w, 48, 24, theme::Surface);
            (on ? app.fonts.bold : app.fonts.regular)->draw(c, x + 22, y + (48 - app.fonts.bold->lineHeight(22)) / 2, 22, genres_[i], on ? theme::Text : theme::TextDim);
            x += w + 12;
        }
        if (shown_.empty()) {
            app.fonts.regular->draw(c, area.x + theme::Margin, 300, 28,
                                    all_.empty() ? "Nenhum titulo. Ative a demonstracao ou adicione uma fonte em Fontes." : "Nada encontrado com esse filtro.",
                                    theme::TextDim);
            return;
        }
        const int top = 270;
        c.pushClip({area.x, top - 30, area.w, area.bottom() - top + 30});
        for (size_t i = 0; i < shown_.size(); i++) {
            int col = int(i) % kCols, row = int(i) / kCols;
            int cx = area.x + theme::Margin + col * (kCardW + 36);
            int cy = top + row * kRowH - int(scroll_);
            if (cy > area.bottom() || cy + kRowH < top - 30) continue;
            const auto &m = all_[shown_[i]];
            std::string sub = m.releaseInfo + (m.imdbRating.empty() ? "" : "  -  " + m.imdbRating);
            drawPosterCard(app, c, {cx, cy, kCardW, kCardH}, m.id, m.name, sub, int(i) == sel_ && !app.sidebarFocused());
        }
        c.popClip();
    }

    Hints hints() const override
    {
        Hints h{{BtnCross, "Detalhes"}, {BtnL1 | BtnR1, "Genero"}, {BtnTriangle, "Buscar"}};
        if (!query_.empty()) h.push_back({BtnSquare, "Limpar busca"});
        return h;
    }

private:
    static constexpr int kCols = 6, kCardW = 212, kCardH = 318, kRowH = 400;
    std::string type_;
    bool loaded_ = false, anim_ = false;
    std::vector<addon::MetaPreview> all_;
    std::vector<size_t> shown_;
    std::vector<std::string> genres_{"Todos"};
    int genre_ = 0, sel_ = 0;
    float scroll_ = 0;
    std::string query_, notes_;
};

// ---------------------------------------------------------------- detail
class DetailScreen : public Screen {
public:
    DetailScreen(App &a, std::string type, std::string id, std::string name)
        : Screen(a), type_(std::move(type)), id_(std::move(id)), fallbackName_(std::move(name)) {}
    std::string title() const override { return meta_.name.empty() ? fallbackName_ : meta_.name; }

    void onEnter() override
    {
        if (loaded_) return;
        loaded_ = true;
        if (demo::isDemo(id_)) demo::meta(id_, meta_);
        else error_ = app.sources.fetchMeta(type_, id_, meta_);
        if (meta_.name.empty()) meta_.name = fallbackName_;
        seasons_ = meta_.seasons();
        // resume where the user stopped
        if (const LibraryItem *li = app.library.progressFor(id_))
            for (size_t i = 0; i < seasons_.size(); i++) if (seasons_[i] == li->season) season_ = int(i);
    }

    std::vector<const addon::Video *> episodes() const
    {
        std::vector<const addon::Video *> v;
        if (seasons_.empty()) return v;
        for (const auto &e : meta_.videos) if (e.season == seasons_[size_t(season_)]) v.push_back(&e);
        return v;
    }

    bool handle(uint32_t p) override
    {
        auto eps = episodes();
        bool isSeries = !seasons_.empty();
        if (zone_ == 0) {
            if (p & BtnLeft) { if (btn_ == 0) return false; btn_--; return true; }
            if (p & BtnRight) { if (btn_ < 1) btn_++; return true; }
            if ((p & BtnDown) && isSeries) { zone_ = 1; return true; }
            if (p & BtnCross) {
                if (btn_ == 1) {
                    LibraryItem it;
                    it.id = id_; it.type = type_; it.name = meta_.name;
                    app.library.toggleFavorite(it, int64_t(app.now()));
                    app.toast(app.library.isFavorite(id_) ? "Adicionado aos favoritos" : "Removido dos favoritos");
                } else if (isSeries) {
                    const LibraryItem *li = app.library.progressFor(id_);
                    playVideo(li ? li->videoId : (eps.empty() ? "" : eps[0]->id));
                } else {
                    playVideo(id_);
                }
                return true;
            }
            return false;
        }
        if (p & (BtnL1 | BtnR1)) {
            int n = int(seasons_.size());
            season_ = (season_ + ((p & BtnR1) ? 1 : n - 1)) % n;
            ep_ = 0;
            return true;
        }
        if (zone_ == 1) {   // season tabs
            if (p & BtnLeft) { if (season_ == 0) return false; season_--; ep_ = 0; return true; }
            if (p & BtnRight) { if (season_ + 1 < int(seasons_.size())) { season_++; ep_ = 0; } return true; }
            if (p & BtnUp) { zone_ = 0; return true; }
            if (p & BtnDown) { if (!eps.empty()) zone_ = 2; return true; }
            return false;
        }
        // episodes
        if (p & BtnUp) { if (ep_ == 0) zone_ = 1; else ep_--; return true; }
        if (p & BtnDown) { if (ep_ + 1 < int(eps.size())) ep_++; return true; }
        if ((p & BtnCross) && ep_ < int(eps.size())) { playVideo(eps[size_t(ep_)]->id); return true; }
        return false;
    }

    void update(float dt) override
    {
        float target = float(std::max(0, ep_ - 3) * 96);
        float k = app.settings.animations ? std::min(1.0f, dt * 14.0f) : 1.0f;
        anim_ = std::fabs(target - scroll_) > 0.5f;
        scroll_ = anim_ ? scroll_ + (target - scroll_) * k : target;
    }
    bool animating() const override { return anim_; }

    void draw(Canvas &c, Rect area) override
    {
        Rect bg{area.x, 0, area.w, 560};
        drawGeneratedArt(c, app.fonts, bg, 0, meta_.id + "#bg", meta_.name, false);
        c.fillGradient(area.x, 0, area.w, 560, 0, withAlpha(theme::Bg, 245), withAlpha(theme::Bg, 40), 0);
        c.fillGradient(area.x, 360, area.w, 200, 0, withAlpha(theme::Bg, 0), theme::Bg, 1);
        c.fillRect(area.x, 560, area.w, area.h - 560, theme::Bg);
        int x = area.x + theme::Margin;
        Rect poster{x, 70, 260, 390};
        c.shadow(poster.x, poster.y, poster.w, poster.h, 18, 30, rgba(0, 0, 0, 160));
        drawGeneratedArt(c, app.fonts, poster, 18, meta_.id, meta_.name, true);
        int tx = x + 310;
        app.fonts.bold->draw(c, tx, 70, 60, app.fonts.bold->ellipsize(60, meta_.name, area.right() - tx - 60), theme::Text);
        std::string line;
        auto add = [&](const std::string &s) { if (!s.empty()) line += (line.empty() ? "" : "   -   ") + s; };
        add(meta_.releaseInfo);
        add(meta_.runtime);
        if (!meta_.genres.empty()) add(meta_.genres[0]);
        if (!meta_.imdbRating.empty()) add("nota " + meta_.imdbRating);
        if (!seasons_.empty()) add(std::to_string(seasons_.size()) + (seasons_.size() == 1 ? " temporada" : " temporadas"));
        app.fonts.regular->draw(c, tx, 152, 24, line, theme::Glow);
        auto lines = app.fonts.regular->wrap(25, meta_.description.empty() ? error_ : meta_.description, area.right() - tx - 80, 4);
        int y = 200;
        for (const auto &l : lines) { app.fonts.regular->draw(c, tx, y, 25, l, theme::TextDim); y += 36; }
        if (!meta_.cast.empty()) {
            std::string cast = "Elenco: ";
            for (size_t i = 0; i < meta_.cast.size() && i < 4; i++) cast += (i ? ", " : "") + meta_.cast[i];
            app.fonts.regular->draw(c, tx, y + 8, 21, cast, theme::TextMute);
        }
        if (demo::isDemo(id_)) {
            int w = app.fonts.bold->measure(16, "DEMONSTRACAO") + 24;
            c.fillRoundRect(tx, 34, w, 26, 13, rgba(251, 191, 36, 40));
            app.fonts.bold->draw(c, tx + 12, 34 + (26 - app.fonts.bold->lineHeight(16)) / 2, 16, "DEMONSTRACAO", theme::Warn);
        }
        // buttons
        const LibraryItem *li = app.library.progressFor(id_);
        std::string playLabel = li && li->positionMs > 0 ? (seasons_.empty() ? "Continuar" : "Continuar T" + std::to_string(li->season) + "E" + std::to_string(li->episode)) : "Assistir";
        bool fav = app.library.isFavorite(id_);
        drawButton(c, tx, 412, playLabel, Icon::Play, zone_ == 0 && btn_ == 0);
        drawButton(c, tx + 300, 412, fav ? "Nos favoritos" : "Favoritar", fav ? Icon::HeartFilled : Icon::Heart, zone_ == 0 && btn_ == 1);
        if (seasons_.empty()) return;
        // seasons
        int sx = x;
        for (size_t i = 0; i < seasons_.size(); i++) {
            std::string s = seasons_[i] == 0 ? "Especiais" : "Temporada " + std::to_string(seasons_[i]);
            bool on = int(i) == season_;
            int w = app.fonts.bold->measure(22, s) + 44;
            if (on && zone_ == 1) { c.shadow(sx, 590, w, 50, 25, 16, withAlpha(theme::Violet, 120)); c.fillGradient(sx, 590, w, 50, 25, theme::Violet, theme::Blue, 0); }
            else if (on) c.fillRoundRect(sx, 590, w, 50, 25, theme::SurfaceHi);
            else c.fillRoundRect(sx, 590, w, 50, 25, theme::Surface);
            (on ? app.fonts.bold : app.fonts.regular)->draw(c, sx + 22, 590 + (50 - app.fonts.bold->lineHeight(22)) / 2, 22, s, on ? theme::Text : theme::TextDim);
            sx += w + 12;
        }
        auto eps = episodes();
        c.pushClip({area.x, 660, area.w, area.bottom() - 660});
        int ey = 668 - int(scroll_);
        for (size_t i = 0; i < eps.size(); i++, ey += 96) {
            if (ey + 96 < 660 || ey > area.bottom()) continue;
            bool f = zone_ == 2 && int(i) == ep_;
            Rect r{x, ey, area.w - 2 * theme::Margin, 84};
            if (f) { c.shadow(r.x, r.y, r.w, r.h, 16, 16, withAlpha(theme::Violet, 100)); c.fillRoundRect(r.x, r.y, r.w, r.h, 16, theme::SurfaceHi);
                     c.strokeRoundRect(r.x, r.y, r.w, r.h, 16, 2, rgba(196, 181, 253)); }
            else c.fillRoundRect(r.x, r.y, r.w, r.h, 16, theme::Surface);
            drawGeneratedArt(c, app.fonts, {r.x + 10, r.y + 10, 114, 64}, 10, eps[i]->id, "", false);
            app.fonts.bold->draw(c, r.x + 146, r.y + 12, 24, std::to_string(eps[i]->episode) + ". " + eps[i]->title, theme::Text);
            app.fonts.regular->draw(c, r.x + 146, r.y + 46, 19, app.fonts.regular->ellipsize(19, eps[i]->overview, r.w - 420), theme::TextMute);
            const LibraryItem *p = app.library.progressFor(id_, eps[i]->id);
            if (p && p->durationMs > 0) drawProgress(c, r.right() - 240, r.y + 38, 200, 8, p->progress(), theme::Blue, theme::SurfaceHi);
        }
        c.popClip();
    }

    Hints hints() const override
    {
        Hints h{{BtnCross, "Selecionar"}, {BtnCircle, "Voltar"}};
        if (!seasons_.empty()) h.insert(h.begin() + 1, {BtnL1 | BtnR1, "Temporada"});
        return h;
    }

private:
    std::string type_, id_, fallbackName_, error_;
    addon::MetaDetail meta_;
    std::vector<int> seasons_;
    bool loaded_ = false, anim_ = false;
    int zone_ = 0, btn_ = 0, season_ = 0, ep_ = 0;
    float scroll_ = 0;

    void drawButton(Canvas &c, int x, int y, const std::string &label, Icon icon, bool focused)
    {
        int w = 280, h = 66;
        if (focused) { c.shadow(x, y, w, h, 33, 22, withAlpha(theme::Violet, 140)); c.fillGradient(x, y, w, h, 33, theme::Violet, theme::Blue, 0); }
        else c.fillRoundRect(x, y, w, h, 33, rgba(255, 255, 255, 30));
        drawIcon(c, icon, float(x + 44), float(y + h / 2), 24, theme::Text);
        app.fonts.bold->draw(c, x + 74, y + (h - app.fonts.bold->lineHeight(24)) / 2, 24, app.fonts.bold->ellipsize(24, label, w - 90), theme::Text);
    }

    void playVideo(const std::string &videoId)
    {
        if (videoId.empty()) return;
        if (demo::isDemo(id_)) {
            app.modal("Conteudo de demonstracao",
                      "Este titulo faz parte do catalogo de demonstracao e nao tem video. Adicione uma fonte autorizada em "
                      "Fontes para ver opcoes de reproducao reais.");
            return;
        }
        std::vector<std::string> errs;
        auto streams = app.sources.fetchStreams(type_, videoId, &errs);
        if (streams.empty()) {
            app.modal("Nenhuma opcao de reproducao", errs.empty() ? "Nenhuma fonte ativa ofereceu streams para este titulo." : errs[0]);
            return;
        }
        std::vector<std::string> labels;
        for (size_t i = 0; i < streams.size() && i < 3; i++) labels.push_back(streams[i].name.empty() ? "Opcao " + std::to_string(i + 1) : streams[i].name);
        std::string body;
        for (size_t i = 0; i < streams.size() && i < 3; i++) {
            const auto &s = streams[i];
            body += labels[i] + ": " + (s.description.empty() ? "-" : s.description);
            if (!s.playableHere()) body += s.kind == addon::StreamKind::Torrent ? " (P2P: nao suportado)" : " (formato nao suportado)";
            body += "\n";
        }
        auto st = std::make_shared<std::vector<addon::Stream>>(streams);
        App *a = &app;
        app.modal("Escolha a fonte", body, labels, [a, st](int choice) {
            if (choice < 0) return;
            const auto &s = (*st)[size_t(choice)];
            if (!s.playableHere()) { a->modal("Nao suportado", "Esse tipo de stream nao e suportado pelo Moonlight HB."); return; }
            VideoPlayer vp;
            std::string err;
            if (!vp.open(s.url, &err)) a->modal("Player de video", err);
        });
    }
};

}  // namespace

std::unique_ptr<Screen> makeCatalog(App &a, const std::string &type) { return std::unique_ptr<Screen>(new CatalogScreen(a, type)); }
std::unique_ptr<Screen> makeDetail(App &a, const std::string &type, const std::string &id, const std::string &name)
{
    return std::unique_ptr<Screen>(new DetailScreen(a, type, id, name));
}

}  // namespace ml
