// Movies / Series: catalog grid with genre tabs and search, and the detail page
// (info, favorites, seasons/episodes, stream selection).
#include <algorithm>
#include <cctype>
#include <cmath>

#include "app/app.hpp"
#include "app/catalog_loader.hpp"
#include "app/multi_fetch.hpp"
#include "player/sniff.hpp"
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

    // One tab per source (plus the demonstration while it is shown): only the selected source's
    // catalogs are listed and loaded, so a screen never asks every source for everything at once.
    void reload()
    {
        loaded_ = true;
        seenVersion_ = app.sourcesVersion;
        std::string keep = tab_ < int(tabs_.size()) ? tabs_[size_t(tab_)].name : "";
        tabs_.clear();
        if (app.videoDemo()) tabs_.push_back({"Demonstracao", -1});
        for (const auto &ref : app.sources.browsableCatalogs(type_)) {
            int src = int(ref.source);
            if (std::none_of(tabs_.begin(), tabs_.end(), [&](const Tab &t) { return t.source == src; }))
                tabs_.push_back({app.sources.sources()[ref.source].manifest.name, src});
        }
        tab_ = 0;
        for (size_t i = 0; i < tabs_.size(); i++) if (tabs_[i].name == keep) tab_ = int(i);
        tabFocus_ = false;
        buildRows();
    }

    void buildRows()
    {
        loader_->cancelAll();
        rows_.clear();
        row_ = 0;
        scrollY_ = 0;
        query_.clear();
        if (tabs_.empty()) return;
        const Tab &tab = tabs_[size_t(tab_)];
        if (tab.source < 0) {
            std::vector<addon::MetaPreview> demo = type_ == "movie" ? demo::movies() : demo::series();
            Row all;
            all.title = "Destaques";
            all.items = demo;
            all.state = Row::Done;
            rows_.push_back(all);
            for (const auto &m : demo)
                for (const auto &g : m.genres) {
                    auto it = std::find_if(rows_.begin(), rows_.end(), [&](const Row &r) { return r.title == g; });
                    if (it == rows_.end()) {
                        Row r;
                        r.title = g;
                        r.state = Row::Done;
                        rows_.push_back(r);
                        it = rows_.end() - 1;
                    }
                    it->items.push_back(m);
                }
            return;
        }
        std::vector<Row> genreRows;
        bool genreDone = false;
        for (const auto &ref : app.sources.browsableCatalogs(type_)) {
            if (int(ref.source) != tab.source) continue;
            Row r;
            r.title = ref.decl->name.empty() ? ref.decl->id : ref.decl->name;
            r.url = app.sources.catalogUrl(ref, {}, &r.policy);
            if (!r.url.empty()) rows_.push_back(r);
            if (genreDone) continue;   // genre rows from the first catalog that offers a genre filter
            for (const auto &ex : ref.decl->extra) {
                if (ex.name != "genre" || ex.options.empty()) continue;
                genreDone = true;
                for (size_t i = 0; i < ex.options.size() && i < 24; i++) {
                    Row g;
                    g.title = ex.options[i];
                    g.sub = r.title;
                    g.url = app.sources.catalogUrl(ref, {{"genre", ex.options[i]}}, &g.policy);
                    if (!g.url.empty()) genreRows.push_back(g);
                }
                break;
            }
        }
        rows_.insert(rows_.end(), genreRows.begin(), genreRows.end());
        requestNear();
    }

    void selectTab(int t)
    {
        if (tabs_.empty()) return;
        int n = int(tabs_.size());
        tab_ = ((t % n) + n) % n;
        buildRows();
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
        if ((p & (BtnL1 | BtnR1)) && tabs_.size() > 1) { selectTab(tab_ + ((p & BtnR1) ? 1 : -1)); return true; }
        if (tabFocus_) {   // on the row of sources
            if (p & BtnLeft) { if (tab_ == 0) return false; selectTab(tab_ - 1); return true; }
            if (p & BtnRight) { if (tab_ + 1 < int(tabs_.size())) selectTab(tab_ + 1); return true; }
            if (p & (BtnDown | BtnCross)) { tabFocus_ = false; return true; }
            return p & BtnUp ? true : false;
        }
        if (rows_.empty()) {
            if ((p & BtnUp) && !tabs_.empty()) { tabFocus_ = true; return true; }
            return false;
        }
        Row &r = rows_[size_t(row_)];
        int n = int(r.items.size());
        if (p & BtnUp) {
            if (row_ > 0) { row_--; requestNear(); }
            else if (!tabs_.empty()) tabFocus_ = true;
            return true;
        }
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
        if (!tabs_.empty()) sub = tabs_[size_t(tab_)].name + "   -   " + sub;
        drawSectionHeader(app, c, area, title(), sub);
        // sources on top: the selected one is filled; the catalog below is only that source's
        int top = 176;
        if (!tabs_.empty()) {
            int tx = area.x + theme::Margin;
            for (size_t i = 0; i < tabs_.size(); i++) {
                std::string name = app.fonts.bold->ellipsize(24, tabs_[i].name, 380);
                int tw = app.fonts.bold->measure(24, name) + 44;
                if (tx + tw > area.right() - theme::Margin) break;   // the rest is reachable with L1/R1
                bool on = int(i) == tab_;
                Rect chip{tx, 170, tw, 50};
                if (on) c.fillGradient(chip.x, chip.y, chip.w, chip.h, 25, theme::Violet, theme::Blue, 0);
                else c.fillRoundRect(chip.x, chip.y, chip.w, chip.h, 25, theme::Surface);
                if (on && tabFocus_ && !app.sidebarFocused()) c.strokeRoundRect(chip.x - 3, chip.y - 3, chip.w + 6, chip.h + 6, 28, 3, rgba(255, 255, 255));
                app.fonts.bold->draw(c, chip.x + 22, chip.y + 10, 24, name, on ? theme::Text : theme::TextDim);
                tx += tw + 14;
            }
            top = 244;
        }
        if (rows_.empty()) {
            app.fonts.regular->draw(c, area.x + theme::Margin, top + 30, 28,
                                    tabs_.empty() ? "Nenhum catalogo. Adicione uma fonte em Fontes (ou ative a demonstracao)."
                                                  : "Esta fonte nao tem catalogo aqui.",
                                    theme::TextDim);
            return;
        }
        c.pushClip({area.x, top, area.w, area.bottom() - top});
        for (size_t i = 0; i < rows_.size(); i++) {
            int y = top + int(i) * kRowH - int(scrollY_);
            if (y > area.bottom() || y + kRowH < top) continue;
            const Row &r = rows_[i];
            bool focusRow = int(i) == row_ && !app.sidebarFocused() && !tabFocus_;
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
        if (tabs_.size() > 1) h.push_back({BtnL1 | BtnR1, "Trocar de fonte"});
        if (!query_.empty()) h.push_back({BtnSquare, "Limpar busca"});
        return h;
    }

    std::string describe() const override
    {
        return "aba=" + (tabs_.empty() ? std::string("-") : tabs_[size_t(tab_)].name) + " abas=" + std::to_string(tabs_.size()) +
               " linhas=" + std::to_string(rows_.size()) + (tabFocus_ ? " foco=abas" : "");
    }

private:
    struct Tab {
        std::string name;
        int source;   // index in the source list; -1 = demonstration
    };
    std::vector<Tab> tabs_;
    int tab_ = 0;
    bool tabFocus_ = false;
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

// ---------------------------------------------------------------- playback options
// Asks every source for links at the same time, in the background, and lists them as they arrive.
// Each source's line says whether it answered, how many links it gave and how long it took.
// Links the PS4 cannot play (P2P, MKV/AVI...) are hidden behind a count (square shows them), so the
// list only offers what has a chance; external links and YouTube are shown as a QR code for the phone.
// When nothing playable comes back, the title goes to the problem log with the reason.
class StreamPickerScreen : public Screen {
public:
    StreamPickerScreen(App &a, std::string type, std::string videoId, std::string title, std::string poster)
        : Screen(a), type_(std::move(type)), videoId_(std::move(videoId)), title_(std::move(title)), poster_(std::move(poster)) {}
    std::string title() const override { return "Opcoes de reproducao"; }

    void onEnter() override
    {
        if (started_) return;
        started_ = true;
        startedMs_ = app.now();
        std::vector<MultiFetch::Item> items;
        for (const auto &r : app.sources.resourceRequests("stream", type_, videoId_)) {
            items.push_back({r.source, r.url, r.policy, kTimeoutMs});
            Src src;
            src.label = r.source;
            srcs_.push_back(src);
        }
        if (!items.empty()) fetch_ = MultiFetch::start(app.platform, std::move(items));
    }

    void update(float) override
    {
        if (!fetch_) return;
        bool changed = false;
        for (size_t i = 0; i < srcs_.size(); i++) {
            Src &src = srcs_[i];
            if (src.state != MultiFetch::State::Pending) continue;
            Json j;
            std::string e;
            int secs = 0;
            MultiFetch::State st = fetch_->poll(i, &j, &e, &secs);
            if (st == MultiFetch::State::Pending) continue;
            src.seconds = secs;
            src.state = st;
            if (st == MultiFetch::State::Done) {
                std::vector<addon::Stream> part;
                addon::ParseResult r = addon::parseStreams(j, part);
                if (!r.ok()) { src.state = MultiFetch::State::Failed; src.error = r.error; }
                src.count = int(part.size());
                for (auto &p : part) {
                    Entry en;
                    en.id = nextId_++;
                    en.source = src.label;
                    classify(p, en);
                    if (p.name.empty()) p.name = src.label;
                    en.s = std::move(p);
                    entries_.push_back(std::move(en));
                }
            } else {
                src.error = e;
            }
            changed = true;
        }
        if (changed) {
            // what the PS4 can most likely play first (MP4); source order otherwise
            std::stable_sort(entries_.begin(), entries_.end(), [](const Entry &a, const Entry &b) { return a.rank < b.rank; });
            rebuildView();
            app.dirty();
        }
        bool pending = false;
        for (const auto &src : srcs_) pending = pending || src.state == MultiFetch::State::Pending;
        if (!pending) {
            fetch_.reset();
            if (playable() == 0 && !noted_) noteNothingPlayable();
        }
    }
    bool animating() const override { return fetch_ != nullptr; }   // elapsed seconds + polling

    bool handle(uint32_t p) override
    {
        int n = int(view_.size());
        if (p & BtnUp) { if (sel_ > 0) { sel_--; userMoved_ = true; } return true; }
        if (p & BtnDown) { if (sel_ + 1 < n) { sel_++; userMoved_ = true; } return true; }
        if (p & BtnSquare) {
            if (hiddenCount() > 0 || showHidden_) { showHidden_ = !showHidden_; rebuildView(); }
            return true;
        }
        if ((p & BtnCross) && sel_ < n) { play(entries_[size_t(view_[size_t(sel_)])]); return true; }
        return false;
    }

    void draw(Canvas &c, Rect area) override
    {
        std::string sub = title_;
        if (!view_.empty()) sub += "   -   " + std::to_string(view_.size()) + (view_.size() == 1 ? " opcao" : " opcoes");
        drawSectionHeader(app, c, area, "Opcoes de reproducao", app.fonts.regular->ellipsize(24, sub, area.w - 2 * theme::Margin));
        int x = area.x + theme::Margin, w = area.w - 2 * theme::Margin;
        int y = 176;
        int waited = int((app.now() - startedMs_) / 1000);
        if (srcs_.empty()) {
            app.fonts.regular->draw(c, x, y, 24, "Nenhuma fonte ativa oferece links para este titulo. Veja suas fontes em Fontes.", theme::TextDim);
            return;
        }
        for (const auto &src : srcs_) {   // one status line per source
            std::string t = src.label + ": ";
            Color col = theme::TextDim;
            if (src.state == MultiFetch::State::Pending) {
                t += "procurando... " + std::to_string(waited) + " s (espera ate " + std::to_string(kTimeoutMs / 1000) + " s)";
            } else if (src.state == MultiFetch::State::Failed) {
                t += src.error + " (" + std::to_string(src.seconds) + " s)";
                col = theme::Warn;
            } else if (src.count == 0) {
                t += "respondeu sem nenhum link para este titulo (" + std::to_string(src.seconds) + " s)";
            } else {
                t += std::to_string(src.count) + (src.count == 1 ? " link (" : " links (") + std::to_string(src.seconds) + " s)";
                col = theme::Good;
            }
            app.fonts.regular->draw(c, x, y, 22, app.fonts.regular->ellipsize(22, t, w), col);
            y += 32;
        }
        int hidden = hiddenCount();
        if (hidden > 0 || showHidden_) {
            std::string t = showHidden_ ? "Mostrando tambem os links que o PS4 nao abre (" + hiddenSummary() + "). Quadrado esconde."
                                        : std::to_string(hidden) + (hidden == 1 ? " link escondido" : " links escondidos") +
                                              " porque o PS4 nao abre (" + hiddenSummary() + "). Quadrado mostra.";
            app.fonts.regular->draw(c, x, y, 22, app.fonts.regular->ellipsize(22, t, w), theme::TextMute);
            y += 32;
        }
        y += 12;
        if (view_.empty()) {
            if (!fetch_)
                app.fonts.regular->draw(c, x, y + 10, 26,
                                        entries_.empty() ? "Nenhuma opcao de reproducao para este titulo."
                                                         : "Nenhum link que o PS4 consiga abrir para este titulo. Ficou anotado em Configuracoes.",
                                        theme::TextDim);
            return;
        }
        const int rowH = 88;
        int visible = std::max(1, (area.bottom() - y) / rowH);
        if (sel_ >= int(view_.size())) sel_ = int(view_.size()) - 1;
        if (sel_ < first_) first_ = sel_;
        if (sel_ >= first_ + visible) first_ = sel_ - visible + 1;
        c.pushClip({area.x, y, area.w, area.bottom() - y});
        for (int i = first_; i < int(view_.size()) && i < first_ + visible + 1; i++) {
            const Entry &en = entries_[size_t(view_[size_t(i)])];
            Rect r{x, y + (i - first_) * rowH, w, rowH - 8};
            bool f = i == sel_ && !app.sidebarFocused();
            if (f) {
                c.shadow(r.x, r.y, r.w, r.h, 16, 16, withAlpha(theme::Violet, 100));
                c.fillRoundRect(r.x, r.y, r.w, r.h, 16, theme::SurfaceHi);
                c.strokeRoundRect(r.x, r.y, r.w, r.h, 16, 2, rgba(196, 181, 253));
            } else {
                c.fillRoundRect(r.x, r.y, r.w, r.h, 16, theme::Surface);
            }
            const int rightW = 360;
            app.fonts.bold->draw(c, r.x + 24, r.y + 10, 24, app.fonts.bold->ellipsize(24, oneLine(en.s.name), r.w - rightW - 48), theme::Text);
            app.fonts.regular->draw(c, r.x + 24, r.y + 44, 19,
                                    app.fonts.regular->ellipsize(19, oneLine(en.s.description.empty() ? "-" : en.s.description), r.w - rightW - 48),
                                    theme::TextMute);
            Color bc = en.rank == 0 ? theme::Good : en.rank >= 3 ? theme::Warn : theme::TextDim;
            int bw = app.fonts.bold->measure(19, en.badge);
            app.fonts.bold->draw(c, r.right() - 24 - bw, r.y + 12, 19, en.badge, bc);
            std::string from = app.fonts.regular->ellipsize(18, en.source, rightW - 30);
            app.fonts.regular->draw(c, r.right() - 24 - app.fonts.regular->measure(18, from), r.y + 46, 18, from, theme::TextMute);
        }
        c.popClip();
    }

    Hints hints() const override
    {
        Hints h{{BtnCross, "Assistir"}};
        if (hiddenCount() > 0 || showHidden_) h.push_back({BtnSquare, showHidden_ ? "Esconder os que nao abrem" : "Mostrar escondidos"});
        h.push_back({BtnCircle, "Voltar"});
        return h;
    }

    std::string describe() const override
    {
        int pending = 0, failed = 0;
        for (const auto &src : srcs_) {
            pending += src.state == MultiFetch::State::Pending;
            failed += src.state == MultiFetch::State::Failed;
        }
        std::string d = "links=" + std::to_string(view_.size()) + " escondidos=" + std::to_string(hiddenCount()) + " pendentes=" +
                        std::to_string(pending) + " falhas=" + std::to_string(failed);
        if (sel_ < int(view_.size())) {
            const Entry &en = entries_[size_t(view_[size_t(sel_)])];
            d += " sel=" + oneLine(en.s.name) + " fmt=" + en.fmt;
        }
        return d;
    }

private:
    static constexpr int kTimeoutMs = 40000;   // sources that look links up on demand often need more than 15 s
    struct Src {
        std::string label, error;
        MultiFetch::State state = MultiFetch::State::Pending;
        int seconds = 0, count = 0;
    };
    // rank: 0 MP4, 1 direct link of unknown format (or HLS/TS), 2 opens in the web browser,
    //       3 a format the PS4 player does not open (MKV/AVI), 4 not supported at all (P2P...)
    struct Entry {
        addon::Stream s;
        std::string source, fmt, badge, why;   // why: short word for the hidden summary ("P2P", "MKV")
        int id = 0, rank = 1;
        bool hidden() const { return rank >= 3; }
    };
    std::string type_, videoId_, title_, poster_;
    std::shared_ptr<MultiFetch> fetch_;
    std::vector<Src> srcs_;
    std::vector<Entry> entries_;
    std::vector<int> view_;   // indexes into entries_ that are listed
    bool started_ = false, userMoved_ = false, showHidden_ = false, noted_ = false;
    int sel_ = 0, first_ = 0, nextId_ = 1;
    uint64_t startedMs_ = 0;

    static std::string oneLine(const std::string &t)
    {
        std::string o;
        for (char ch : t) {
            if (ch == '\r') continue;
            if (ch == '\n') { if (!o.empty() && o.back() != ' ') o += "  -  "; continue; }
            o += ch;
        }
        return o;
    }

    static void classify(const addon::Stream &p, Entry &en)
    {
        switch (p.kind) {
        case addon::StreamKind::Url:
            en.fmt = guessContainer(p.filename, p.url, p.description + "\n" + p.name);
            if (en.fmt == "mp4") { en.rank = 0; en.badge = "MP4"; }
            else if (en.fmt == "mkv") { en.rank = 3; en.badge = "MKV: o PS4 nao abre"; en.why = "MKV"; }
            else if (en.fmt == "avi") { en.rank = 3; en.badge = "AVI: o PS4 nao abre"; en.why = "AVI"; }
            else if (en.fmt == "hls") { en.rank = 1; en.badge = "HLS (pode nao abrir)"; }
            else if (en.fmt == "ts") { en.rank = 1; en.badge = "TS (pode nao abrir)"; }
            else { en.rank = 1; en.badge = "formato: so abrindo"; }
            break;
        case addon::StreamKind::External:
            en.rank = 2;
            en.badge = "site externo: abre no celular";
            break;
        case addon::StreamKind::YouTube:
            en.rank = 2;
            en.badge = "YouTube: abre no celular";
            break;
        case addon::StreamKind::Torrent:
            en.rank = 4;
            en.badge = "P2P: nao suportado";
            en.why = "P2P";
            break;
        default:
            en.rank = 4;
            en.badge = "nao suportado";
            en.why = "tipo desconhecido";
            break;
        }
    }

    int hiddenCount() const
    {
        int n = 0;
        for (const auto &e : entries_) n += e.hidden();
        return n;
    }
    int playable() const { return int(entries_.size()) - hiddenCount(); }

    // "3 P2P, 2 MKV"
    std::string hiddenSummary() const
    {
        std::vector<std::pair<std::string, int>> counts;
        for (const auto &e : entries_) {
            if (!e.hidden()) continue;
            auto it = std::find_if(counts.begin(), counts.end(), [&](const std::pair<std::string, int> &c) { return c.first == e.why; });
            if (it == counts.end()) counts.push_back({e.why, 1});
            else it->second++;
        }
        std::string out;
        for (const auto &c : counts) out += (out.empty() ? "" : ", ") + std::to_string(c.second) + " " + c.first;
        return out;
    }

    void rebuildView()
    {
        int keep = sel_ < int(view_.size()) ? entries_[size_t(view_[size_t(sel_)])].id : -1;
        view_.clear();
        for (size_t i = 0; i < entries_.size(); i++)
            if (showHidden_ || !entries_[i].hidden()) view_.push_back(int(i));
        if (userMoved_) {
            for (size_t i = 0; i < view_.size(); i++)
                if (entries_[size_t(view_[i])].id == keep) sel_ = int(i);
        } else {
            sel_ = 0;
        }
        if (sel_ >= int(view_.size())) sel_ = view_.empty() ? 0 : int(view_.size()) - 1;
        first_ = std::min(first_, sel_);
    }

    // Every source answered and there is nothing the PS4 can open: record it (name, poster, why).
    void noteNothingPlayable()
    {
        noted_ = true;
        Problem p;
        p.title = title_;
        p.poster = poster_;
        std::string failed;
        for (const auto &src : srcs_) {
            p.details += src.label + ": ";
            if (src.state == MultiFetch::State::Failed) {
                p.details += src.error + " (" + std::to_string(src.seconds) + " s)\n";
                failed += (failed.empty() ? "" : ", ") + src.label;
            } else {
                p.details += std::to_string(src.count) + " links (" + std::to_string(src.seconds) + " s)\n";
            }
        }
        for (const auto &e : entries_) p.details += "  [" + e.badge + "] " + oneLine(e.s.name) + " (" + e.source + ")\n";
        if (!entries_.empty()) p.reason = "nenhum link que o PS4 abra: " + hiddenSummary();
        else if (!failed.empty()) p.reason = "nenhuma fonte respondeu com links (falharam: " + failed + ")";
        else p.reason = "as fontes nao tem link para este titulo";
        p.source = std::to_string(srcs_.size()) + (srcs_.size() == 1 ? " fonte consultada" : " fontes consultadas");
        app.reportProblem(p);
    }

    void play(const Entry &en)
    {
        const addon::Stream &st = en.s;
        if (st.kind == addon::StreamKind::External || st.kind == addon::StreamKind::YouTube) {
            // Opening the PS4 web browser from an app has no verified interface (OpenOrbis declares
            // sceSystemServiceLaunchWebBrowser without parameters), so the link goes to the phone as a QR code.
            std::string url = st.kind == addon::StreamKind::YouTube ? "https://www.youtube.com/watch?v=" + st.ytId : st.url;
            app.push(makeLinkQr(app, title_, oneLine(st.name) + " (" + en.source + ")", url));
            return;
        }
        if (!st.playableHere()) {
            app.modal("Nao suportado",
                      st.kind == addon::StreamKind::Torrent
                          ? "Este link e P2P (torrent). O Moonlight HB nao tem motor P2P. Escolha um link direto (os marcados MP4 sao os que o PS4 abre)."
                          : "Esse tipo de link nao abre dentro do Moonlight HB. Escolha um link direto (os marcados MP4 sao os que o PS4 abre).");
            return;
        }
        PlayContext ctx;
        ctx.title = title_ + "  -  " + oneLine(st.name);
        ctx.poster = poster_;
        ctx.source = oneLine(st.name) + " (" + en.source + ")";
        // headers the source declares for this link (protocol: behaviorHints.proxyHeaders.request),
        // except the ones the client itself must control
        for (const auto &h : st.requestHeaders) {
            std::string k = lower(h.first);
            if (k == "host" || k == "range" || k == "connection" || k == "content-length" || k == "accept-encoding" || k == "transfer-encoding") continue;
            ctx.requestHeaders.push_back(h);
        }
        app.playVideo(st.url, ctx);
    }
};

// ---------------------------------------------------------------- detail
class DetailScreen : public Screen {
public:
    DetailScreen(App &a, std::string type, std::string id, std::string name)
        : Screen(a), type_(std::move(type)), id_(std::move(id)), fallbackName_(std::move(name)) {}
    std::string title() const override { return meta_.name.empty() ? fallbackName_ : meta_.name; }

    // Details come in the background (01.16 fetched them here, one source after the other, and the
    // screen froze until the last one answered or timed out). The first source that answers wins.
    void onEnter() override
    {
        if (loaded_) return;
        loaded_ = true;
        meta_.name = fallbackName_;
        if (demo::isDemo(id_)) { demo::meta(id_, meta_); applyMeta(); return; }
        std::vector<MultiFetch::Item> items;
        for (const auto &r : app.sources.resourceRequests("meta", type_, id_)) items.push_back({r.source, r.url, r.policy, 25000});
        if (items.empty()) { error_ = "Nenhuma fonte ativa fornece detalhes para este titulo."; return; }
        metaSeen_.assign(items.size(), 0);
        metaFetch_ = MultiFetch::start(app.platform, std::move(items));
    }

    void applyMeta()
    {
        if (meta_.name.empty()) meta_.name = fallbackName_;
        seasons_ = meta_.seasons();
        // resume where the user stopped
        if (const LibraryItem *li = app.library.progressFor(id_))
            for (size_t i = 0; i < seasons_.size(); i++) if (seasons_[i] == li->season) season_ = int(i);
    }

    void pollMeta()
    {
        bool pending = false;
        for (size_t i = 0; i < metaSeen_.size(); i++) {
            if (metaSeen_[i]) continue;
            Json j;
            std::string e;
            MultiFetch::State st = metaFetch_->poll(i, &j, &e, nullptr);
            if (st == MultiFetch::State::Pending) { pending = true; continue; }
            metaSeen_[i] = 1;
            if (st == MultiFetch::State::Done) {
                addon::MetaDetail md;
                addon::ParseResult r = addon::parseMeta(j, md);
                if (r.ok()) {
                    meta_ = std::move(md);
                    error_.clear();
                    applyMeta();
                    metaFetch_.reset();   // the other answers are not needed
                    app.dirty();
                    return;
                }
                e = r.error;
            }
            error_ = metaFetch_->label(i) + ": " + e;
            app.dirty();
        }
        if (!pending) metaFetch_.reset();
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
                } else if (metaFetch_ && type_ == "series") {
                    app.toast("Carregando os episodios...");   // which episode to play is not known yet
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
        if (metaFetch_) pollMeta();
        float target = float(std::max(0, ep_ - 3) * 96);
        float k = app.settings.animations ? std::min(1.0f, dt * 14.0f) : 1.0f;
        anim_ = std::fabs(target - scroll_) > 0.5f;
        scroll_ = anim_ ? scroll_ + (target - scroll_) * k : target;
    }
    bool animating() const override { return anim_ || metaFetch_ != nullptr; }

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
        std::string about = !meta_.description.empty() ? meta_.description : metaFetch_ ? std::string("Carregando detalhes...") : error_;
        auto lines = app.fonts.regular->wrap(25, about, area.right() - tx - 80, 4);
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
    std::shared_ptr<MultiFetch> metaFetch_;
    std::vector<char> metaSeen_;
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
        std::string what = meta_.name;
        for (const auto &e : meta_.videos)
            if (e.id == videoId && (e.season > 0 || e.episode > 0))
                what += "  T" + std::to_string(e.season) + "E" + std::to_string(e.episode) + (e.title.empty() ? "" : " " + e.title);
        app.push(std::unique_ptr<Screen>(new StreamPickerScreen(app, type_, videoId, what, meta_.poster)));
    }
};

}  // namespace

std::unique_ptr<Screen> makeCatalog(App &a, const std::string &type) { return std::unique_ptr<Screen>(new CatalogScreen(a, type)); }
std::unique_ptr<Screen> makeDetail(App &a, const std::string &type, const std::string &id, const std::string &name)
{
    return std::unique_ptr<Screen>(new DetailScreen(a, type, id, name));
}

}  // namespace ml
