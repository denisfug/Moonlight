// screens_vipstore.cpp — VIP Store: curated homebrew catalog + subscription model
// C++17, no exceptions, software renderer.
#include "app/app.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

namespace ml {
namespace {

using namespace ui;

// ---- Subscription tiers ---------------------------------------------------
struct Tier {
    const char *name;
    const char *price;        // display only — real billing via PSN or external
    const char *description;
    const char *features[6];  // null-terminated list
    bool        highlight;    // golden border
};

static const Tier kTiers[] = {
    {
        "Free",
        "Gratuito",
        "Acesso basico ao Moonlight",
        {
            "Reproducao local MP3",
            "Fontes Stremio (PC)",
            "Radio Browser",
            "Favoritos e historico",
            nullptr, nullptr
        },
        false
    },
    {
        "VIP",
        "R$ 9,90 / mes",
        "Experiencia completa",
        {
            "Tudo do Free",
            "Fontes premium verificadas",
            "Recomendacoes inteligentes",
            "Suporte prioritario",
            "Sem anuncios",
            nullptr
        },
        true
    },
    {
        "VIP+",
        "R$ 19,90 / mes",
        "Para colecionadores",
        {
            "Tudo do VIP",
            "Catalogo de jogos curado",
            "Moonlight Lab priority access",
            "Beta features",
            "Badge exclusivo",
            "Acesso antecipado"
        },
        false
    },
};
static constexpr int kTierCount = int(sizeof(kTiers) / sizeof(kTiers[0]));

// ---- Curated catalog entries ----------------------------------------------
struct CatalogEntry {
    const char *title;
    const char *type;    // "Jogo", "App", "Plugin"
    const char *author;
    const char *desc;
    bool        vipRequired;
};

static const CatalogEntry kCatalog[] = {
    {"M33 (PPSSPP)",      "Plugin", "iHaiDeeZ",        "Emulador de PSP para PS4. Ver Moonlight Lab.", false},
    {"RPCS3 Port",        "Plugin", "RoberixStudios",   "Emulador experimental de PS3.",                 true},
    {"Moonlight Remote",  "App",    "Moonlight Team",   "Controle pelo celular (em desenvolvimento).",   false},
    {"Themes Pack 1",     "App",    "Moonlight Team",   "Temas visuais extras para o Moonlight.",        true},
    {"Enhanced Radio",    "Plugin", "Moonlight Team",   "Mais filtros e qualidade para o Radio Browser.", true},
};
static constexpr int kCatalogCount = int(sizeof(kCatalog) / sizeof(kCatalog[0]));

// ---- Tabs -----------------------------------------------------------------
enum class StoreTab { Plans = 0, Catalog = 1, Account = 2 };

// ---- VipStoreScreen -------------------------------------------------------

class VipStoreScreen : public Screen {
public:
    explicit VipStoreScreen(App &app) : Screen(app) {}
    std::string title() const override { return "VIP Store"; }

    void onEnter() override
    {
        sel_    = 0;
        scroll_ = 0;
    }

    bool handle(uint32_t pressed) override
    {
        if (pressed & BtnL1) { switchTab(-1); return true; }
        if (pressed & BtnR1) { switchTab(+1); return true; }

        if (tab_ == StoreTab::Plans) {
            if (pressed & BtnLeft)  { sel_ = (sel_ - 1 + kTierCount) % kTierCount; return true; }
            if (pressed & BtnRight) { sel_ = (sel_ + 1) % kTierCount;              return true; }
        }
        if (tab_ == StoreTab::Catalog) {
            if (pressed & BtnUp)   { sel_ = (sel_ - 1 + kCatalogCount) % kCatalogCount; return true; }
            if (pressed & BtnDown) { sel_ = (sel_ + 1) % kCatalogCount;                 return true; }
        }
        return false;
    }

    void draw(Canvas &c, Rect area) override
    {
        // Tab bar
        const char *tabs[] = {"Planos", "Catalogo", "Minha Conta"};
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
        case StoreTab::Plans:   drawPlans(c, body);   break;
        case StoreTab::Catalog: drawCatalog(c, body); break;
        case StoreTab::Account: drawAccount(c, body); break;
        }
    }

