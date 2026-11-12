// Videos: the user's own MP4 files (PS4 HD / pendrive) and the full-screen player.
#include <algorithm>
#include <cctype>
#include <cmath>

#include "app/app.hpp"
#include "ui/theme.hpp"

namespace ml {

using namespace ui;

namespace {

bool isVideo(const std::string &n)
{
    if (n.empty() || n[0] == '.') return false;
    size_t dot = n.rfind('.');
    if (dot == std::string::npos) return false;
    std::string e = n.substr(dot + 1);
    for (auto &c : e) c = char(std::tolower(static_cast<unsigned char>(c)));
    return e == "mp4" || e == "m4v" || e == "mov";
}

class VideosScreen : public Screen {
public:
    using Screen::Screen;
    std::string title() const override { return "Videos"; }

    void onEnter() override { scan(); }

    void scan()
    {
        files_.clear();
        for (const auto &dir : app.platform.videoDirs()) {
            std::vector<DirEntry> ents;
            if (!app.platform.fs().listDir(dir, ents)) continue;
            std::sort(ents.begin(), ents.end(), [](const DirEntry &a, const DirEntry &b) { return a.name < b.name; });
            for (const auto &e : ents)
                if (!e.isDir && isVideo(e.name)) files_.push_back({dir + "/" + e.name, e.name, e.size});
        }
        if (sel_ >= int(files_.size())) sel_ = 0;
    }

    bool handle(uint32_t p) override
    {
        int n = int(files_.size());
        if (p & BtnTriangle) { scan(); app.toast(std::to_string(files_.size()) + " videos encontrados"); return true; }
        if (p & BtnUp) { if (sel_ > 0) sel_--; return true; }
        if (p & BtnDown) { if (sel_ + 1 < n) sel_++; return true; }
        if ((p & BtnCross) && n > 0) {
            const File &f = files_[size_t(sel_)];
            std::string name = f.name.substr(0, f.name.rfind('.'));
            app.playVideo(f.path, name);
            return true;
        }
        return false;
    }

    void update(float dt) override
    {
        float target = float(std::max(0, sel_ - 6) * 92);
        float k = app.settings.animations ? std::min(1.0f, dt * 14.0f) : 1.0f;
        anim_ = std::fabs(target - scroll_) > 0.5f;
        scroll_ = anim_ ? scroll_ + (target - scroll_) * k : target;
    }
    bool animating() const override { return anim_; }

    void draw(Canvas &c, Rect area) override
    {
        drawSectionHeader(app, c, area, "Videos", "Seus arquivos MP4 (H.264): " + app.platform.dataDir() + "/videos ou pendrive Videos/");
        int x = area.x + theme::Margin, top = 186, w = area.w - 2 * theme::Margin;
        if (files_.empty()) {
            Rect r{x, top, w, 260};
            c.fillRoundRect(r.x, r.y, r.w, r.h, 24, theme::Surface);
            drawIcon(c, Icon::Film, float(r.x + 80), float(r.y + 90), 64, theme::Glow);
            app.fonts.bold->draw(c, r.x + 150, r.y + 50, 32, "Nenhum video encontrado", theme::Text);
            auto lines = app.fonts.regular->wrap(23, "Coloque arquivos .mp4 numa pasta Videos na raiz do pendrive, ou em " + app.platform.dataDir() +
                                                         "/videos pelo FTP, e aperte triangulo para procurar de novo. O player do PS4 ainda e "
                                                         "experimental: se algo falhar, a tela do player mostra em que passo parou.",
                                                 r.w - 200, 4);
            int y = r.y + 104;
            for (const auto &l : lines) { app.fonts.regular->draw(c, r.x + 150, y, 23, l, theme::TextDim); y += 34; }
            return;
        }
        c.pushClip({area.x, top - 20, area.w, area.bottom() - top + 20});
        int y = top - int(scroll_);
        for (size_t i = 0; i < files_.size(); i++, y += 92) {
            if (y + 92 < top - 20 || y > area.bottom()) continue;
            const File &f = files_[i];
            bool foc = int(i) == sel_ && !app.sidebarFocused();
            Rect r{x, y, w, 80};
            if (foc) {
                c.shadow(r.x, r.y, r.w, r.h, 16, 16, withAlpha(theme::Violet, 90));
                c.fillRoundRect(r.x, r.y, r.w, r.h, 16, theme::SurfaceHi);
                c.strokeRoundRect(r.x, r.y, r.w, r.h, 16, 2, rgba(196, 181, 253));
            } else {
                c.fillRoundRect(r.x, r.y, r.w, r.h, 16, theme::Surface);
            }
            drawGeneratedArt(c, app.fonts, {r.x + 10, r.y + 10, 106, 60}, 10, "video:" + f.name, "", false);
            drawIcon(c, Icon::Play, float(r.x + 63), float(r.y + 40), 24, rgba(255, 255, 255, 230));
            app.fonts.bold->draw(c, r.x + 140, r.y + 12, 25, app.fonts.bold->ellipsize(25, f.name, r.w - 360), theme::Text);
            std::string where = f.path.compare(0, 8, "/mnt/usb") == 0 ? "pendrive" : "HD do PS4";
            char sz[32];
            std::snprintf(sz, sizeof sz, "%.1f MB", double(f.size) / (1024.0 * 1024.0));
            app.fonts.regular->draw(c, r.x + 140, r.y + 46, 19, where + "  -  " + sz, theme::TextMute);
        }
        c.popClip();
    }

