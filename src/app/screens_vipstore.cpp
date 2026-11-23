// screens_vipstore.cpp — "Apoie o Moonlight" (suporte voluntário)
// Moonlight é 100% gratuito e de código aberto. Nenhuma funcionalidade é bloqueada.
// Esta seção apresenta o projeto e oferece canais de apoio voluntário.
// C++17, no exceptions, software renderer.
#include "app/app.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

namespace ml {
namespace {

using namespace ui;

// ---- Canais de apoio voluntário -------------------------------------------
// Nenhum canal concede vantagens técnicas ou acesso exclusivo.
// Atualizar URLs quando as plataformas forem definidas.

struct SupportChannel {
    const char *name;
    const char *description;
    const char *url;         // vazio = "em breve"
    const char *platform;    // nome curto da plataforma
};

static const SupportChannel kChannels[] = {
    {
        "GitHub Sponsors",
        "Apoie diretamente os desenvolvedores pelo GitHub. "
        "Contribuicoes mensais ou unicas, sem recompensas exclusivas.",
        "",   // preencher quando disponivel
        "github.com/sponsors"
    },
    {
        "Ko-fi",
        "Plataforma de apoio por doacao unica ou mensal. "
        "Simples, sem assinaturas obrigatorias.",
        "",
        "ko-fi.com"
    },
    {
        "Open Collective",
        "Transparencia total: receitas e despesas do projeto publicadas. "
        "Ideal para contribuicoes de organizacoes.",
        "",
        "opencollective.com"
    },
};
static constexpr int kChannelCount = int(sizeof(kChannels) / sizeof(kChannels[0]));

// ---- Como contribuir além de dinheiro -------------------------------------
struct ContribWay {
    const char *title;
    const char *body;
};

static const ContribWay kContribWays[] = {
    {
        "Reporte bugs",
        "Abra uma issue no GitHub com detalhes do problema, versao do firmware "
        "e passos para reproduzir. Cada relato melhora o projeto para todos."
    },
    {
        "Contribua com codigo",
        "Pull requests sao bem-vindos. Leia o CLAUDE.md para entender a "
        "arquitetura antes de comecar."
    },
    {
        "Teste no console real",
        "Testes no PS4 fisico sao escassos. Relatar o que funciona (ou nao) "
        "com versao do firmware e modelo do console e extremamente valioso."
    },
    {
        "Divulgue o projeto",
        "Compartilhar o Moonlight com a comunidade homebrew ajuda a atrair "
        "novos colaboradores e usuarios."
    },
};
static constexpr int kContribCount = int(sizeof(kContribWays) / sizeof(kContribWays[0]));

// ---- Tabs -----------------------------------------------------------------
enum class SupportTab { About = 0, Donate = 1, Contribute = 2 };

// ---- SupportScreen --------------------------------------------------------

class SupportScreen : public Screen {
public:
    explicit SupportScreen(App &app) : Screen(app) {}
    std::string title() const override { return "Apoie o Moonlight"; }

    void onEnter() override
    {
        sel_    = 0;
        scroll_ = 0;
    }

    bool handle(uint32_t pressed) override
    {
        if (pressed & BtnL1) { switchTab(-1); return true; }
        if (pressed & BtnR1) { switchTab(+1); return true; }

        if (tab_ == SupportTab::Donate) {
            int n = kChannelCount;
            if (pressed & BtnUp)   { sel_ = (sel_ - 1 + n) % n; return true; }
            if (pressed & BtnDown) { sel_ = (sel_ + 1) % n;      return true; }
        }
        if (tab_ == SupportTab::Contribute) {
            int n = kContribCount;
            if (pressed & BtnUp)   { sel_ = (sel_ - 1 + n) % n; return true; }
            if (pressed & BtnDown) { sel_ = (sel_ + 1) % n;      return true; }
        }
        return false;
    }

    void draw(Canvas &c, Rect area) override
    {
        const char *tabs[] = {"Sobre", "Doacao", "Contribuir"};
        int tabW = area.w / 3;
        for (int i = 0; i < 3; i++) {
            bool active = int(tab_) == i;
            uint32_t bg = active ? theme::Accent() : theme::BgCard();
            c.fillRect(area.x + i * tabW, area.y, tabW, 48, bg);
            c.fillRect(area.x + i * tabW, area.y + 47, tabW, 1, theme::Border());
            uint32_t fg = active ? rgba(255,255,255,255) : theme::TextMute();
            int tw = app.fonts.bold->measure(18, tabs[i]);
            app.fonts.bold->draw(c, area.x + i * tabW + (tabW - tw) / 2,
                                 area.y + (48 - app.fonts.bold->lineHeight(18)) / 2,
                                 18, tabs[i], fg);
        }
        Rect body{area.x, area.y + 48, area.w, area.h - 48};

        switch (tab_) {
        case SupportTab::About:      drawAbout(c, body);      break;
        case SupportTab::Donate:     drawDonate(c, body);     break;
        case SupportTab::Contribute: drawContribute(c, body); break;
        }
    }

    Hints hints() const override
    {
        Hints h;
        h.push_back({BtnL1 | BtnR1, "Aba"});
        if (tab_ != SupportTab::About)
            h.push_back({BtnUp | BtnDown, "Navegar"});
        h.push_back({BtnCircle, "Voltar"});
        return h;
    }

private:
    SupportTab tab_   = SupportTab::About;
    int        sel_   = 0;
    int        scroll_= 0;

    void switchTab(int dir)
    {
        tab_ = SupportTab((int(tab_) + dir + 3) % 3);
        sel_    = 0;
        scroll_ = 0;
    }

