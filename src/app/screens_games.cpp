// screens_games.cpp — Jogos section: Biblioteca | Online | Moonlight Lab
// C++17, no exceptions, software renderer.
#include "app/app.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

namespace ml {
namespace {

using namespace ui;

// ---- Moonlight Lab: emulator/port catalogue --------------------------------
// Statically compiled list — updated by admin JSON in future; no network at startup.

enum class CompatStatus { Compatible, Portable, Experimental, Incompatible };

struct EmulatorEntry {
    const char *id;
    const char *name;
    const char *platform;      // original platform (PSP, PS3, NES, etc.)
    const char *runOn;         // "PS4 (GoldHEN)"
    const char *description;
    const char *license;
    const char *repoUrl;
    const char *lastKnownVersion;
    CompatStatus compat;
    const char *compatNote;
};

// RESEARCH FINDINGS (november 2026):
// M33 (iHaiDeeZ/M33): PSP emulator via PPSSPP, real releases up to v1.19, GPL-2.0.
//   Uses PPSSPP's MIPS→x86-64 JIT + Mesa RADV Vulkan on PS4's AMD GPU.
//   DualShock 4 fully mapped. Status: COMPATIBLE (tested, real releases).
//
// rpcs3-port-for-ps4 (RoberixStudios): PS3 emulator, v0.40 pre-release, GPL-2.0.
//   Uses RPCS3 PPU/SPU LLVM recompilers cross-compiled for PS4 + OpenOrbis.
//   v0.39: some games reach menus but not stable gameplay (thread exhaustion).
//   v0.40: targets thread headroom, still needs console tests.
//   Status: EXPERIMENTAL (pre-release, not yet stable gameplay).
//
// PS1/PS2: No confirmed PS4 homebrew port found. DuckStation and PCSX2 are
//   desktop projects; no OpenOrbis port exists in public repos as of this research.
//
// NES/SNES/GBA: No confirmed PS4 port of RetroArch or standalone emulators
//   found in public OpenOrbis-compatible repos as of this research.
//
// Cloud gaming: PS Now / PS Plus Premium streams use Sony private APIs.
//   Stadia is shut down. Boosteroid / Xbox Cloud require browser/app not
//   available on homebrew PS4. No integration path confirmed.

static const EmulatorEntry kEntries[] = {
    {
        "m33",
        "M33 (PPSSPP)",
        "PSP",
        "PS4 (GoldHEN)",
        "Emulador de PSP para PS4 jailbroken. Baseado no PPSSPP com JIT MIPS→x86-64 "
        "e renderizacao Vulkan via Mesa RADV. Suporta XMB nativo do PSP com firmware 6.61, "
        "estados de jogo, trapaças, rebobinar (L2), RetroAchievements e DualShock 4 completo. "
        "Versão 1.19 com releases públicos testados no console.",
        "GPL-2.0",
        "https://github.com/iHaiDeeZ/M33",
        "1.19",
        CompatStatus::Compatible,
        "Testado no PS4 (GoldHEN). Releases públicos. Traga seus próprios backups."
    },
    {
        "rpcs3ps4",
        "RPCS3 Port for PS4",
        "PS3",
        "PS4 (GoldHEN)",
        "Port experimental do emulador RPCS3 de PS3 para PS4. Usa recompiladores LLVM PPU/SPU "
        "e Vulkan via Mesa RADV. v0.40 melhora uso de threads. v0.39: alguns jogos chegam ao menu "
        "mas sem gameplay estável. Suporte a 4 jogadores, RPCN online (experimental). "
        "Requer chaves para ISOs encriptados (fornecidas pelo usuário).",
        "GPL-2.0",
        "https://github.com/RoberixStudios/rpcs3-port-for-ps4",
        "0.40 (pre-release)",
        CompatStatus::Experimental,
        "Pre-release. v0.40 ainda precisa de testes no console. Desempenho varia muito por jogo."
    },
    {
        "ps1_research",
        "PS1 (Em pesquisa)",
        "PS1",
        "PS4 (GoldHEN)",
        "Nenhum port confirmado do DuckStation ou similar para PS4 homebrew encontrado "
        "nos repositórios públicos. Investigação em andamento.",
        "N/A",
        "",
        "N/A",
        CompatStatus::Experimental,
        "Sem port público confirmado para PS4+OpenOrbis. Requer investigação adicional."
    },
    {
        "ps2_research",
        "PS2 (Em pesquisa)",
        "PS2",
        "PS4 (GoldHEN)",
        "Nenhum port confirmado do PCSX2 ou similar para PS4 homebrew encontrado "
        "nos repositórios públicos com OpenOrbis. Investigação em andamento.",
        "N/A",
        "",
        "N/A",
        CompatStatus::Experimental,
        "Sem port público confirmado para PS4+OpenOrbis. Requer investigação adicional."
    },
    {
        "retro_research",
        "Plataformas Retro (NES/SNES/GBA)",
        "NES / SNES / GBA",
        "PS4 (GoldHEN)",
        "Não foi encontrado port do RetroArch ou de emuladores standalone "
        "para PS4 com OpenOrbis nos repositórios públicos verificados. "
        "RetroArch tem port para PS4 oficial (firmware <5.05) mas não homebrew/GoldHEN.",
        "N/A",
        "",
        "N/A",
        CompatStatus::Experimental,
        "Sem port público confirmado para o ambiente GoldHEN. Investigação em andamento."
    },
};
static constexpr int kEntryCount = int(sizeof(kEntries) / sizeof(kEntries[0]));

// ---- Color for compat status -----------------------------------------------
static uint32_t compatColor(CompatStatus s)
{
    switch (s) {
    case CompatStatus::Compatible:   return rgba(80, 200, 80, 255);
    case CompatStatus::Portable:     return rgba(80, 180, 240, 255);
    case CompatStatus::Experimental: return rgba(240, 180, 40, 255);
    case CompatStatus::Incompatible: return rgba(220, 60, 60, 255);
    }
    return theme::TextMute();
}
static const char *compatLabel(CompatStatus s)
{
    switch (s) {
    case CompatStatus::Compatible:   return "Compativel";
    case CompatStatus::Portable:     return "Portavel";
    case CompatStatus::Experimental: return "Experimental";
    case CompatStatus::Incompatible: return "Incompativel";
    }
    return "Desconhecido";
}

// ---- Tab IDs ---------------------------------------------------------------
enum class GamesTab { Library = 0, Online = 1, Lab = 2 };

// ---- GamesScreen -----------------------------------------------------------

class GamesScreen : public Screen {
public:
    explicit GamesScreen(App &app) : Screen(app) {}
    std::string title() const override { return "Jogos"; }

