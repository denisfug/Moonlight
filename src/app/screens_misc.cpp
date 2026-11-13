// Live TV (M3U), Library (favorites/history), Sources (addon manager) and Settings.
#include <algorithm>
#include <cctype>
#include <atomic>
#include <cmath>
#include <ctime>

#include "app/app.hpp"
#include "app/demo_data.hpp"
#include "app/updater.hpp"
#include "qrcodegen/qrcodegen.h"
#include "net/tls.hpp"
#include "ui/theme.hpp"

namespace ml {

using namespace ui;

namespace {

// Vertical list helper with smooth scrolling.
struct ListScroll {
    int sel = 0;
    float scroll = 0;
    bool anim = false;
    void update(float dt, int rowH, int visible, bool animations)
    {
        float target = float(std::max(0, sel - visible + 2) * rowH);
        float k = animations ? std::min(1.0f, dt * 14.0f) : 1.0f;
        anim = std::fabs(target - scroll) > 0.5f;
        scroll = anim ? scroll + (target - scroll) * k : target;
    }
};

void drawListRow(App &app, Canvas &c, Rect r, bool focused)
{
    if (focused) {
        c.shadow(r.x, r.y, r.w, r.h, 16, 16, withAlpha(theme::Violet, 90));
        c.fillRoundRect(r.x, r.y, r.w, r.h, 16, theme::SurfaceHi);
        c.strokeRoundRect(r.x, r.y, r.w, r.h, 16, 2, rgba(196, 181, 253));
    } else {
        c.fillRoundRect(r.x, r.y, r.w, r.h, 16, theme::Surface);
    }
}

void drawTabs(App &app, Canvas &c, int x, int y, const std::vector<std::string> &tabs, int sel)
{
    for (size_t i = 0; i < tabs.size(); i++) {
        bool on = int(i) == sel;
        int w = app.fonts.bold->measure(22, tabs[i]) + 44;
        if (on) c.fillGradient(x, y, w, 48, 24, theme::Violet, theme::Indigo, 0);
        else c.fillRoundRect(x, y, w, 48, 24, theme::Surface);
        (on ? app.fonts.bold : app.fonts.regular)->draw(c, x + 22, y + (48 - app.fonts.bold->lineHeight(22)) / 2, 22, tabs[i], on ? theme::Text : theme::TextDim);
        x += w + 12;
    }
}

// ---------------------------------------------------------------- live tv
class LiveScreen : public Screen {
public:
    using Screen::Screen;
    std::string title() const override { return "Canais"; }
    void onEnter() override { rebuild(); }

    void rebuild()
    {
        groups_ = {"Todos"};
        for (const auto &ch : app.channels)
            if (std::find(groups_.begin(), groups_.end(), ch.group) == groups_.end()) groups_.push_back(ch.group);
        if (group_ >= int(groups_.size())) group_ = 0;
        shown_.clear();
        for (size_t i = 0; i < app.channels.size(); i++)
            if (group_ == 0 || app.channels[i].group == groups_[size_t(group_)]) shown_.push_back(i);
        if (list_.sel >= int(shown_.size())) list_.sel = 0;
    }

    bool handle(uint32_t p) override
    {
        if (p & (BtnL1 | BtnR1)) {
            int n = int(groups_.size());
            group_ = (group_ + ((p & BtnR1) ? 1 : n - 1)) % n;
            list_.sel = 0;
            rebuild();
            return true;
        }
        if (p & BtnUp) { if (list_.sel > 0) list_.sel--; return true; }
        if (p & BtnDown) { if (list_.sel + 1 < int(shown_.size())) list_.sel++; return true; }
        if ((p & BtnCross) && !shown_.empty()) {
            const Channel &ch = app.channels[shown_[size_t(list_.sel)]];
            if (ch.url.empty()) app.modal(ch.name, "Canal de demonstracao, sem transmissao. Coloque uma lista M3U autorizada em " + app.platform.dataDir() + "/channels.m3u.");
            else if (ch.drmProtected) app.modal(ch.name, "Este canal usa DRM. O Moonlight HB nao contorna protecoes de conteudo.");
            else app.playVideo(ch.url, ch.name);
            return true;
        }
        return false;
    }

    void update(float dt) override { list_.update(dt, 92, 8, app.settings.animations); }
    bool animating() const override { return list_.anim; }

    void draw(Canvas &c, Rect area) override
    {
        drawSectionHeader(app, c, area, "Canais ao vivo", "Fonte: " + app.channelsSource);
        int x = area.x + theme::Margin;
        drawTabs(app, c, x, 176, groups_, group_);
        if (shown_.empty()) { app.fonts.regular->draw(c, x, 270, 26, "Nenhum canal.", theme::TextDim); return; }
        int top = 256, w = area.w - 2 * theme::Margin;
        c.pushClip({area.x, top - 20, area.w, area.bottom() - top + 20});
        int y = top - int(list_.scroll);
        for (size_t i = 0; i < shown_.size(); i++, y += 92) {
            if (y + 92 < top - 20 || y > area.bottom()) continue;
            const Channel &ch = app.channels[shown_[i]];
            Rect r{x, y, w, 80};
            drawListRow(app, c, r, int(i) == list_.sel && !app.sidebarFocused());
            drawGeneratedArt(c, app.fonts, {r.x + 10, r.y + 10, 106, 60}, 10, "ch:" + ch.name, "", false);
            drawIcon(c, Icon::Live, float(r.x + 63), float(r.y + 40), 26, rgba(255, 255, 255, 220));
            app.fonts.bold->draw(c, r.x + 140, r.y + 12, 25, ch.name, theme::Text);
            app.fonts.regular->draw(c, r.x + 140, r.y + 46, 19, ch.group + (ch.url.empty() ? "  -  demonstracao" : "") + (ch.drmProtected ? "  -  DRM (nao suportado)" : ""), theme::TextMute);
        }
        c.popClip();
    }