    Hints hints() const override
    {
        Hints h;
        h.push_back({BtnL1 | BtnR1, "Aba"});
        if (tab_ == StoreTab::Plans)
            h.push_back({BtnLeft | BtnRight, "Plano"});
        if (tab_ == StoreTab::Catalog)
            h.push_back({BtnUp | BtnDown, "Navegar"});
        h.push_back({BtnCircle, "Voltar"});
        return h;
    }

private:
    StoreTab tab_    = StoreTab::Plans;
    int      sel_    = 0;
    int      scroll_ = 0;

    static constexpr int kCardW  = 280;
    static constexpr int kCardH  = 320;

    void switchTab(int dir)
    {
        tab_ = StoreTab((int(tab_) + dir + 3) % 3);
        sel_    = 0;
        scroll_ = 0;
    }

    // --- Plans tab ----------------------------------------------------------
    void drawPlans(Canvas &c, Rect r)
    {
        // Header
        int y = r.y + 20;
        {
            int tw = app.fonts.bold->measure(26, "VIP Store");
            app.fonts.bold->draw(c, r.x + (r.w - tw) / 2, y, 26, "VIP Store", theme::Text());
        }
        y += 40;
        {
            const char *sub = "Escolha o plano ideal para sua experiencia Moonlight";
            int tw = app.fonts.regular->measure(16, sub);
            app.fonts.regular->draw(c, r.x + (r.w - tw) / 2, y, 16, sub, theme::TextMute());
        }
        y += 50;

        // Tier cards side by side
        int totalW   = kTierCount * kCardW + (kTierCount - 1) * 20;
        int startX   = r.x + (r.w - totalW) / 2;

        for (int i = 0; i < kTierCount; i++) {
            const Tier &t  = kTiers[i];
            bool   focused = i == sel_;
            int    cx      = startX + i * (kCardW + 20);

            // Card background
            uint32_t borderCol = t.highlight ? rgba(255, 200, 50, 255)
                                : focused     ? theme::Accent()
                                             : theme::Border();
            c.fillRoundRect(cx - 2, y - 2, kCardW + 4, kCardH + 4, 12, borderCol);
            c.fillRoundRect(cx, y, kCardW, kCardH, 10, theme::BgCard());

            // Name
            int tw = app.fonts.bold->measure(22, t.name);
            app.fonts.bold->draw(c, cx + (kCardW - tw) / 2, y + 20, 22, t.name, theme::Text());

            // Price
            tw = app.fonts.bold->measure(18, t.price);
            uint32_t pCol = t.highlight ? rgba(255, 200, 50, 255) : theme::Accent();
            app.fonts.bold->draw(c, cx + (kCardW - tw) / 2, y + 50, 18, t.price, pCol);

            // Description
            tw = app.fonts.regular->measure(14, t.description);
            app.fonts.regular->draw(c, cx + (kCardW - tw) / 2, y + 78, 14, t.description, theme::TextMute());

            // Divider
            c.fillRect(cx + 20, y + 102, kCardW - 40, 1, theme::Border());

            // Features
            int fy = y + 112;
            for (int f = 0; f < 6 && t.features[f]; f++) {
                // Checkmark
                c.fillRect(cx + 24, fy + 6, 8, 8, theme::Accent());
                app.fonts.regular->draw(c, cx + 40, fy, 14, t.features[f], theme::Text());
                fy += 24;
            }

            // CTA button
            int btnY = y + kCardH - 50;
            uint32_t btnBg = t.highlight ? rgba(255, 200, 50, 180) : withAlpha(theme::Accent(), 160);
            if (focused) btnBg = t.highlight ? rgba(255, 200, 50, 255) : theme::Accent();
            c.fillRoundRect(cx + 24, btnY, kCardW - 48, 36, 8, btnBg);
            const char *cta = (i == 0) ? "Plano atual" : "Assinar";
            tw = app.fonts.bold->measure(15, cta);
            app.fonts.bold->draw(c, cx + 24 + (kCardW - 48 - tw) / 2, btnY + 10, 15, cta,
                                 rgba(255, 255, 255, 255));
        }

        // Note
        const char *note = "* Pagamento via PSN ou metodo externo. Moonlight nao processa cartoes diretamente.";
        app.fonts.regular->draw(c, r.x + 32, r.y + r.h - 30, 12, note, theme::TextMute());
    }