    // ---- Sobre -------------------------------------------------------------
    void drawAbout(Canvas &c, Rect r)
    {
        int y = r.y + 28;

        // Título principal
        const char *headline = "Moonlight e 100% gratuito";
        int tw = app.fonts.bold->measure(24, headline);
        app.fonts.bold->draw(c, r.x + (r.w - tw) / 2, y, 24, headline, theme::Text());
        y += 44;

        // Subtítulo
        const char *sub = "Sempre foi, sempre sera.";
        tw = app.fonts.regular->measure(17, sub);
        app.fonts.regular->draw(c, r.x + (r.w - tw) / 2, y, 17, sub, theme::Accent());
        y += 48;

        // Parágrafo 1
        struct Line { const char *text; };
        static const Line lines[] = {
            {"O Moonlight e um centro multimidia homebrew para PS4, desenvolvido por"},
            {"voluntarios nas horas vagas. Todo o codigo e aberto (GitHub)."},
            {""},
            {"Nenhuma funcionalidade e bloqueada por pagamento. Nao existem planos"},
            {"pagos, assinaturas ou conteudo exclusivo para doadores."},
            {""},
            {"Se o Moonlight for util para voce, considere apoiar voluntariamente."},
            {"Qualquer contribuicao — codigo, testes ou doacao — ajuda o projeto"},
            {"a crescer e chegar a mais pessoas."},
        };
        for (const auto &l : lines) {
            if (l.text[0] == '\0') { y += 12; continue; }
            tw = app.fonts.regular->measure(16, l.text);
            app.fonts.regular->draw(c, r.x + (r.w - tw) / 2, y, 16, l.text, theme::TextMute());
            y += 26;
        }

        // Versão e repo
        y += 20;
        c.fillRect(r.x + 48, y, r.w - 96, 1, theme::Border());
        y += 16;
        const char *repo = "github.com/denisfug/Moonlight";
        tw = app.fonts.regular->measure(15, repo);
        app.fonts.regular->draw(c, r.x + (r.w - tw) / 2, y, 15, repo, theme::Accent());
        y += 28;
        const char *lic = "Licenca: MIT  |  Codigo aberto";
        tw = app.fonts.regular->measure(14, lic);
        app.fonts.regular->draw(c, r.x + (r.w - tw) / 2, y, 14, lic, theme::TextMute());
    }

    // ---- Doação ------------------------------------------------------------
    void drawDonate(Canvas &c, Rect r)
    {
        int y = r.y + 20;
        app.fonts.bold->draw(c, r.x + 32, y, 20, "Apoio financeiro voluntario", theme::Text());
        app.fonts.regular->draw(c, r.x + 32, y + 30, 14,
            "Nenhuma contribuicao concede vantagens tecnicas ou acesso exclusivo.",
            theme::Accent());
        y += 72;

        static constexpr int kRowH = 90;
        for (int i = 0; i < kChannelCount; i++) {
            const SupportChannel &ch = kChannels[i];
            bool focused = i == sel_;
            uint32_t bg = focused ? theme::BgHover() : theme::BgCard();
            c.fillRoundRect(r.x + 24, y, r.w - 48, kRowH - 8, 10, bg);
            if (focused) c.fillRoundRect(r.x + 24, y, 4, kRowH - 8, 2, theme::Accent());

            app.fonts.bold->draw(c, r.x + 42, y + 10, 17, ch.name, theme::Text());
            app.fonts.regular->draw(c, r.x + 42, y + 32, 13, ch.description, theme::TextMute());

            // URL ou "em breve"
            const char *urlText = ch.url[0] ? ch.url : "Em breve";
            uint32_t urlColor   = ch.url[0] ? theme::Accent() : theme::TextMute();
            int uw = app.fonts.regular->measure(13, urlText);
            app.fonts.regular->draw(c, r.x + r.w - uw - 42, y + 60, 13, urlText, urlColor);

            y += kRowH;
        }

        // Nota de rodapé
        y = r.y + r.h - 44;
        c.fillRect(r.x + 24, y, r.w - 48, 1, theme::Border());
        app.fonts.regular->draw(c, r.x + 32, y + 10, 13,
            "URLs serao ativadas quando as plataformas de apoio forem configuradas.",
            theme::TextMute());
    }

    // ---- Contribuir --------------------------------------------------------
    void drawContribute(Canvas &c, Rect r)
    {
        int y = r.y + 20;
        app.fonts.bold->draw(c, r.x + 32, y, 20, "Como contribuir com o projeto", theme::Text());
        app.fonts.regular->draw(c, r.x + 32, y + 30, 14,
            "Codigo, testes e relatos sao tao valiosos quanto doacoes financeiras.",
            theme::TextMute());
        y += 72;

        static constexpr int kRowH = 86;
        for (int i = 0; i < kContribCount; i++) {
            const ContribWay &w = kContribWays[i];
            bool focused = i == sel_;
            uint32_t bg = focused ? theme::BgHover() : theme::BgCard();
            c.fillRoundRect(r.x + 24, y, r.w - 48, kRowH - 8, 10, bg);
            if (focused) c.fillRoundRect(r.x + 24, y, 4, kRowH - 8, 2, theme::Accent());

            app.fonts.bold->draw(c, r.x + 42, y + 10, 17, w.title, theme::Text());

            // Body — trunca se necessário
            std::string body(w.body);
            if (body.size() > 110) body = body.substr(0, 107) + "...";
            app.fonts.regular->draw(c, r.x + 42, y + 34, 13, body, theme::TextMute());

            y += kRowH;
        }
    }
};

}  // namespace

// Factory — nome antigo mantido para compatibilidade com app.cpp (makeVipStore → makeSupport já renomeado)
std::unique_ptr<Screen> makeSupport(App &a) { return std::unique_ptr<Screen>(new SupportScreen(a)); }

}  // namespace ml