    void onEnter() override
    {
        sel_   = 0;
        scroll_= 0;
    }

    bool handle(uint32_t pressed) override
    {
        if (pressed & BtnL1) { switchTab(-1); return true; }
        if (pressed & BtnR1) { switchTab(+1); return true; }

        if (tab_ == GamesTab::Lab) {
            int n = kEntryCount;
            if (pressed & BtnUp)    { sel_ = (sel_ - 1 + n) % n; clampScroll(); return true; }
            if (pressed & BtnDown)  { sel_ = (sel_ + 1) % n;      clampScroll(); return true; }
        }
        return false;
    }

    void draw(Canvas &c, Rect area) override
    {
        // Tab bar
        const char *tabs[] = {"Biblioteca", "Online", "Moonlight Lab"};
        int tabW = area.w / 3;
        for (int i = 0; i < 3; i++) {
            bool active = int(tab_) == i;
            uint32_t bg = active ? theme::Accent() : theme::BgCard();
            c.fillRect(area.x + i * tabW, area.y, tabW, 48, bg);
            c.fillRect(area.x + i * tabW, area.y + 47, tabW, 1, theme::Border());
            uint32_t fg = active ? rgba(255,255,255,255) : theme::TextMute();
            int tw = app.fonts.bold->measure(18, tabs[i]);
            app.fonts.bold->draw(c, area.x + i * tabW + (tabW - tw) / 2,
                                 area.y + (48 - app.fonts.bold->lineHeight(18)) / 2, 18, tabs[i], fg);
        }
        Rect body{area.x, area.y + 48, area.w, area.h - 48};

        switch (tab_) {
        case GamesTab::Library: drawLibrary(c, body); break;
        case GamesTab::Online:  drawOnline(c, body);  break;
        case GamesTab::Lab:     drawLab(c, body);     break;
        }
    }

