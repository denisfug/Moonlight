// Light Tester — tela de diagnóstico interativo para testes no PS4.
// Fullscreen, sem sidebar. Cada item tem um número, título e status.
// X executa o teste selecionado; resultado aparece na mesma linha.
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "app/app.hpp"
#include "core/net.hpp"
#include "player/player.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

namespace ml {

using namespace ui;

namespace {

// ─── Helpers ───────────────────────────────────────────────────────────────

enum class Status { Idle, Running, Ok, Fail };

struct TestItem {
    std::string label;
    Status status = Status::Idle;
    std::string detail;   // resultado curto exibido ao lado
};

static uint32_t statusColor(Status s)
{
    switch (s) {
    case Status::Ok:      return rgba(0x2E, 0xCC, 0x71, 255);   // verde
    case Status::Fail:    return rgba(0xE7, 0x4C, 0x3C, 255);   // vermelho
    case Status::Running: return rgba(0xF3, 0x9C, 0x12, 255);   // laranja
    default:              return theme::TextSub();
    }
}

static const char *statusText(Status s)
{
    switch (s) {
    case Status::Ok:      return "OK";
    case Status::Fail:    return "FALHOU";
    case Status::Running: return "...";
    default:              return "—";
    }
}

// ─── TesterScreen ──────────────────────────────────────────────────────────

class TesterScreen : public Screen {
public:
    explicit TesterScreen(App &a) : Screen(a)
    {
        items_ = {
            {"1. Teclado — digitar e confirmar"},
            {"2. Audio local — MP3 do pendrive"},
            {"3. HTTP — conectar archive.org"},
            {"4. HTTPS/TLS — conectar api.audius.co"},
            {"5. Stream de radio — Radio Browser"},
            {"6. Busca de musica — Internet Archive"},
            {"7. Busca de musica — Audius"},
        };
    }

    std::string title() const override { return "Testes"; }
    bool fullscreen() const override { return true; }

    bool handle(uint32_t p) override
    {
        int n = int(items_.size());
        if (p & BtnUp)   { if (sel_ > 0) sel_--; return true; }
        if (p & BtnDown) { if (sel_ + 1 < n) sel_++; return true; }
        if (p & BtnCross) { runTest(sel_); return true; }
        if (p & BtnCircle) { return false; }   // volta para sidebar
        return true;
    }

    void draw(Canvas &c, Rect area) override
    {
        const int px = 80, py = 60;
        const int rowH = 72, gap = 10;

        // título
        app.fonts.bold->draw(c, px, py, 42, "Light Tester  v" ML_VERSION, theme::Text());
        app.fonts.regular->draw(c, px, py + 52, 26,
            "X = executar    Circulo = voltar    R1 = limpar resultados",
            theme::TextSub());

        int y = py + 110;
        for (int i = 0; i < int(items_.size()); i++) {
            bool focused = (i == sel_);
            Rect r{area.x + px - 16, y, area.w - px * 2 + 32, rowH};
            if (focused) {
                c.fillRoundRect(r.x, r.y, r.w, r.h, 14, theme::BgHover());
                c.strokeRoundRect(r.x, r.y, r.w, r.h, 14, 2, theme::Red);
            } else {
                c.fillRoundRect(r.x, r.y, r.w, r.h, 14, theme::BgSurface());
            }
            // label
            int textY = y + (rowH - app.fonts.regular->lineHeight(28)) / 2;
            app.fonts.regular->draw(c, px, textY, 28, items_[size_t(i)].label,
                focused ? theme::Text() : theme::TextSub());
            // status badge
            Status s = items_[size_t(i)].status;
            const char *st = statusText(s);
            uint32_t col = statusColor(s);
            int sw = app.fonts.bold->measure(24, st);
            int bx = area.x + area.w - px - sw - 48;
            if (s != Status::Idle) {
                c.fillRoundRect(bx - 16, textY - 4, sw + 32, 36, 8, withAlpha(col, 40));
                app.fonts.bold->draw(c, bx, textY, 24, st, col);
            }
            // detail text
            if (!items_[size_t(i)].detail.empty()) {
                int dx = bx - 20 - app.fonts.regular->measure(22, items_[size_t(i)].detail);
                app.fonts.regular->draw(c, dx, textY + 2, 22, items_[size_t(i)].detail, theme::TextSub());
            }
            y += rowH + gap;
        }

        // dica de rodapé
        int hy = area.y + area.h - 52;
        drawHints(c, app.fonts, {{BtnCross, "Executar"}, {BtnR1, "Limpar"}, {BtnCircle, "Voltar"}},
                  area.x + area.w - 60, hy);
    }

    Hints hints() const override
    {
        return {{BtnCross, "Executar"}, {BtnR1, "Limpar"}, {BtnCircle, "Voltar"}};
    }

    bool handle_extra(uint32_t p)
    {
        if (p & BtnR1) {
            for (auto &it : items_) { it.status = Status::Idle; it.detail = ""; }
            return true;
        }
        return false;
    }

private:
    std::vector<TestItem> items_;
    int sel_ = 0;

    void setStatus(int i, Status s, const std::string &detail = "")
    {
        items_[size_t(i)].status = s;
        items_[size_t(i)].detail = detail;
        app.dirty();
    }

    void runTest(int i)
    {
        if (items_[size_t(i)].status == Status::Running) return;
        setStatus(i, Status::Running);

        switch (i) {
        case 0: testTeclado(); break;
        case 1: testAudioLocal(); break;
        case 2: testHttp(); break;
        case 3: testHttps(); break;
        case 4: testRadioStream(); break;
        case 5: testBuscaArchive(); break;
        case 6: testBuscaAudius(); break;
        default: setStatus(i, Status::Fail, "nao implementado"); break;
        }
    }