    // --- Catalog tab --------------------------------------------------------
    void drawCatalog(Canvas &c, Rect r)
    {
        int y = r.y + 20;
        app.fonts.bold->draw(c, r.x + 32, y, 20, "Catalogo Curado", theme::Text());
        app.fonts.regular->draw(c, r.x + 32, y + 28, 14,
            "Apps, plugins e jogos verificados pela equipe Moonlight",
            theme::TextMute());
        y += 72;

        static constexpr int kRowH = 80;
        for (int i = 0; i < kCatalogCount && y + kRowH <= r.y + r.h - 16; i++) {
            const CatalogEntry &e = kCatalog[i];
            bool focused = i == sel_;
            uint32_t bg = focused ? theme::BgHover() : theme::BgCard();
            c.fillRoundRect(r.x + 24, y, r.w - 48, kRowH - 8, 10, bg);
            if (focused) c.fillRoundRect(r.x + 24, y, 4, kRowH - 8, 2, theme::Accent());

            // Type badge
            uint32_t typeBg = rgba(60, 100, 200, 180);
            int bw = app.fonts.regular->measure(12, e.type) + 16;
            c.fillRoundRect(r.x + 42, y + 10, bw, 20, 4, typeBg);
            app.fonts.regular->draw(c, r.x + 50, y + 13, 12, e.type, rgba(255,255,255,255));

            app.fonts.bold->draw(c, r.x + 42 + bw + 10, y + 11, 16, e.title, theme::Text());
            app.fonts.regular->draw(c, r.x + 42, y + 36, 13, e.desc, theme::TextMute());
            app.fonts.regular->draw(c, r.x + 42, y + 54, 12, e.author, theme::TextMute());

            if (e.vipRequired) {
                const char *vipTag = "VIP";
                int vw = app.fonts.bold->measure(12, vipTag) + 14;
                c.fillRoundRect(r.x + r.w - vw - 36, y + 10, vw, 22, 4,
                                withAlpha(rgba(255,200,50,255), 60));
                app.fonts.bold->draw(c, r.x + r.w - vw - 29, y + 14, 12, vipTag,
                                     rgba(255,200,50,255));
            }
            y += kRowH;
        }
    }

    // --- Account tab --------------------------------------------------------
    void drawAccount(Canvas &c, Rect r)
    {
        int y = r.y + 32;
        app.fonts.bold->draw(c, r.x + 32, y, 20, "Minha Conta", theme::Text());
        y += 50;

        // Current plan
        c.fillRoundRect(r.x + 24, y, r.w - 48, 80, 10, theme::BgCard());
        c.fillRoundRect(r.x + 24, y, 4, 80, 2, theme::Accent());
        app.fonts.bold->draw(c, r.x + 42, y + 14, 16, "Plano atual", theme::TextMute());
        app.fonts.bold->draw(c, r.x + 42, y + 36, 20, "Free", theme::Text());
        app.fonts.regular->draw(c, r.x + 42, y + 58, 13, "Sem assinatura ativa", theme::TextMute());
        y += 96;

        // Info boxes
        const struct { const char *title; const char *body; } infos[] = {
            {
                "Como assinar",
                "Acesse a aba Planos, escolha VIP ou VIP+ e use seu metodo de pagamento preferido."
            },
            {
                "Seguranca",
                "O Moonlight nao armazena dados de pagamento. Todas as transacoes sao processadas "
                "pelo provedor externo."
            },
            {
                "Cancelamento",
                "Cancele a qualquer momento pelo mesmo canal de pagamento. Sem multas."
            },
        };
        for (const auto &info : infos) {
            if (y + 80 > r.y + r.h - 16) break;
            c.fillRoundRect(r.x + 24, y, r.w - 48, 76, 8, theme::BgCard());
            app.fonts.bold->draw(c, r.x + 42, y + 10, 15, info.title, theme::Text());
            // Wrap at ~100 chars
            std::string body(info.body);
            int bw = r.w - 90;
            int fw = app.fonts.regular->measure(13, body.c_str());
            if (fw <= bw) {
                app.fonts.regular->draw(c, r.x + 42, y + 32, 13, body, theme::TextMute());
            } else {
                size_t mid = body.size() / 2;
                size_t sp  = body.rfind(' ', mid);
                if (sp == std::string::npos) sp = mid;
                app.fonts.regular->draw(c, r.x + 42, y + 32, 13, body.substr(0, sp), theme::TextMute());
                app.fonts.regular->draw(c, r.x + 42, y + 50, 13, body.substr(sp + 1), theme::TextMute());
            }
            y += 86;
        }
    }
};

}  // namespace

std::unique_ptr<Screen> makeVipStore(App &a) { return std::unique_ptr<Screen>(new VipStoreScreen(a)); }

}  // namespace ml