    Hints hints() const override
    {
        Hints h;
        h.push_back({BtnL1 | BtnR1, "Aba"});
        if (tab_ == GamesTab::Lab) {
            h.push_back({BtnUp | BtnDown, "Navegar"});
        }
        h.push_back({BtnCircle, "Voltar"});
        return h;
    }

private:
    GamesTab tab_   = GamesTab::Library;
    int      sel_   = 0;
    int      scroll_= 0;

    static constexpr int kRowH = 90;
    static constexpr int kVisible = 6;

    void switchTab(int dir)
    {
        int n = 3;
        tab_ = GamesTab((int(tab_) + dir + n) % n);
        sel_    = 0;
        scroll_ = 0;
    }

    void clampScroll()
    {
        if (sel_ < scroll_) scroll_ = sel_;
        if (sel_ >= scroll_ + kVisible) scroll_ = sel_ - kVisible + 1;
    }

    // --- Library tab --------------------------------------------------------
    void drawLibrary(Canvas &c, Rect r)
    {
        int y = r.y + 32;
        app.fonts.bold->draw(c, r.x + 32, y, 22, "Biblioteca de Jogos", theme::Text());
        y += 50;
        app.fonts.regular->draw(c, r.x + 32, y, 18,
            "Seus jogos instalados no PS4 aparecem aqui.", theme::TextMute());
        y += 36;
        app.fonts.regular->draw(c, r.x + 32, y, 16,
            "Homebrew PKG instalados via GoldHEN sao listados automaticamente.", theme::TextMute());
        y += 60;
        // Placeholder: future integration reads /data/ and PKG list
        drawPlaceholderCard(c, {r.x + 32, y, r.w - 64, 80},
            "Integracao com PKG instalados em desenvolvimento",
            "Requer leitura da lista de apps do sistema via sceSystemServiceGetAppStatus");
    }

    // --- Online tab ---------------------------------------------------------
    void drawOnline(Canvas &c, Rect r)
    {
        int y = r.y + 32;
        app.fonts.bold->draw(c, r.x + 32, y, 22, "Moonlight Online", theme::Text());
        y += 50;

        struct OnlineItem {
            const char *name;
            const char *desc;
            const char *status;
            uint32_t statusColor;
        };
        static const OnlineItem items[] = {
            {"Jogos Nativos Online",
             "Jogos instalados com modo multiplayer. Requerem PSN.",
             "Via PSN (nao disponivel em homebrew sem spoofing)",
             rgba(220,60,60,255)},
            {"Cloud Gaming — PS Plus Premium",
             "Streaming de PS3/PS4 via servidores Sony.",
             "Requer conta PSN ativa e APIs Sony privadas (indisponivel em homebrew)",
             rgba(220,60,60,255)},
            {"Boosteroid / Xbox Cloud",
             "Plataformas de cloud gaming de terceiros.",
             "Requerem browser ou app dedicado — sem suporte homebrew PS4 confirmado",
             rgba(240,180,40,255)},
            {"RPCN (via RPCS3 Port)",
             "Servidor de emulacao online para jogos PS3 (experimental).",
             "Experimental — requer RPCS3 Port funcionando (ver Moonlight Lab)",
             rgba(240,180,40,255)},
        };

        for (const auto &item : items) {
            c.fillRoundRect(r.x + 32, y, r.w - 64, 90, 8, theme::BgCard());
            app.fonts.bold->draw(c, r.x + 52, y + 14, 17, item.name, theme::Text());
            app.fonts.regular->draw(c, r.x + 52, y + 36, 14, item.desc, theme::TextMute());
            int sw = app.fonts.regular->measure(13, item.status);
            c.fillRoundRect(r.w - sw - 44, y + 58, sw + 24, 22, 4, withAlpha(item.statusColor, 40));
            app.fonts.regular->draw(c, r.w - sw - 32, y + 61, 13, item.status, item.statusColor);
            y += 100;
            if (y + 90 > r.y + r.h - 16) break;
        }
    }

