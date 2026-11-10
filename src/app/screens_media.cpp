// Movies / Series: catalog grid with genre tabs and search, and the detail page
// (info, favorites, seasons/episodes, stream selection).
#include <algorithm>
#include <cctype>
#include <cmath>

#include "app/app.hpp"
#include "app/catalog_loader.hpp"
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

// ---------------------------------------------------------------- catalog rows
// One row per catalog of the user's sources (addons often name them after the streaming service:
// "Netflix", "Prime Video"...) plus one row per genre when a catalog offers a genre filter.
// Rows load in the background (CatalogLoader) as they come near the screen.
class CatalogScreen : public Screen {
public:
    CatalogScreen(App &a, std::string type)
        : Screen(a), type_(std::move(type)), loader_(new CatalogLoader(a.platform, a.platform.http())) {}
    std::string title() const override { return type_ == "movie" ? "Filmes" : "Series"; }

    void onEnter() override
    {
        if (!loaded_ || seenVersion_ != app.sourcesVersion) reload();
    }

    void reload()
    {
        loaded_ = true;
        seenVersion_ = app.sourcesVersion;
        loader_->cancelAll();
        rows_.clear();
        row_ = 0;
        scrollY_ = 0;
        query_.clear();
        if (app.videoDemo()) {
            std::vector<addon::MetaPreview> demo = type_ == "movie" ? demo::movies() : demo::series();
            Row all;
            all.title = "Destaques";
            all.sub = "demonstracao";
            all.items = demo;
            all.state = Row::Done;
            rows_.push_back(all);
            for (const auto &m : demo)
                for (const auto &g : m.genres) {
                    auto it = std::find_if(rows_.begin(), rows_.end(), [&](const Row &r) { return r.title == g; });
                    if (it == rows_.end()) {
                        Row r;
                        r.title = g;
                        r.sub = "demonstracao";
                        r.state = Row::Done;
                        rows_.push_back(r);
                        it = rows_.end() - 1;
                    }
                    it->items.push_back(m);
                }
        }
        size_t enabled = 0;
        for (const auto &src : app.sources.sources()) if (src.enabled) enabled++;
        std::vector<CatalogRef> refs = app.sources.browsableCatalogs(type_);
        std::vector<Row> genreRows;
        std::vector<size_t> genreDone;   // sources that already gave genre rows
        for (const auto &ref : refs) {
            const std::string &srcName = app.sources.sources()[ref.source].manifest.name;
            Row r;
            r.title = ref.decl->name.empty() ? ref.decl->id : ref.decl->name;
            r.sub = enabled > 1 ? srcName : "";
            r.url = app.sources.catalogUrl(ref, {}, &r.policy);
            if (!r.url.empty()) rows_.push_back(r);
            // genre rows from the first catalog of each source that offers a genre filter
            if (std::find(genreDone.begin(), genreDone.end(), ref.source) != genreDone.end()) continue;
            for (const auto &ex : ref.decl->extra) {
                if (ex.name != "genre" || ex.options.empty()) continue;
                genreDone.push_back(ref.source);
                for (size_t i = 0; i < ex.options.size() && i < 24; i++) {
                    Row g;
                    g.title = ex.options[i];
                    g.sub = r.title + (enabled > 1 ? " - " + srcName : "");
                    g.url = app.sources.catalogUrl(ref, {{"genre", ex.options[i]}}, &g.policy);
                    if (!g.url.empty()) genreRows.push_back(g);
                }
                break;
            }
        }
        rows_.insert(rows_.end(), genreRows.begin(), genreRows.end());
        requestNear();
    }

    void requestNear()
    {
        int from = std::max(0, row_ - 1), to = std::min(int(rows_.size()) - 1, row_ + 3);
        for (int i = from; i <= to; i++) {
            Row &r = rows_[size_t(i)];
            if (r.state != Row::Idle) continue;
            r.ticket = loader_->request(r.url, r.policy);
            r.state = r.ticket ? Row::Loading : Row::Failed;
            if (!r.ticket) r.error = "sem rede";
        }
    }