    Hints hints() const override { return {{BtnCross, "Assistir"}, {BtnTriangle, "Procurar de novo"}}; }

private:
    struct File {
        std::string path, name;
        uint64_t size;
    };
    std::vector<File> files_;
    int sel_ = 0;
    float scroll_ = 0;
    bool anim_ = false;
};

// ---------------------------------------------------------------- full-screen player
class PlayerScreen : public Screen {
public:
    PlayerScreen(App &a, std::unique_ptr<VideoEngine> e, std::string location, PlayContext ctx)
        : Screen(a), engine_(std::move(e)), location_(std::move(location)), title_(ctx.title), ctx_(std::move(ctx))
    {
        frame_.w = theme::W;
        frame_.h = theme::H;
        frame_.px.assign(size_t(frame_.w) * frame_.h, 0xFF000000u);
    }
    ~PlayerScreen() override { if (engine_) engine_->stop(); }
    std::string title() const override { return title_; }
    bool fullscreen() const override { return true; }

    // Opening reads from the network (and the system player reads while adding the source), so it
    // happens after the "Abrindo..." frame is on screen instead of freezing on the previous screen.
    void onEnter() override
    {
        if (started_) return;
        started_ = true;
        overlayUntil_ = app.now() + 4000;
    }

    bool handle(uint32_t p) override
    {
        if (p & (BtnCircle | BtnOptions)) {
            engine_->stop();
            app.pop();   // destroys this screen: nothing may touch members after this
            return true;
        }
        overlayUntil_ = app.now() + 4000;
        if (p & (BtnCross | BtnSquare)) { if (engine_->paused()) engine_->resume(); else engine_->pause(); }
        if (p & (BtnLeft | BtnRight | BtnL1 | BtnR1)) {
            int64_t d = (p & BtnLeft) ? -10000 : (p & BtnRight) ? 10000 : (p & BtnL1) ? -60000 : 60000;
            engine_->seek(engine_->positionMs() + d);
        }
        if (p & BtnTriangle) details_ = !details_;
        return true;
    }

    void update(float) override
    {
        if (!opened_) {
            if (drawn_ < 2) return;   // let "Abrindo o video..." reach the screen first
            opened_ = true;
            std::string err;
            if (!engine_->open(location_, &err)) fail(err);
            openedAt_ = app.now();
            overlayUntil_ = app.now() + 4000;
            return;
        }
        if (!error_.empty()) return;
        if (engine_->updateFrame(frame_.fb())) { newFrame_ = true; gotFrame_ = true; }
        // The player took the file but shows nothing: say so instead of a black screen forever.
        if (!gotFrame_ && app.now() - openedAt_ > app.tuning.firstFrameMs)
            fail("o player aceitou o arquivo, mas nenhuma imagem chegou em " + std::to_string(app.tuning.firstFrameMs / 1000) +
                 " s (parou em: " + engine_->status() + ")");
    }