    // --- Moonlight Lab tab --------------------------------------------------
    void drawLab(Canvas &c, Rect r)
    {
        int headerY = r.y + 20;
        app.fonts.bold->draw(c, r.x + 32, headerY, 22, "Moonlight Lab", theme::Text());
        app.fonts.regular->draw(c, r.x + 32, headerY + 32, 15,
            "Emuladores e ports investigados — compatibilidade baseada em evidencias reais",
            theme::TextMute());

        int y = r.y + 90;
        for (int i = scroll_; i < kEntryCount && y + kRowH <= r.y + r.h - 16; i++) {
            const EmulatorEntry &e = kEntries[i];
            bool focused = i == sel_;
            uint32_t bg = focused ? theme::BgHover() : theme::BgCard();
            c.fillRoundRect(r.x + 24, y, r.w - 48, kRowH - 8, 10, bg);
            if (focused) c.fillRoundRect(r.x + 24, y, 4, kRowH - 8, 2, theme::Accent());

            // Name + platform
            app.fonts.bold->draw(c, r.x + 42, y + 10, 17, e.name, theme::Text());
            app.fonts.regular->draw(c, r.x + 42, y + 32, 13, e.platform, theme::TextMute());

            // Compat badge
            uint32_t cc = compatColor(e.compat);
            const char *cl = compatLabel(e.compat);
            int bw = app.fonts.regular->measure(13, cl) + 20;
            c.fillRoundRect(r.x + r.w - bw - 36, y + 10, bw, 24, 4, withAlpha(cc, 50));
            app.fonts.regular->draw(c, r.x + r.w - bw - 26, y + 15, 13, cl, cc);

            // Description (truncated)
            std::string desc(e.description);
            if (focused && desc.size() > 120) desc = desc.substr(0, 117) + "...";
            else if (!focused && desc.size() > 80) desc = desc.substr(0, 77) + "...";
            app.fonts.regular->draw(c, r.x + 42, y + 54, 13, desc, theme::TextMute());

            y += kRowH;
        }

        // Detail panel for focused entry
        if (sel_ >= 0 && sel_ < kEntryCount) {
            const EmulatorEntry &e = kEntries[sel_];
            int panelY = r.y + r.h - 110;
            c.fillRect(r.x, panelY, r.w, 110, theme::BgCard());
            c.fillRect(r.x, panelY, r.w, 1, theme::Border());
            app.fonts.bold->draw(c, r.x + 32, panelY + 10, 14, e.compatNote, compatColor(e.compat));
            if (e.repoUrl[0]) {
                std::string repo = std::string("Repo: ") + e.repoUrl;
                app.fonts.regular->draw(c, r.x + 32, panelY + 32, 13, repo, theme::Accent());
            }
            std::string lic = std::string("Licenca: ") + e.license + "   Versao conhecida: " + e.lastKnownVersion;
            app.fonts.regular->draw(c, r.x + 32, panelY + 56, 13, lic, theme::TextMute());
        }
    }

    void drawPlaceholderCard(Canvas &c, Rect r, const char *title, const char *note)
    {
        c.fillRoundRect(r.x, r.y, r.w, r.h, 8, theme::BgCard());
        c.fillRoundRect(r.x, r.y, 4, r.h, 2, theme::Accent());
        app.fonts.bold->draw(c, r.x + 20, r.y + 14, 15, title, theme::Text());
        app.fonts.regular->draw(c, r.x + 20, r.y + 38, 13, note, theme::TextMute());
    }
};

}  // namespace

std::unique_ptr<Screen> makeGames(App &a) { return std::unique_ptr<Screen>(new GamesScreen(a)); }

}  // namespace ml