    Hints hints() const override { return {{BtnCross, "Assistir"}, {BtnL1 | BtnR1, "Categoria"}}; }

private:
    std::vector<std::string> groups_{"Todos"};
    std::vector<size_t> shown_;
    int group_ = 0;
    ListScroll list_;
};

// ---------------------------------------------------------------- library
class LibraryScreen : public Screen {
public:
    using Screen::Screen;
    std::string title() const override { return "Biblioteca"; }
    void onEnter() override { refresh(); }

    void refresh()
    {
        items_ = tab_ == 0 ? app.library.favorites() : tab_ == 1 ? app.library.continueWatching(100) : app.library.history();
        if (list_.sel >= int(items_.size())) list_.sel = 0;
    }

    bool handle(uint32_t p) override
    {
        if (p & (BtnL1 | BtnR1)) { tab_ = (tab_ + ((p & BtnR1) ? 1 : 2)) % 3; list_.sel = 0; refresh(); return true; }
        if (p & BtnUp) { if (list_.sel > 0) list_.sel--; return true; }
        if (p & BtnDown) { if (list_.sel + 1 < int(items_.size())) list_.sel++; return true; }
        if ((p & BtnCross) && !items_.empty()) {
            const LibraryItem &i = items_[size_t(list_.sel)];
            app.push(makeDetail(app, i.type, i.id, i.name));
            return true;
        }
        return false;
    }

    void update(float dt) override { list_.update(dt, 100, 7, app.settings.animations); }
    bool animating() const override { return list_.anim; }

    void draw(Canvas &c, Rect area) override
    {
        drawSectionHeader(app, c, area, "Biblioteca", std::to_string(app.library.favorites().size()) + " favoritos  -  " +
                                                         std::to_string(app.library.history().size()) + " no historico");
        int x = area.x + theme::Margin;
        drawTabs(app, c, x, 176, {"Favoritos", "Continuar assistindo", "Historico"}, tab_);
        if (items_.empty()) {
            const char *msg[] = {"Nenhum favorito. Abra um filme ou serie e escolha Favoritar.",
                                 "Nada em andamento. O progresso aparece aqui quando a reproducao de video existir.",
                                 "Historico vazio."};
            app.fonts.regular->draw(c, x, 280, 26, msg[tab_], theme::TextDim);
            return;
        }
        int top = 256, w = area.w - 2 * theme::Margin;
        c.pushClip({area.x, top - 20, area.w, area.bottom() - top + 20});
        int y = top - int(list_.scroll);
        for (size_t k = 0; k < items_.size(); k++, y += 100) {
            if (y + 100 < top - 20 || y > area.bottom()) continue;
            const LibraryItem &i = items_[k];
            Rect r{x, y, w, 88};
            drawListRow(app, c, r, int(k) == list_.sel && !app.sidebarFocused());
            drawGeneratedArt(c, app.fonts, {r.x + 10, r.y + 8, 48, 72}, 8, i.id, "", false);
            app.fonts.bold->draw(c, r.x + 80, r.y + 14, 25, i.name, theme::Text);
            std::string sub = i.type == "movie" ? "Filme" : i.type == "series" ? "Serie" : i.type;
            if (i.season) sub += "  -  T" + std::to_string(i.season) + " E" + std::to_string(i.episode);
            app.fonts.regular->draw(c, r.x + 80, r.y + 50, 19, sub, theme::TextMute);
            if (i.durationMs > 0) drawProgress(c, r.right() - 300, r.y + 40, 260, 8, i.progress(), theme::Blue, theme::SurfaceHi);
        }
        c.popClip();
    }

    Hints hints() const override { return {{BtnCross, "Abrir"}, {BtnL1 | BtnR1, "Aba"}}; }

private:
    int tab_ = 0;
    std::vector<LibraryItem> items_;
    ListScroll list_;
};

// ---------------------------------------------------------------- sources
class SourcesScreen : public Screen {
public:
    using Screen::Screen;
    std::string title() const override { return "Fontes"; }

    bool handle(uint32_t p) override
    {
        const auto &src = app.sources.sources();
        int n = int(src.size());
        if (p & BtnOptions) { addSource(); return true; }
        if (p & BtnR1) { importList(); return true; }
        if (p & BtnUp) { if (list_.sel > 0) list_.sel--; return true; }
        if (p & BtnDown) { if (list_.sel + 1 < n) list_.sel++; return true; }
        if (n == 0) { if (p & BtnCross) { addSource(); return true; } return false; }
        size_t i = size_t(list_.sel);
        if (p & BtnCross) {
            app.sources.setEnabled(i, !src[i].enabled);
            app.sourcesVersion++;
            app.toast(src[i].manifest.name + (src[i].enabled ? " ativada" : " desativada"));
            return true;
        }
        if (p & BtnTriangle) {
            std::string e = app.sources.refresh(i, int64_t(app.now()));
            app.toast(e.empty() ? "Fonte atualizada" : "Falha ao atualizar: " + e);
            return true;
        }
        if (p & BtnSquare) {
            std::string name = src[i].manifest.name;
            App *a = &app;
            ListScroll *ls = &list_;
            app.modal("Remover fonte", "Remover \"" + name + "\" da lista? Voce pode adiciona-la de novo depois.", {"Cancelar", "Remover"},
                      [a, i, ls, name](int choice) {
                          if (choice != 1) return;
                          a->sources.remove(i);
                          a->sourcesVersion++;
                          if (ls->sel > 0) ls->sel--;
                          a->toast(name + " removida");
                      });
            return true;
        }
        return false;
    }