    // ── Teste 1: teclado ────────────────────────────────────────────────────
    void testTeclado()
    {
        app.openKeyboard("Teste de teclado", "", false, [this](const std::string &text) {
            if (!text.empty())
                setStatus(0, Status::Ok, "\"" + text.substr(0, 20) + "\"");
            else
                setStatus(0, Status::Fail, "vazio ou cancelado");
        });
    }

    // ── Teste 2: audio local ─────────────────────────────────────────────────
    void testAudioLocal()
    {
        // Tenta tocar o primeiro MP3 da biblioteca local
        auto &tracks = app.music.tracks();
        for (size_t t = 0; t < tracks.size(); t++) {
            if (!tracks[t].path.empty() && !tracks[t].demo) {
                std::string err;
                if (app.startTrack(t, &err)) {
                    setStatus(1, Status::Ok, tracks[t].title.empty() ? "tocando" : tracks[t].title.substr(0, 24));
                } else {
                    setStatus(1, Status::Fail, err.substr(0, 40));
                }
                return;
            }
        }
        setStatus(1, Status::Fail, "nenhum MP3 local encontrado");
    }

    // ── Teste 3: HTTP ────────────────────────────────────────────────────────
    void testHttp()
    {
        int idx = 2;
        std::thread([this, idx] {
            HttpRequest req;
            req.url = app.archiveBase + "/advancedsearch.php?q=test&rows=1&output=json";
            req.timeoutMs = 6000;
            std::string err;
            auto stream = app.platform.http()->openStream(req, &err);
            if (stream) {
                char buf[64]; stream->read(buf, sizeof buf);
                setStatus(idx, Status::Ok, "conectou");
            } else {
                setStatus(idx, Status::Fail, err.substr(0, 40));
            }
        }).detach();
    }

    // ── Teste 4: HTTPS/TLS ───────────────────────────────────────────────────
    void testHttps()
    {
        int idx = 3;
        std::thread([this, idx] {
            HttpRequest req;
            req.url = app.audiusBase + "/v1/tracks/trending?limit=1";
            req.timeoutMs = 8000;
            std::string err;
            auto stream = app.platform.http()->openStream(req, &err);
            if (stream) {
                char buf[64]; stream->read(buf, sizeof buf);
                setStatus(idx, Status::Ok, "TLS OK");
            } else {
                setStatus(idx, Status::Fail, err.substr(0, 40));
            }
        }).detach();
    }

    // ── Teste 5: stream de radio ─────────────────────────────────────────────
    void testRadioStream()
    {
        int idx = 4;
        // Usa a primeira estação favorita, ou busca uma ao vivo
        if (!app.radioFavorites.list().empty()) {
            const auto &s = app.radioFavorites.list()[0];
            app.playStation(s);
            // Aguarda até 3 s para confirmar que conectou
            std::thread([this, idx] {
                for (int i = 0; i < 60; i++) {
                    app.platform.sleepMs(50);
                    auto st = app.audio.state();
                    if (st == PlayState::Playing) { setStatus(idx, Status::Ok, "streaming"); return; }
                    if (st == PlayState::Error)   { setStatus(idx, Status::Fail, app.audio.lastError().substr(0, 40)); return; }
                }
                setStatus(idx, Status::Fail, "timeout");
            }).detach();
        } else {
            setStatus(idx, Status::Fail, "sem favoritos — adicione uma radio primeiro");
        }
    }

    // ── Teste 6: busca Archive.org ───────────────────────────────────────────
    void testBuscaArchive()
    {
        int idx = 5;
        std::thread([this, idx] {
            HttpRequest req;
            req.url = app.archiveBase + "/advancedsearch.php?q=jazz&mediatype=audio&rows=3&output=json";
            req.timeoutMs = 8000;
            std::string err, body;
            auto s = app.platform.http()->openStream(req, &err);
            if (!s) { setStatus(idx, Status::Fail, err.substr(0, 40)); return; }
            char buf[256]; long n;
            while ((n = s->read(buf, sizeof buf)) > 0) body.append(buf, size_t(n));
            bool ok = body.find("\"response\"") != std::string::npos;
            setStatus(idx, ok ? Status::Ok : Status::Fail,
                      ok ? std::to_string(body.size()) + " bytes" : "resposta invalida");
        }).detach();
    }

    // ── Teste 7: busca Audius ────────────────────────────────────────────────
    void testBuscaAudius()
    {
        int idx = 6;
        std::thread([this, idx] {
            HttpRequest req;
            req.url = app.audiusBase + "/v1/tracks/search?query=jazz&limit=3";
            req.timeoutMs = 8000;
            std::string err, body;
            auto s = app.platform.http()->openStream(req, &err);
            if (!s) { setStatus(idx, Status::Fail, err.substr(0, 40)); return; }
            char buf[256]; long n;
            while ((n = s->read(buf, sizeof buf)) > 0) body.append(buf, size_t(n));
            bool ok = body.find("\"data\"") != std::string::npos;
            setStatus(idx, ok ? Status::Ok : Status::Fail,
                      ok ? std::to_string(body.size()) + " bytes" : "resposta invalida");
        }).detach();
    }
};

}  // namespace

std::unique_ptr<Screen> makeTester(App &app)
{
    return std::unique_ptr<Screen>(new TesterScreen(app));
}

}  // namespace ml