    bool handle(uint32_t p) override
    {
        if (p & BtnTriangle) {
            app.openKeyboard(type_ == "movie" ? "Buscar filmes" : "Buscar series", query_, false, [this](const std::string &q) { search(q); });
            return true;
        }
        if (p & BtnSquare) {
            if (!query_.empty() && !rows_.empty() && rows_[0].search) { rows_.erase(rows_.begin()); query_.clear(); row_ = 0; }
            return true;
        }
        if (rows_.empty()) return false;
        Row &r = rows_[size_t(row_)];
        int n = int(r.items.size());
        if (p & BtnUp) { if (row_ > 0) { row_--; requestNear(); } return true; }
        if (p & BtnDown) { if (row_ + 1 < int(rows_.size())) { row_++; requestNear(); } return true; }
        if (p & BtnLeft) { if (r.sel == 0) return false; r.sel--; return true; }
        if (p & BtnRight) { if (r.sel + 1 < n) r.sel++; return true; }
        if ((p & BtnCross) && n > 0) {
            const auto &m = r.items[size_t(r.sel)];
            app.push(makeDetail(app, m.type.empty() ? type_ : m.type, m.id, m.name));
            return true;
        }
        if ((p & BtnCross) && r.state == Row::Failed) {   // try a failed row again
            r.state = Row::Idle;
            requestNear();
            return true;
        }
        return false;
    }

    void search(const std::string &q)
    {
        if (!rows_.empty() && rows_[0].search) rows_.erase(rows_.begin());
        query_ = q;
        if (q.empty()) return;
        Row r;
        r.title = "Busca: " + q;
        r.search = true;
        r.state = Row::Done;
        std::string lq = lower(q);
        for (const auto &row : rows_)   // what is already loaded
            for (const auto &m : row.items)
                if (lower(m.name).find(lq) != std::string::npos &&
                    std::none_of(r.items.begin(), r.items.end(), [&](const addon::MetaPreview &x) { return x.id == m.id; }))
                    r.items.push_back(m);
        std::vector<addon::MetaPreview> found;   // sources that support search
        std::string e = app.sources.search(type_, q, found);
        for (auto &m : found)
            if (std::none_of(r.items.begin(), r.items.end(), [&](const addon::MetaPreview &x) { return x.id == m.id; })) r.items.push_back(m);
        if (r.items.empty()) { r.state = Row::Failed; r.error = "nada encontrado"; }
        rows_.insert(rows_.begin(), r);
        row_ = 0;
        scrollY_ = 0;
    }

    void update(float dt) override
    {
        bool got = false;
        for (auto &res : loader_->take())
            for (auto &r : rows_)
                if (r.ticket == res.ticket && r.state == Row::Loading) {
                    r.items = std::move(res.metas);
                    r.error = res.error;
                    r.state = res.error.empty() ? Row::Done : Row::Failed;
                    got = true;
                }
        if (got) app.dirty();
        float k = app.settings.animations ? std::min(1.0f, dt * 14.0f) : 1.0f;
        float ty = float(std::max(0, row_ - 0) * kRowH);
        anim_ = std::fabs(ty - scrollY_) > 0.5f;
        scrollY_ = anim_ ? scrollY_ + (ty - scrollY_) * k : ty;
        for (auto &r : rows_) {
            float tx = float(std::max(0, r.sel - 2) * kStep);
            if (std::fabs(tx - r.scrollX) > 0.5f) { r.scrollX += (tx - r.scrollX) * k; anim_ = true; }
            else r.scrollX = tx;
        }
    }
    bool animating() const override { return anim_ || loading(); }
    bool loading() const
    {
        for (const auto &r : rows_) if (r.state == Row::Loading) return true;
        return false;
    }