    void update(float dt) override { list_.update(dt, 128, 5, app.settings.animations); }
    bool animating() const override { return list_.anim; }

    void draw(Canvas &c, Rect area) override
    {
        const auto &src = app.sources.sources();
        HttpClient *h = app.sources.http();
        std::string ns = app.platform.networkStatus();
        std::string net = !h ? "sem cliente de rede nesta plataforma"
                        : !ns.empty() ? "internet com problema: " + ns + " (Configuracoes > Testar internet)"
                        : std::string("rede: ") + h->name() + (h->supportsHttps() ? "" : " (somente http local)");
        drawSectionHeader(app, c, area, "Fontes", std::to_string(src.size()) + " fontes  -  " + net);
        int x = area.x + theme::Margin, top = 186;
        int listW = 880;
        if (src.empty()) {
            Rect r{x, top, area.w - 2 * theme::Margin, 300};
            c.fillRoundRect(r.x, r.y, r.w, r.h, 24, theme::Surface);
            drawIcon(c, Icon::Sources, float(r.x + 80), float(r.y + 90), 64, theme::Glow);
            app.fonts.bold->draw(c, r.x + 150, r.y + 50, 32, "Nenhuma fonte adicionada", theme::Text);
            auto lines = app.fonts.regular->wrap(23, "Fontes seguem o protocolo de add-ons do Stremio (endereco terminando em /manifest.json). "
                                                     "Use apenas servicos e servidores que voce esta autorizado a acessar. "
                                                     "Aperte X ou OPTIONS para adicionar.", r.w - 200, 5);
            int y = r.y + 104;
            for (const auto &l : lines) { app.fonts.regular->draw(c, r.x + 150, y, 23, l, theme::TextDim); y += 34; }
            return;
        }
        c.pushClip({area.x, top - 20, listW + theme::Margin + 20, area.bottom() - top + 20});
        int y = top - int(list_.scroll);
        for (size_t i = 0; i < src.size(); i++, y += 128) {
            const Source &s = src[i];
            Rect r{x, y, listW, 112};
            drawListRow(app, c, r, int(i) == list_.sel && !app.sidebarFocused());
            drawGeneratedArt(c, app.fonts, {r.x + 14, r.y + 14, 84, 84}, 16, "src:" + s.manifest.id, "", false);
            app.fonts.bold->draw(c, r.x + 120, r.y + 16, 26, app.fonts.bold->ellipsize(26, s.manifest.name, r.w - 300), theme::Text);
            app.fonts.regular->draw(c, r.x + 120, r.y + 52, 19, "v" + s.manifest.version + "  -  " + std::to_string(s.manifest.catalogs.size()) + " catalogos", theme::TextMute);
            app.fonts.regular->draw(c, r.x + 120, r.y + 78, 17, app.fonts.regular->ellipsize(17, redactUrl(s.manifestUrl), r.w - 160), theme::TextMute);
            const char *st = s.enabled ? "ATIVA" : "DESATIVADA";
            int w = app.fonts.bold->measure(16, st) + 26;
            c.fillRoundRect(r.right() - w - 20, r.y + 18, w, 28, 14, s.enabled ? rgba(74, 222, 128, 40) : rgba(255, 255, 255, 20));
            app.fonts.bold->draw(c, r.right() - w - 7, r.y + 18 + (28 - app.fonts.bold->lineHeight(16)) / 2, 16, st, s.enabled ? theme::Good : theme::TextMute);
            if (!s.lastError.empty()) drawIcon(c, Icon::Warning, float(r.right() - 36), float(r.y + 80), 22, theme::Warn);
        }
        c.popClip();
        // details of the focused source
        const Source &s = src[size_t(std::min(list_.sel, int(src.size()) - 1))];
        Rect d{x + listW + 28, top, area.right() - theme::Margin - (x + listW + 28), area.bottom() - top - 20};
        c.fillRoundRect(d.x, d.y, d.w, d.h, 24, theme::Surface);
        int dy = d.y + 28;
        auto lineOut = [&](const std::string &k, const std::string &v) {
            app.fonts.bold->draw(c, d.x + 28, dy, 19, k, theme::TextMute);
            auto ls = app.fonts.regular->wrap(21, v.empty() ? "-" : v, d.w - 56, 3);
            dy += 28;
            for (const auto &l : ls) { app.fonts.regular->draw(c, d.x + 28, dy, 21, l, theme::Text); dy += 30; }
            dy += 10;
        };
        lineOut("DESCRICAO", s.manifest.description);
        std::string types, res, cats;
        for (const auto &t : s.manifest.types) types += (types.empty() ? "" : ", ") + t;
        for (const auto &r : s.manifest.resources) res += (res.empty() ? "" : ", ") + r.name;
        for (const auto &ct : s.manifest.catalogs) cats += (cats.empty() ? "" : ", ") + ct.name + " (" + ct.type + ")";
        lineOut("TIPOS", types);
        lineOut("RECURSOS", res);
        lineOut("CATALOGOS", cats);
        if (s.manifest.p2p) lineOut("AVISO", "Esta fonte declara conteudo P2P. Streams P2P nao sao reproduzidos pelo Moonlight HB.");
        if (!s.lastError.empty()) lineOut("ULTIMO ERRO", s.lastError);
    }

