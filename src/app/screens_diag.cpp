// Diagnostics screens:
//  - Problemas ao reproduzir: what did not play (name, poster, reason) + a details page to photograph;
//  - Teste de video: step-by-step check of the system player with every system code on screen.
#include <algorithm>
#include <atomic>
#include <ctime>
#include <memory>

#include "app/app.hpp"
#include "app/multi_fetch.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

namespace ml {

using namespace ui;

namespace {

void listRow(App &app, Canvas &c, Rect r, bool focused)
{
    if (focused) {
        c.shadow(r.x, r.y, r.w, r.h, 16, 16, withAlpha(theme::Violet, 90));
        c.fillRoundRect(r.x, r.y, r.w, r.h, 16, theme::SurfaceHi);
        c.strokeRoundRect(r.x, r.y, r.w, r.h, 16, 2, rgba(196, 181, 253));
    } else {
        c.fillRoundRect(r.x, r.y, r.w, r.h, 16, theme::Surface);
    }
    (void)app;
}

std::string ago(int64_t when)
{
    int64_t d = int64_t(std::time(nullptr)) - when;
    if (when <= 0 || d < 0) return "";
    if (d < 90) return "agora ha pouco";
    if (d < 3600) return "ha " + std::to_string(d / 60) + " min";
    if (d < 48 * 3600) return "ha " + std::to_string(d / 3600) + " h";
    return "ha " + std::to_string(d / 86400) + " dias";
}

// Long text split into lines that fit the page, keeping the text's own line breaks.
std::vector<std::string> pageLines(Font *f, int px, const std::string &text, int width)
{
    std::vector<std::string> out;
    size_t a = 0;
    while (a <= text.size()) {
        size_t b = text.find('\n', a);
        std::string line = text.substr(a, b == std::string::npos ? std::string::npos : b - a);
        if (line.empty()) out.push_back("");
        else for (auto &l : f->wrap(px, line, width, 12)) out.push_back(l);
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return out;
}

// ---------------------------------------------------------------- one problem, full text
class ProblemDetailScreen : public Screen {
public:
    ProblemDetailScreen(App &a, Problem p) : Screen(a), p_(std::move(p)) {}
    std::string title() const override { return "Detalhes do problema"; }

    bool handle(uint32_t b) override
    {
        if (b & BtnUp) { if (first_ > 0) first_--; return true; }
        if (b & BtnDown) { first_++; return true; }
        return false;
    }

    void draw(Canvas &c, Rect area) override
    {
        drawSectionHeader(app, c, area, "Detalhes do problema", "Tire uma foto desta tela e mande para quem desenvolve o app");
        int x = area.x + theme::Margin, w = area.w - 2 * theme::Margin;
        std::string text = "Titulo: " + p_.title + "\n" + (p_.source.empty() ? "" : "Fonte / link: " + p_.source + "\n") +
                           (p_.where.empty() ? "" : "Servidor: " + p_.where + "\n") + "Quando: " + ago(p_.when) +
                           "   Versao do app: " + ML_VERSION + "\n" + "Motivo: " + p_.reason + "\n\n" + p_.details;
        std::vector<std::string> lines = pageLines(app.fonts.regular, 22, text, w);
        const int lineH = 30;
        int visible = std::max(1, (area.bottom() - 180) / lineH);
        int maxFirst = std::max(0, int(lines.size()) - visible);
        if (first_ > maxFirst) first_ = maxFirst;
        int y = 180;
        for (int i = first_; i < int(lines.size()) && i < first_ + visible; i++, y += lineH)
            app.fonts.regular->draw(c, x, y, 22, lines[size_t(i)], i < 5 ? theme::Text : theme::TextDim);
        if (maxFirst > 0) {
            std::string pg = std::to_string(first_ + 1) + "-" + std::to_string(std::min(int(lines.size()), first_ + visible)) + " de " +
                             std::to_string(lines.size()) + " linhas";
            app.fonts.regular->draw(c, area.right() - theme::Margin - app.fonts.regular->measure(20, pg), 140, 20, pg, theme::TextMute);
        }
    }

    Hints hints() const override { return {{BtnUp | BtnDown, "Rolar"}, {BtnCircle, "Voltar"}}; }

private:
    Problem p_;
    int first_ = 0;
};

// ---------------------------------------------------------------- list of problems
class ProblemsScreen : public Screen {
public:
    using Screen::Screen;
    std::string title() const override { return "Problemas ao reproduzir"; }

    bool handle(uint32_t b) override
    {
        int n = int(app.problems.entries().size());
        if (b & BtnUp) { if (sel_ > 0) sel_--; return true; }
        if (b & BtnDown) { if (sel_ + 1 < n) sel_++; return true; }
        if ((b & BtnCross) && sel_ < n) {
            app.push(std::unique_ptr<Screen>(new ProblemDetailScreen(app, app.problems.entries()[size_t(sel_)])));
            return true;
        }
        if ((b & BtnSquare) && n > 0) {
            App *a = &app;
            app.modal("Problemas ao reproduzir", "Apagar a lista de problemas?", {"Apagar", "Cancelar"}, [a](int choice) {
                if (choice == 0) a->problems.clear();
            });
            return true;
        }
        return false;
    }

    void draw(Canvas &c, Rect area) override
    {
        const auto &list = app.problems.entries();
        drawSectionHeader(app, c, area, "Problemas ao reproduzir",
                          list.empty() ? "Nada anotado" : std::to_string(list.size()) + (list.size() == 1 ? " anotado" : " anotados") +
                                                              "   -   X abre os detalhes para foto");
        int x = area.x + theme::Margin, w = area.w - 2 * theme::Margin, top = 176;
        if (list.empty()) {
            app.fonts.regular->draw(c, x, top + 20, 26, "Quando um filme, serie ou video nao tocar, o motivo aparece aqui.", theme::TextDim);
            return;
        }
        if (sel_ >= int(list.size())) sel_ = int(list.size()) - 1;
        const int rowH = 136;
        int visible = std::max(1, (area.bottom() - top) / rowH);
        if (sel_ < first_) first_ = sel_;
        if (sel_ >= first_ + visible) first_ = sel_ - visible + 1;
        for (int i = first_; i < int(list.size()) && i < first_ + visible; i++) {
            const Problem &p = list[size_t(i)];
            Rect r{x, top + (i - first_) * rowH, w, rowH - 10};
            listRow(app, c, r, i == sel_ && !app.sidebarFocused());
            Rect art{r.x + 14, r.y + 9, 72, 108};
            if (const Image *img = p.poster.empty() ? nullptr : app.images.get(p.poster, art.w, art.h)) c.drawImage(*img, art.x, art.y, 8);
            else drawGeneratedArt(c, app.fonts, art, 8, "problem:" + p.title, "", false);
            int tx = art.right() + 22, tw = r.right() - tx - 24;
            app.fonts.bold->draw(c, tx, r.y + 10, 25, app.fonts.bold->ellipsize(25, p.title.empty() ? "(sem nome)" : p.title, tw - 260), theme::Text);
            std::string when = ago(p.when);
            app.fonts.regular->draw(c, r.right() - 24 - app.fonts.regular->measure(19, when), r.y + 14, 19, when, theme::TextMute);
            int ly = r.y + 46;
            for (const auto &l : app.fonts.regular->wrap(21, p.reason, tw, 2)) { app.fonts.regular->draw(c, tx, ly, 21, l, theme::Warn); ly += 28; }
            std::string from = p.source + (p.source.empty() || p.where.empty() ? "" : "   -   ") + p.where;
            app.fonts.regular->draw(c, tx, r.bottom() - 28, 18, app.fonts.regular->ellipsize(18, from, tw), theme::TextMute);
        }
    }

    Hints hints() const override { return {{BtnCross, "Detalhes"}, {BtnSquare, "Apagar tudo"}, {BtnCircle, "Voltar"}}; }

    std::string describe() const override { return "problemas=" + std::to_string(app.problems.entries().size()); }

private:
    int sel_ = 0, first_ = 0;
};

// ---------------------------------------------------------------- video test
// 1. downloads the known-good test MP4 (H.264 High 4.0 + AAC 48 kHz, 20 s) to the data folder with the
//    app's own HTTPS;
// 2. plays the LOCAL file with the system player (no network involved);
//    if that fails: the same again capturing the player's internal log;
// 3. plays the same video straight from the internet (the app reads, the player decodes).
// Every step leaves its codes on screen and in the problem log.
class VideoTestScreen : public Screen {
public:
    explicit VideoTestScreen(App &a) : Screen(a), dl_(std::make_shared<Download>())
    {
        thumb_.w = 640;
        thumb_.h = 360;
        thumb_.px.assign(size_t(thumb_.w) * thumb_.h, 0xFF000000u);
    }
    ~VideoTestScreen() override { if (engine_) engine_->stop(); }
    std::string title() const override { return "Teste de video"; }

    void onEnter() override
    {
        if (started_) return;
        started_ = true;
        path_ = app.platform.dataDir() + "/teste_video.mp4";
        say("1. Baixando o video de teste (4 MB) com a internet do app...", theme::Text);
        std::shared_ptr<Download> dl = dl_;
        Platform *plat = &app.platform;
        std::string path = path_;
        std::string url = app.videoTestUrl;
        bool ok = MultiFetch::runDetached(app.platform, [dl, plat, path, url] {
            std::string err;
            HttpClient *h = plat->http();
            if (!h) {
                err = "sem rede";
            } else {
                HttpRequest q;
                q.url = url;
                q.timeoutMs = 30000;
                q.maxBytes = 16 * 1024 * 1024;
                HttpResponse r = h->get(q);
                if (!r.error.empty()) err = r.error;
                else if (r.status != 200) err = "o servidor respondeu HTTP " + std::to_string(r.status);
                else if (r.body.size() < 100 * 1024 || r.body.compare(4, 4, "ftyp") != 0) err = "o arquivo baixado nao e o video de teste";
                else if (!plat->fs().writeFile(path, r.body)) err = "nao gravou em " + path + " (" + plat->fs().lastError() + ")";
                else dl->bytes = r.body.size();
            }
            dl->error = err;
            dl->done.store(true, std::memory_order_release);
        });
        if (!ok) { dl_->error = "nao iniciou o download"; dl_->done = true; }
        step_ = Step::Downloading;
    }

    bool handle(uint32_t b) override
    {
        if (b & BtnUp) { if (first_ > 0) first_--; manual_ = true; return true; }
        if (b & BtnDown) { first_++; manual_ = true; return true; }
        return false;
    }

    void update(float) override
    {
        uint64_t now = app.now();
        switch (step_) {
        case Step::Idle:
        case Step::Done:
            break;
        case Step::Downloading:
            if (!dl_->done.load(std::memory_order_acquire)) break;
            if (!dl_->error.empty()) {
                bad("   falhou: " + dl_->error);
                summary_ = "o video de teste nao baixou (problema de internet, nao do player)";
                finish();
                break;
            }
            good("   ok: " + std::to_string(dl_->bytes / 1024) + " KB gravados em " + path_);
            begin(Step::Local, "2. Tocando o arquivo do HD pelo player do PS4 (sem internet)...", path_, false);
            break;
        case Step::Local:
        case Step::LocalLog:
        case Step::Network:
            if (!drawnSinceBegin_) break;   // the step's line must reach the screen before a blocking open
            if (!opened_) { open(); break; }
            if (failed_) { stepFailed(); break; }
            if (engine_->updateFrame(thumb_.fb())) {
                if (!gotFrame_) { gotFrame_ = true; firstFrameAt_ = now; }
                app.dirty();
            }
            if (!gotFrame_ && now - openedAt_ > app.tuning.testWaitMs) {
                failed_ = true;
                failure_ = "o player aceitou o arquivo, mas nenhuma imagem chegou em " + std::to_string(app.tuning.testWaitMs / 1000) + " s (parou em: " +
                           engine_->status() + ")";
                break;
            }
            if (gotFrame_ && now - firstFrameAt_ > app.tuning.testPlayMs) stepPassed();
            break;
        }
    }
    bool animating() const override { return step_ != Step::Done && step_ != Step::Idle; }

    void draw(Canvas &c, Rect area) override
    {
        drawSectionHeader(app, c, area, "Teste de video", step_ == Step::Done ? "Terminado. Tire uma foto desta tela." : "Testando... nao saia desta tela");
        drawnSinceBegin_ = true;
        int x = area.x + theme::Margin;
        const int thumbW = thumb_.w, textW = area.w - 2 * theme::Margin - thumbW - 40;
        // moving picture from the player (proof that image arrives), top right
        int tx = area.right() - theme::Margin - thumbW, ty = 180;
        c.drawImage(thumb_, tx, ty, 12);
        app.fonts.regular->draw(c, tx, ty + thumb_.h + 12, 20, gotFrameEver_ ? "Imagem vinda do player do PS4" : "Aqui aparece a imagem quando o player funciona",
                                gotFrameEver_ ? theme::Good : theme::TextMute);
        const int lineH = 30;
        std::vector<std::pair<std::string, Color>> lines;
        for (const auto &l : report_)
            for (auto &w : pageLines(app.fonts.regular, 21, l.first, textW)) lines.push_back({w, l.second});
        int visible = std::max(1, (area.bottom() - 180) / lineH);
        int maxFirst = std::max(0, int(lines.size()) - visible);
        if (!manual_ || first_ > maxFirst) first_ = maxFirst;   // follow the newest line until the user scrolls
        int y = 180;
        for (int i = first_; i < int(lines.size()) && i < first_ + visible; i++, y += lineH)
            app.fonts.regular->draw(c, x, y, 21, lines[size_t(i)].first, lines[size_t(i)].second);
    }

    Hints hints() const override { return {{BtnUp | BtnDown, "Rolar"}, {BtnCircle, "Voltar"}}; }

    std::string describe() const override
    {
        return std::string("passo=") + (step_ == Step::Done ? "fim" : step_ == Step::Downloading ? "baixando" : "tocando") + " resultado=" + summary_;
    }

private:
    enum class Step { Idle, Downloading, Local, LocalLog, Network, Done };
    struct Download {
        std::atomic<bool> done{false};
        std::string error;
        size_t bytes = 0;
    };

    std::shared_ptr<Download> dl_;
    std::unique_ptr<VideoEngine> engine_;
    Image thumb_;
    std::vector<std::pair<std::string, Color>> report_;
    std::string path_, location_, failure_, summary_;
    Step step_ = Step::Idle;
    bool started_ = false, opened_ = false, failed_ = false, gotFrame_ = false, gotFrameEver_ = false, drawnSinceBegin_ = false, manual_ = false;
    bool localOk_ = false, networkOk_ = false;
    uint64_t openedAt_ = 0, firstFrameAt_ = 0;
    int first_ = 0;

    void say(const std::string &t, Color col) { report_.push_back({t, col}); app.dirty(); }
    void good(const std::string &t) { say(t, theme::Good); }
    void bad(const std::string &t) { say(t, theme::Warn); }

    void begin(Step s, const std::string &line, const std::string &location, bool internalLog)
    {
        step_ = s;
        location_ = location;
        opened_ = failed_ = gotFrame_ = false;
        drawnSinceBegin_ = false;
        say(line, theme::Text);
        engine_ = app.platform.makeVideoEngine();
        if (!engine_) {
            failed_ = true;
            opened_ = true;
            std::string why = app.platform.videoEngineError();
            failure_ = "o player de video do sistema nao carregou" + (why.empty() ? std::string() : " (" + why + ")");
            return;
        }
        engine_->setInternalLog(internalLog);
    }

    void open()
    {
        opened_ = true;
        openedAt_ = app.now();
        std::string err;
        if (!engine_->open(location_, &err)) {
            failed_ = true;
            failure_ = err;
        }
        openedAt_ = app.now();
    }

    std::string diag()
    {
        if (!engine_) return "";
        std::string d = engine_->diagnostics();
        return d.empty() ? "" : d;
    }

    void stepPassed()
    {
        good("   ok: imagem e som chegando (" + engine_->status() + ")");
        gotFrameEver_ = true;
        std::string d = diag();
        engine_->stop();
        engine_.reset();
        if (step_ == Step::Local || step_ == Step::LocalLog) {
            localOk_ = true;
            if (!d.empty()) say(d, theme::TextMute);
            begin(Step::Network, "3. Tocando o mesmo video direto da internet (o app le, o player do PS4 decodifica)...", app.videoTestUrl, false);
        } else {
            networkOk_ = true;
            if (!d.empty()) say(d, theme::TextMute);
            summary_ = "o player do PS4 funciona com MP4, do HD e pela internet";
            finish();
        }
    }

    void stepFailed()
    {
        bad("   falhou: " + failure_);
        std::string d = diag();
        if (!d.empty()) say(d, theme::TextDim);
        if (engine_) { engine_->stop(); engine_.reset(); }
        if (step_ == Step::Local) {
            // once more, now capturing what the system player itself says
            begin(Step::LocalLog, "2b. De novo, anotando as mensagens internas do player do PS4...", path_, true);
        } else if (step_ == Step::LocalLog) {
            summary_ = "o player do PS4 nao tocou nem o MP4 de teste gravado no HD";
            say("3. Pela internet: pulado (o player nao tocou nem o arquivo do HD).", theme::TextDim);
            finish();
        } else {
            summary_ = "o player toca o arquivo do HD, mas nao pela internet (leitura feita pelo app)";
            finish();
        }
    }

    void finish()
    {
        step_ = Step::Done;
        bool ok = localOk_ && networkOk_;
        say("", theme::Text);
        say("RESULTADO: " + summary_ + ".", ok ? theme::Good : theme::Warn);
        say("Versao " + std::string(ML_VERSION) + ". Role com as setas para ver tudo; a lista tambem fica em Problemas ao reproduzir.", theme::TextMute);
        if (!ok) {
            Problem p;
            p.title = "Teste de video";
            p.source = "video de teste do Moonlight";
            p.where = hostForLog(app.videoTestUrl);
            p.reason = summary_;
            for (const auto &l : report_) p.details += l.first + "\n";
            app.reportProblem(p);
        }
    }
};

}  // namespace

std::unique_ptr<Screen> makeProblems(App &a) { return std::unique_ptr<Screen>(new ProblemsScreen(a)); }
std::unique_ptr<Screen> makeVideoTest(App &a) { return std::unique_ptr<Screen>(new VideoTestScreen(a)); }

}  // namespace ml