    void draw(Canvas &c, Rect area) override
    {
        std::string sub = std::to_string(rows_.size()) + (rows_.size() == 1 ? " lista" : " listas");
        if (loading()) sub += "  -  carregando...";
        drawSectionHeader(app, c, area, title(), sub);
        if (rows_.empty()) {
            app.fonts.regular->draw(c, area.x + theme::Margin, 260, 28,
                                    "Nenhum catalogo. Adicione uma fonte em Fontes (ou ative a demonstracao).", theme::TextDim);
            return;
        }
        const int top = 176;
        c.pushClip({area.x, top, area.w, area.bottom() - top});
        for (size_t i = 0; i < rows_.size(); i++) {
            int y = top + int(i) * kRowH - int(scrollY_);
            if (y > area.bottom() || y + kRowH < top) continue;
            const Row &r = rows_[i];
            bool focusRow = int(i) == row_ && !app.sidebarFocused();
            int x0 = area.x + theme::Margin;
            app.fonts.bold->draw(c, x0, y, 30, r.title, focusRow ? theme::Text : theme::TextDim);
            if (!r.sub.empty()) {
                int tw = app.fonts.bold->measure(30, r.title);
                app.fonts.regular->draw(c, x0 + tw + 18, y + 8, 21, r.sub, theme::TextMute);
            }
            int cy = y + 56;
            if (r.state == Row::Loading || r.state == Row::Idle) {
                for (int k = 0; k < 6; k++) c.fillRoundRect(x0 + k * kStep, cy, kCardW, kCardH, 16, theme::Surface);
                app.fonts.regular->draw(c, x0 + 24, cy + kCardH / 2 - 14, 24, "Carregando...", theme::TextMute);
                continue;
            }
            if (r.items.empty()) {
                std::string msg = r.state == Row::Failed ? "Nao carregou: " + r.error + (focusRow ? "  (X tenta de novo)" : "") : "Lista vazia";
                app.fonts.regular->draw(c, x0, cy + 20, 23, app.fonts.regular->ellipsize(23, msg, area.w - 2 * theme::Margin), theme::TextMute);
                continue;
            }
            c.pushClip({area.x, y + 40, area.w, kRowH});
            for (size_t k = 0; k < r.items.size(); k++) {
                int cx = x0 + int(k) * kStep - int(r.scrollX);
                if (cx > area.right() || cx + kCardW < area.x) continue;
                const auto &m = r.items[k];
                std::string s2 = m.releaseInfo + (m.imdbRating.empty() ? "" : "  -  " + m.imdbRating);
                drawPosterCard(app, c, {cx, cy, kCardW, kCardH}, m.id, m.name, s2, focusRow && int(k) == r.sel, -1, m.poster);
            }
            c.popClip();
        }
        c.popClip();
    }

    Hints hints() const override
    {
        Hints h{{BtnCross, "Detalhes"}, {BtnTriangle, "Buscar"}};
        if (!query_.empty()) h.push_back({BtnSquare, "Limpar busca"});
        return h;
    }

private:
    struct Row {
        enum State { Idle, Loading, Done, Failed } state = Idle;
        std::string title, sub, url, error;
        UrlPolicy policy;
        std::vector<addon::MetaPreview> items;
        int ticket = 0, sel = 0;
        float scrollX = 0;
        bool search = false;
    };
    static constexpr int kCardW = 212, kCardH = 318, kStep = 240, kRowH = 440;
    std::string type_;
    std::unique_ptr<CatalogLoader> loader_;
    bool loaded_ = false, anim_ = false;
    int seenVersion_ = -1, row_ = 0;
    float scrollY_ = 0;
    std::vector<Row> rows_;
    std::string query_;
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
        if (const Image *img = app.images.get(meta_.poster, poster.w, poster.h)) c.drawImage(*img, poster.x, poster.y, 18);
        else drawGeneratedArt(c, app.fonts, poster, 18, meta_.id, meta_.name, true);
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
            a->playVideo(s.url, s.name.empty() ? "Video" : s.name);
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