    Hints hints() const override
    {
        if (app.sources.sources().empty()) return {{BtnCross, "Digitar endereco"}, {BtnR1, "Importar lista do pendrive"}};
        return {{BtnCross, "Ativar/Desativar"}, {BtnTriangle, "Atualizar"}, {BtnSquare, "Remover"}, {BtnOptions, "Adicionar"},
                {BtnR1, "Importar lista"}};
    }

private:
    ListScroll list_;

    // Reads addresses (one per line) from moonlight-fontes.txt on a pendrive or in the data folder,
    // so nobody has to type long URLs with the controller.
    void importList()
    {
        // Any .txt in the pendrive root whose name mentions "fonte"/"font"/"moonlight" (Windows often adds
        // a hidden second ".txt"), or fontes.txt in the data folder.
        std::string text, used;
        std::vector<std::string> roots;
        for (int i = 0; i < 8; i++) roots.push_back("/mnt/usb" + std::to_string(i));
        for (const auto &r : roots) {
            std::vector<DirEntry> ents;
            if (!app.platform.fs().listDir(r, ents)) continue;
            for (const auto &e : ents) {
                std::string low = e.name;
                for (auto &ch : low) ch = char(std::tolower(static_cast<unsigned char>(ch)));
                bool txt = low.size() > 4 && low.compare(low.size() - 4, 4, ".txt") == 0;
                bool named = low.find("fonte") != std::string::npos || low.find("font") != std::string::npos ||
                             low.find("moonlight") != std::string::npos;
                if (!e.isDir && txt && named && app.platform.fs().readFile(r + "/" + e.name, text, 256 * 1024)) {
                    used = r + "/" + e.name;
                    break;
                }
            }
            if (!used.empty()) break;
        }
        if (used.empty() && app.platform.fs().readFile(app.platform.dataDir() + "/fontes.txt", text, 256 * 1024)) used = "fontes.txt";
        if (used.empty()) {
            app.modal("Importar fontes", "Nao achei o arquivo. Coloque um .txt com \"fontes\" no nome (ex: moonlight-fontes.txt) "
                                         "solto na raiz do pendrive, um endereco por linha terminando em /manifest.json, "
                                         "conecte na porta da frente do PS4 e aperte R1 de novo.");
            return;
        }
        int added = 0, failed = 0;
        std::string lastErr;
        size_t pos = 0;
        while (pos < text.size()) {
            size_t nl = text.find('\n', pos);
            std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
            pos = nl == std::string::npos ? text.size() : nl + 1;
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            while (!line.empty() && line.front() == ' ') line.erase(0, 1);
            if (line.empty() || line[0] == '#') continue;
            std::string e = app.addSourceByUser(line);
            if (e.empty()) added++;
            else if (e.find("ja esta na lista") == std::string::npos) { failed++; lastErr = e; }
        }
        std::string msg = "Arquivo: " + used + "\n" + std::to_string(added) + " fonte(s) adicionada(s)";
        if (failed) msg += ", " + std::to_string(failed) + " com erro (ultimo: " + lastErr + ")";
        app.modal("Importar fontes", msg + ".");
    }

    void addSource()
    {
        App *a = &app;
        app.openKeyboard("Endereco da fonte (.../manifest.json)", "https://", true, [a](const std::string &url) {
            std::string e = a->addSourceByUser(url);
            if (e.empty()) { a->toast("Fonte adicionada: " + a->sources.sources().back().manifest.name); }
            else a->modal("Nao foi possivel adicionar", e);
        });
    }
};

// ---------------------------------------------------------------- settings
// ---------------------------------------------------------------- phone remote: QR code
// Shows the address + PIN as a QR code; the link carries the PIN in the fragment (#pin=1234), so
// the phone page signs in by itself. The fragment never leaves the phone's browser.
class RemoteQrScreen : public Screen {
public:
    explicit RemoteQrScreen(App &a) : Screen(a)
    {
        build();
        app.remote->selfTest();
    }
    bool animating() const override { return true; }   // live counters
    std::string title() const override { return "Controle pelo celular"; }

    void build()
    {
        link_ = app.remote->address() + "/#pin=" + app.remote->pin();
        uint8_t tmp[qrcodegen_BUFFER_LEN_MAX], qr[qrcodegen_BUFFER_LEN_MAX];
        size_ = 0;
        if (qrcodegen_encodeText(link_.c_str(), tmp, qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
                                 qrcodegen_Mask_AUTO, true)) {
            size_ = qrcodegen_getSize(qr);
            modules_.assign(size_t(size_ * size_), 0);
            for (int y = 0; y < size_; y++)
                for (int x = 0; x < size_; x++) modules_[size_t(y * size_ + x)] = qrcodegen_getModule(qr, x, y) ? 1 : 0;
        }
    }