    // Records the problem (name, poster, source, reason, technical details) and stops the engine.
    void fail(const std::string &reason)
    {
        error_ = reason;
        Problem p;
        p.title = ctx_.title;
        p.poster = ctx_.poster;
        p.source = ctx_.source;
        p.where = hostForLog(location_);
        p.reason = reason;
        std::string diag = engine_->diagnostics();
        p.details = "ultimo passo: " + engine_->status() + (diag.empty() ? "" : "\n" + diag);
        app.reportProblem(p);
        engine_->stop();
    }
    bool animating() const override { return true; }   // keep polling for decoded frames

    void draw(Canvas &c, Rect) override
    {
        c.drawImage(frame_, 0, 0);
        newFrame_ = false;
        if (!opened_) {
            drawn_++;
            app.fonts.bold->draw(c, 80, 50, 40, app.fonts.bold->ellipsize(40, title_, theme::W - 160), theme::Text);
            app.fonts.regular->draw(c, 200, theme::H / 2 - 20, 30, "Abrindo o video... (pode levar alguns segundos)", theme::TextDim);
            return;
        }
        bool overlay = !error_.empty() || details_ || engine_->paused() || app.now() < overlayUntil_;
        if (!overlay) return;
        c.fillGradient(0, theme::H - 220, theme::W, 220, 0, rgba(0, 0, 0, 0), rgba(0, 0, 0, 200), 1);
        c.fillGradient(0, 0, theme::W, 160, 0, rgba(0, 0, 0, 190), rgba(0, 0, 0, 0), 1);
        app.fonts.bold->draw(c, 80, 50, 40, app.fonts.bold->ellipsize(40, title_, theme::W - 160), theme::Text);
        if (!error_.empty()) {
            auto lines = app.fonts.regular->wrap(28, "Nao foi possivel tocar: " + error_, theme::W - 400, 5);
            int y = theme::H / 2 - 40;
            for (const auto &l : lines) { app.fonts.regular->draw(c, 200, y, 28, l, theme::Warn); y += 40; }
            app.fonts.regular->draw(c, 200, y + 20, 22, "Ficou anotado em Configuracoes > Problemas ao reproduzir (com os detalhes para foto).", theme::TextDim);
            app.fonts.regular->draw(c, 200, y + 56, 22, "O para voltar", theme::TextDim);
            return;
        }
        int64_t pos = engine_->positionMs(), dur = engine_->durationMs();
        int x = 80, y = theme::H - 110, w = theme::W - 160;
        drawIcon(c, engine_->paused() ? Icon::Play : Icon::Pause, float(x + 20), float(y - 30), 34, theme::Text);
        drawProgress(c, x + 70, y - 34, w - 340, 8, dur > 0 ? double(pos) / double(dur) : 0, theme::Blue, rgba(255, 255, 255, 50));
        app.fonts.regular->draw(c, x + w - 250, y - 48, 24, formatTime(pos) + " / " + (dur > 0 ? formatTime(dur) : "--:--"), theme::Text);
        drawHints(c, app.fonts, {{BtnCross, "Pausar"}, {BtnLeft | BtnRight | BtnUp | BtnDown, "-10s / +10s"}, {BtnL1 | BtnR1, "-1 / +1 min"},
                                 {BtnTriangle, "Detalhes"}, {BtnCircle, "Sair"}},
                  x + w, y + 10);
        if (details_) app.fonts.regular->draw(c, 80, 110, 22, "Motor: " + engine_->status(), theme::TextDim);
        if (engine_->finished()) app.fonts.bold->draw(c, 80, 150, 26, "Fim do video. O para voltar.", theme::Glow);
    }

private:
    std::unique_ptr<VideoEngine> engine_;
    std::string location_, title_, error_;
    PlayContext ctx_;
    Image frame_;
    bool started_ = false, newFrame_ = false, details_ = false, opened_ = false, gotFrame_ = false;
    int drawn_ = 0;
    uint64_t overlayUntil_ = 0, openedAt_ = 0;
};

}  // namespace

std::unique_ptr<Screen> makeVideos(App &a) { return std::unique_ptr<Screen>(new VideosScreen(a)); }
std::unique_ptr<Screen> makePlayer(App &a, std::unique_ptr<VideoEngine> engine, const std::string &location, const PlayContext &ctx)
{
    return std::unique_ptr<Screen>(new PlayerScreen(a, std::move(engine), location, ctx));
}

}  // namespace ml