    bool handle(uint32_t p) override
    {
        if (p & BtnSquare) {
            app.remote->stop();
            app.settings.remote = false;
            app.saveSettings();
            app.toast("Controle pelo celular desligado");
            app.pop();
            return true;
        }
        return false;   // O goes back
    }

    void draw(Canvas &c, Rect area) override
    {
        drawSectionHeader(app, c, area, "Controle pelo celular", "Use o celular como controle e teclado");
        const int quiet = 4;
        int px = size_ > 0 ? std::min(16, 560 / (size_ + 2 * quiet)) : 0;
        int side = (size_ + 2 * quiet) * px;
        int qx = area.x + theme::Margin, qy = 200;
        if (size_ > 0) {
            c.fillRoundRect(qx - 8, qy - 8, side + 16, side + 16, 20, rgba(255, 255, 255));
            for (int y = 0; y < size_; y++)
                for (int x = 0; x < size_; x++)
                    if (modules_[size_t(y * size_ + x)]) c.fillRect(qx + (x + quiet) * px, qy + (y + quiet) * px, px, px, rgba(0, 0, 0));
        }
        int tx = qx + side + 80, ty = qy + 10;
        auto line = [&](int size, const std::string &t, Color col, int gap) {
            app.fonts.regular->draw(c, tx, ty, size, t, col);
            ty += gap;
        };
        app.fonts.bold->draw(c, tx, ty, 40, "Aponte a camera do celular", theme::Text);
        ty += 70;
        line(28, "1. Conecte o celular no mesmo Wi-Fi do PS4.", theme::TextDim, 50);
        line(28, "2. Abra a camera e aponte para o codigo.", theme::TextDim, 50);
        line(28, "3. Toque no link que aparecer: entra direto, sem digitar.", theme::TextDim, 90);
        line(24, "Sem camera? Digite no navegador:", theme::TextMute, 44);
        app.fonts.bold->draw(c, tx, ty, 34, app.remote->address(), theme::Text);
        ty += 60;
        app.fonts.bold->draw(c, tx, ty, 34, "PIN  " + app.remote->pin(), theme::Glow);
        ty += 90;
        line(22, "O PIN muda cada vez que o app abre. So funciona na sua rede de casa.", theme::TextMute, 56);
        // Diagnostics: if the self-test fails the console side is the problem; if it passes but the
        // phone never connects (counter stays 0), it is the network between them (other Wi-Fi, mobile
        // data, router isolating devices).
        std::string st = app.remote->selfTestResult();
        line(24, "Teste dentro do PS4: " + (st.empty() ? std::string("testando...") : st), st == "ok" ? theme::Good : st.empty() ? theme::TextDim : theme::Warn, 40);
        line(24, "Conexoes recebidas: " + std::to_string(app.remote->accepted()), theme::TextDim, 40);
        if (app.remote->address().find('<') != std::string::npos)
            line(24, "O PS4 nao informou o IP: veja em Configuracoes do PS4 > Rede > Ver status da conexao.", theme::Warn, 40);
    }

    Hints hints() const override { return {{BtnCircle, "Voltar"}, {BtnSquare, "Desligar"}}; }

private:
    std::string link_;
    int size_ = 0;
    std::vector<uint8_t> modules_;
};

// ---------------------------------------------------------------- external link -> phone
class LinkQrScreen : public Screen {
public:
    LinkQrScreen(App &a, std::string title, std::string from, std::string url)
        : Screen(a), title_(std::move(title)), from_(std::move(from)), url_(std::move(url))
    {
        uint8_t tmp[qrcodegen_BUFFER_LEN_MAX], qr[qrcodegen_BUFFER_LEN_MAX];
        if (url_.size() < 1200 && qrcodegen_encodeText(url_.c_str(), tmp, qr, qrcodegen_Ecc_LOW, qrcodegen_VERSION_MIN,
                                                       qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true)) {
            size_ = qrcodegen_getSize(qr);
            modules_.assign(size_t(size_ * size_), 0);
            for (int y = 0; y < size_; y++)
                for (int x = 0; x < size_; x++) modules_[size_t(y * size_ + x)] = qrcodegen_getModule(qr, x, y) ? 1 : 0;
        }
    }
    std::string title() const override { return "Abrir no celular"; }
    bool handle(uint32_t) override { return false; }   // O goes back

    void draw(Canvas &c, Rect area) override
    {
        drawSectionHeader(app, c, area, "Abrir no celular", title_);
        const int quiet = 4;
        int px = size_ > 0 ? std::max(3, std::min(16, 620 / (size_ + 2 * quiet))) : 0;
        int side = (size_ + 2 * quiet) * px;
        int qx = area.x + theme::Margin, qy = 200;
        if (size_ > 0) {
            c.fillRoundRect(qx - 8, qy - 8, side + 16, side + 16, 20, rgba(255, 255, 255));
            for (int y = 0; y < size_; y++)
                for (int x = 0; x < size_; x++)
                    if (modules_[size_t(y * size_ + x)]) c.fillRect(qx + (x + quiet) * px, qy + (y + quiet) * px, px, px, rgba(0, 0, 0));
        }
        int tx = qx + side + 80, ty = qy + 10, tw = area.right() - theme::Margin - tx;
        app.fonts.bold->draw(c, tx, ty, 36, "Este link abre fora do Moonlight", theme::Text);
        ty += 66;
        for (const auto &l : app.fonts.regular->wrap(26, "O PS4 nao toca este tipo de link dentro do app. Aponte a camera do celular para o codigo e "
                                                         "assista no celular (ou espelhe na TV).", tw, 4)) {
            app.fonts.regular->draw(c, tx, ty, 26, l, theme::TextDim);
            ty += 38;
        }
        ty += 24;
        app.fonts.regular->draw(c, tx, ty, 22, app.fonts.regular->ellipsize(22, from_, tw), theme::TextMute);
        ty += 34;
        for (const auto &l : app.fonts.regular->wrap(20, url_, tw, 4)) {
            app.fonts.regular->draw(c, tx, ty, 20, l, theme::TextMute);
            ty += 28;
        }
        if (size_ == 0) app.fonts.regular->draw(c, qx, qy, 24, "Link grande demais para QR code.", theme::Warn);
    }

    Hints hints() const override { return {{BtnCircle, "Voltar"}}; }
    std::string describe() const override { return "link=" + url_; }

private:
    std::string title_, from_, url_;
    int size_ = 0;
    std::vector<uint8_t> modules_;
};

class SettingsScreen : public Screen {
public:
    using Screen::Screen;
    std::string title() const override { return "Configuracoes"; }

    bool handle(uint32_t p) override
    {
        const int n = kItems;
        if (p & BtnUp) { if (sel_ > 0) sel_--; return true; }
        if (p & BtnDown) { if (sel_ + 1 < n) sel_++; return true; }
        if (p & BtnCross) {
            switch (sel_) {
            case 0: app.settings.animations = !app.settings.animations; app.saveSettings(); break;
            case 1:
                app.settings.showDemo = !app.settings.showDemo;
                app.saveSettings();
                app.rescanMusic();
                app.toast("Reabra as secoes para atualizar o conteudo de demonstracao");
                break;
            case 2: {
                app.rescanMusic();
                size_t real = 0;
                for (const auto &t : app.music.tracks()) if (!t.demo) real++;
                app.toast(std::to_string(real) + " faixas encontradas");
                break;
            }
            case 3: configureServer(); break;
            case 4: app.settings.showRadio = !app.settings.showRadio; app.saveSettings(); break;
            case 5: toggleRemote(); break;
            case 6: updateAction(); break;
            case 7: testInternet(); break;
            case 8: app.push(makeVideoTest(app)); break;
            case 9: app.push(makeProblems(app)); break;
            case 10:
                app.modal("Sobre o Moonlight HB",
                          std::string("Versao ") + ML_VERSION + " (" + app.platform.name() + "). Centro multimidia homebrew para PS4. "
                          "Conteudo apenas de fontes autorizadas; sem contorno de DRM. Fontes de texto: Outfit (OFL). "
                          "Decodificador MP3: minimp3 (CC0). Texto: stb_truetype (dominio publico).");
                break;
            }
            return true;
        }
        return false;
    }

    void draw(Canvas &c, Rect area) override
    {
        serverDesc_ = app.server.configured() ? app.server.user + " em " + app.server.url + "  (X para trocar)"
                                              : "Suas musicas do PC pelo Wi-Fi. X para configurar.";
        remoteDesc_ = app.remote->running() ? "Ligado. X mostra o QR code (" + app.remote->address() + "  PIN " + app.remote->pin() + ")"
                                             : "Use o celular (mesmo Wi-Fi) como controle e teclado. Desligado.";
        updateDesc_ = describeUpdate();
        size_t np = app.problems.entries().size();
        problemsDesc_ = np == 0 ? "Nada anotado. Quando algo nao tocar, o nome, a capa e o motivo aparecem aqui."
                                : std::to_string(np) + (np == 1 ? " anotado" : " anotados") + ". Nome, capa e motivo de cada um, para foto.";
        drawSectionHeader(app, c, area, "Configuracoes", "Dados em " + app.platform.dataDir());
        int x = area.x + theme::Margin, y = 186, w = 1000;
        struct Item { const char *title, *desc; int toggle; };
        Item items[] = {
            {"Animacoes", "Rolagem suave e transicoes entre telas.", app.settings.animations ? 1 : 0},
            {"Conteudo de demonstracao", "Mostra filmes, series, canais e playlists ficticios enquanto nao ha fontes reais.", app.settings.showDemo ? 1 : 0},
            {"Procurar musicas de novo", "Rele /data/music, /data/moonlight/music, /data/devhub/Music e o pendrive (Music/).", -1},
            {"Servidor de musica (Navidrome / Subsonic)", serverDesc_.c_str(), -1},
            {"Secao Radios", "Mostra a secao de radios online na barra lateral.", app.settings.showRadio ? 1 : 0},
            {"Controle pelo celular", remoteDesc_.c_str(), app.remote->running() ? 1 : 0},
            {"Atualizar o Moonlight", updateDesc_.c_str(), -1},
            {"Testar internet", "Tenta iniciar a internet do PS4 e abrir um site seguro (HTTPS). Mostra o erro exato.", -1},
            {"Testar video", "Baixa um video de teste e toca do HD e pela internet, mostrando o codigo de cada passo.", -1},
            {"Problemas ao reproduzir", problemsDesc_.c_str(), -1},
            {"Sobre", "Versao, plataforma e licencas.", -1},
        };
        // the list scrolls: 9 rows fit
        const int visible = 9;
        if (sel_ < first_) first_ = sel_;
        if (sel_ >= first_ + visible) first_ = sel_ - visible + 1;
        if (first_ > 0) app.fonts.regular->draw(c, x + w + 24, 196, 20, "mais acima", theme::TextMute);
        if (first_ + visible < kItems) app.fonts.regular->draw(c, x + w + 24, 186 + (visible - 1) * 90 + 30, 20, "mais abaixo", theme::TextMute);
        for (int i = first_; i < kItems && i < first_ + visible; i++, y += 90) {
            Rect r{x, y, w, 82};
            drawListRow(app, c, r, i == sel_ && !app.sidebarFocused());
            app.fonts.bold->draw(c, r.x + 30, r.y + 12, 27, items[i].title, theme::Text);
            app.fonts.regular->draw(c, r.x + 30, r.y + 50, 20, items[i].desc, theme::TextMute);
            if (items[i].toggle >= 0) {
                bool on = items[i].toggle == 1;
                int tx = r.right() - 110, ty = r.y + 22;
                if (on) c.fillGradient(tx, ty, 76, 38, 19, theme::Violet, theme::Blue, 0);
                else c.fillRoundRect(tx, ty, 76, 38, 19, theme::SurfaceHi);
                c.fillCircle(float(on ? tx + 57 : tx + 19), float(ty + 19), 14, theme::Text);
            }
        }
    }

    Hints hints() const override { return {{BtnCross, "Alterar"}, {BtnCircle, "Voltar"}}; }

private:
    static constexpr int kItems = 11;
    int sel_ = 0, first_ = 0;
    std::string serverDesc_, newUrl_, newUser_, remoteDesc_, updateDesc_, problemsDesc_;
    std::string testHeader_, testResult_;
    std::atomic<bool> testBusy_{false}, testDone_{false};
    bool testOk_ = false;

    std::string describeUpdate()
    {
        Updater::State u = app.updater->state();
        std::string cur = "Versao " + std::string(ML_VERSION) + ". ";
        switch (u.phase) {
        case Updater::Phase::Idle: return cur + "X para procurar versao nova.";
        case Updater::Phase::Checking: return cur + "Procurando...";
        case Updater::Phase::UpToDate: return cur + "Voce ja tem a mais nova. X procura de novo.";
        case Updater::Phase::Available: return "Nova versao " + u.latest + " disponivel! X para baixar.";
        case Updater::Phase::Downloading: {
            int pct = u.size > 0 ? int(u.done * 100 / u.size) : 0;
            return "Baixando " + u.latest + "... " + std::to_string(pct) + "%";
        }
        case Updater::Phase::Downloaded: return u.latest + " baixada. X para instalar (o PS4 mostra a instalacao).";
        case Updater::Phase::Failed: return "Falhou. X mostra o motivo e tenta de novo.";
        }
        return cur;
    }

    void updateAction()
    {
        Updater::State u = app.updater->state();
        switch (u.phase) {
        case Updater::Phase::Available: app.updater->download(); break;
        case Updater::Phase::Failed: {
            // the whole message (the line under the item is cut when long)
            Updater *up = app.updater.get();
            bool dl = !u.assetUrl.empty();
            app.modal("Atualizar o Moonlight", "Falhou: " + u.error, {"Tentar de novo", "Fechar"}, [up, dl](int c) {
                if (c != 0) return;
                if (dl) up->download();
                else up->check();
            });
            break;
        }
        case Updater::Phase::Checking:
        case Updater::Phase::Downloading: app.toast("Aguarde..."); break;
        case Updater::Phase::Downloaded: install(u.path, false); break;
        default: app.updater->check(); break;
        }
    }

    // 01.19: try the real system installer (ps4: libSceAppInstUtil, loaded dynamically). On any failure
    // it returns <0 and we fall back to the manual Package-Installer instructions, so nothing regresses.
    void install(const std::string &path, bool replace)
    {
        std::string e;
        int r = app.platform.installPackage(path, replace, &e);
        if (r == 0) {
            app.modal("Atualizar o Moonlight",
                      "Instalacao iniciada. Veja a notificacao do PS4 na tela inicial e espere terminar; depois "
                      "abra a versao nova.\nSeus dados (fontes, favoritos) em /data/moonlight nao se perdem.");
            return;
        }
        if (r == 1) {
            app.modal("Atualizar o Moonlight",
                      "O Moonlight ja esta instalado. Substituir pela versao " + app.updater->state().latest + "?",
                      {"Substituir", "Cancelar"}, [this, path](int c) { if (c == 0) install(path, true); });
            return;
        }
        app.modal("Atualizar o Moonlight",
                  "Versao nova baixada em " + path + ".\n(Instalacao automatica indisponivel: " + e + ")\n"
                  "Para instalar na mao: feche o Moonlight e instale esse arquivo pelo Package Installer do GoldHEN "
                  "(que le /data/pkg).\nSeus dados (fontes, favoritos) ficam em /data/moonlight e nao se perdem.");
    }

    void toggleRemote()
    {
        if (!app.remote->running()) {
            std::string e;
            if (!app.remote->start(&e)) { app.modal("Controle pelo celular", "Nao foi possivel ligar: " + e); return; }
            app.settings.remote = true;
            app.saveSettings();
        }
        app.push(std::unique_ptr<Screen>(new RemoteQrScreen(app)));   // QR code; square turns it off
    }

    // Runs on a worker thread: if the network hangs, the screen stays responsive (01.11 froze on start
    // while a request waited). The result appears as a window when it is ready.
    void testInternet()
    {
        if (testBusy_.load()) { app.toast("Teste em andamento..."); return; }
        bool up = app.platform.retryNetwork();
        testHeader_ = up ? "Internet do PS4 iniciada.\n" : "A internet do PS4 nao iniciou: " + app.platform.networkStatus() + "\n";
        std::time_t now = std::time(nullptr);   // a wrong console date makes every certificate look invalid
        char date[32] = "?";
        if (std::tm *t = std::gmtime(&now)) std::strftime(date, sizeof date, "%d/%m/%Y %H:%M UTC", t);
        testHeader_ += std::string("Data do console: ") + date + " (se estiver errada, os sites seguros falham).\n";
        if (!up || !app.platform.http()) { app.modal("Testar internet", testHeader_); return; }
        testBusy_ = true;
        testDone_ = false;
        if (!app.platform.startThread(&SettingsScreen::testMain, this, "ml_nettest")) {
            testBusy_ = false;
            app.modal("Testar internet", testHeader_ + "Nao iniciou o teste.");
            return;
        }
        app.toast("Testando a internet (ate 30 s)...");
    }

    static void testMain(void *self) { static_cast<SettingsScreen *>(self)->runTest(); }

    void runTest()
    {
        std::string report;
        HttpClient *h = app.platform.http();
        auto probe = [&](const char *label, const char *url) {
            HttpRequest req;
            req.url = url;
            req.timeoutMs = 10000;
            req.maxBytes = 256 * 1024;
            HttpResponse r = h->get(req);
            if (!r.error.empty()) report += std::string(label) + ": falhou - " + r.error + "\n";
            else report += std::string(label) + ": respondeu " + std::to_string(r.status) + " (" + std::to_string(r.body.size()) + " bytes).\n";
            return r.error.empty();
        };
        bool plain = probe("Site comum (HTTP)", "http://example.com/");
        std::string te;
        int roots = tls::trustedRoots(&te);
        report += roots > 0 ? "Seguranca propria (Mbed TLS): " + std::to_string(roots) + " certificados raiz.\n"
                            : "Seguranca propria nao iniciou: " + te + "\n";
        bool secure = probe("Site seguro (HTTPS)", "https://example.com/");
        if (secure) report += "Tudo certo.\nQuer fazer o teste de video agora? (baixa 4 MB e toca 20 s de teste)";
        else if (plain) report += "A rede funciona; o site seguro falhou no passo mostrado acima.";
        else report += "Nem o site comum abriu: verifique a conexao do console.";
        testOk_ = secure;
        testResult_ = report;   // published by testDone_ (release), read by the UI thread after it
        testDone_.store(true, std::memory_order_release);
    }

    void update(float) override
    {
        if (testBusy_.load() && testDone_.load(std::memory_order_acquire)) {
            testBusy_ = false;
            if (testOk_) {
                App *a = &app;
                app.modal("Testar internet", testHeader_ + testResult_, {"OK", "Testar video"}, [a](int choice) {
                    if (choice == 1) a->push(makeVideoTest(*a));
                });
            } else {
                app.modal("Testar internet", testHeader_ + testResult_);
            }
        }
    }
    bool animating() const override { return testBusy_.load(); }

    // URL -> user -> password, then a ping before saving. The password is turned into a salted token
    // (Subsonic protocol) and discarded; it is never written to disk or logged.
    void configureServer()
    {
        App *a = &app;
        SettingsScreen *self = this;
        app.openKeyboard("Endereco do servidor (ex: http://192.168.15.10:4533)", app.server.url.empty() ? "http://" : app.server.url, true,
                         [a, self](const std::string &url) {
            self->newUrl_ = url;
            a->openKeyboard("Usuario do servidor", a->server.user, false, [a, self](const std::string &user) {
                self->newUser_ = user;
                a->openKeyboard("Senha do servidor", "", false, [a, self](const std::string &pass) {
                    subsonic::Config c = subsonic::makeConfig(self->newUrl_, self->newUser_, pass, a->now());
                    subsonic::Client cl(a->platform.http(), c);
                    std::string e = cl.ping();
                    if (!e.empty()) { a->modal("Servidor de musica", "Nao conectou: " + e); return; }
                    a->setServer(c);
                    a->toast("Servidor conectado. Abra Musica para ver suas playlists.");
                });
            });
        });
    }
};

}  // namespace

std::unique_ptr<Screen> makeLive(App &a) { return std::unique_ptr<Screen>(new LiveScreen(a)); }
std::unique_ptr<Screen> makeLibrary(App &a) { return std::unique_ptr<Screen>(new LibraryScreen(a)); }
std::unique_ptr<Screen> makeSources(App &a) { return std::unique_ptr<Screen>(new SourcesScreen(a)); }
std::unique_ptr<Screen> makeLinkQr(App &a, const std::string &title, const std::string &from, const std::string &url)
{
    return std::unique_ptr<Screen>(new LinkQrScreen(a, title, from, url));
}

std::unique_ptr<Screen> makeSettings(App &a) { return std::unique_ptr<Screen>(new SettingsScreen(a)); }

}  // namespace ml
