// Moonlight HB visual identity: space palette (space black, deep purple, luminous lilac,
// rocket red, lunar white) in a dark (default) and a light variant.
// Fixed colours (Red, Good, Warn, Bad) are constexpr; the ones that change with the theme are
// functions that read theme::activeTheme (a plain enum: no static constructor, see CLAUDE.md).
#pragma once
#include "ui/canvas.hpp"

namespace ml::ui::theme {

constexpr int W = 1920, H = 1080;
constexpr int SidebarW = 132;
constexpr int Margin = 56;
constexpr int Radius = 12;      // default card radius
constexpr int RadiusSm = 8;     // chips, buttons
constexpr int RadiusLg = 20;    // large cards, banners
constexpr int RadiusXl = 32;    // hero banners, overlays

// ── Spacing scale ─────────────────────────────────────────────────────────────
constexpr int Sp1  = 4;    // micro gap
constexpr int Sp2  = 8;    // inner padding
constexpr int Sp3  = 12;   // tight rows
constexpr int Sp4  = 16;   // standard gap
constexpr int Sp5  = 24;   // section gap
constexpr int Sp6  = 32;   // block gap
constexpr int Sp7  = 48;   // section separator
constexpr int Sp8  = 64;   // hero margin
constexpr int CardH   = 180;  // standard card height
constexpr int PosterW = 140;  // poster width
constexpr int PosterH = 210;  // poster height (3:2)
constexpr int BannerH = 320;  // wide banner height
constexpr int HeroH   = 480;  // hero banner height

// ── Space palette — fixed brand colours ───────────────────────────────────────
// Preto espacial:    #0A0A14  — fundo principal do app
// Roxo profundo:     #1A0A2E  — superfície cards/sidebar
// Lilás luminoso:    #9B6DFF  — destaques, foco, links
// Vermelho foguete:  #E8213A  — ações primárias, identidade, foguete
// Branco lunar:      #F0EEF8  — texto principal
namespace space {
    constexpr Color SpaceBlack  = rgba(10,  10,  20);    // #0A0A14 — fundo
    constexpr Color DeepPurple  = rgba(26,  10,  46);    // #1A0A2E — card/surface
    constexpr Color MidPurple   = rgba(45,  20,  80);    // #2D1450 — hover/active
    constexpr Color LilacDim    = rgba(100, 60,  180);   // #643CB4 — lilás escuro
    constexpr Color Lilac       = rgba(155, 109, 255);   // #9B6DFF — lilás luminoso
    constexpr Color LilacBright = rgba(185, 145, 255);   // #B991FF — lilás claro
    constexpr Color RocketRed   = rgba(232, 33,  58);    // #E8213A — vermelho foguete
    constexpr Color RocketRedDim= rgba(185, 25,  45);    // #B9192D — hover/pressed
    constexpr Color LunarWhite  = rgba(240, 238, 248);   // #F0EEF8 — branco lunar
    constexpr Color StarDust    = rgba(180, 170, 210);   // #B4AAD2 — texto secundário
    constexpr Color Nebula      = rgba(100, 90,  140);   // #645A8C — texto apagado
    // Gradiente do fundo: de SpaceBlack para DeepPurple (vertical, top->bottom)
}

// ── Fixed palette (does not change with the theme) ───────────────────────────
// Identity red (kept for backward compat — same visual as RocketRed)
constexpr Color Red    = rgba(232, 33,  58);    // #E8213A rocket red
constexpr Color RedDim = rgba(185, 25,  45);    // #B9192D hover/pressed
// Accent purple (new, for focus rings and links)
constexpr Color Purple     = rgba(155, 109, 255);  // #9B6DFF lilac
constexpr Color PurpleDim  = rgba(100, 60,  180);  // #643CB4 hover
constexpr Color Good       = rgba(34,  197, 94);   // status green
constexpr Color Warn       = rgba(234, 179, 8);    // warning yellow
constexpr Color Bad        = rgba(239, 68,  68);   // error (distinct from Red)

// ── Dark theme — space identity (default) ─────────────────────────────────────
namespace dark {
    constexpr Color Bg         = rgba(10,  10,  20);    // Space Black
    constexpr Color BgCard     = rgba(26,  10,  46);    // Deep Purple
    constexpr Color BgSurface  = rgba(35,  16,  60);    // mid-purple surface
    constexpr Color BgHover    = rgba(45,  20,  80);    // mid-purple hover
    constexpr Color Border     = rgba(155, 109, 255, 30);  // lilac tint border
    constexpr Color BorderFoc  = rgba(155, 109, 255, 200); // lilac focus ring
    constexpr Color BorderAccent = rgba(232, 33, 58, 160); // red accent border
    constexpr Color Text       = rgba(240, 238, 248);   // Lunar White
    constexpr Color TextSub    = rgba(180, 170, 210);   // Stardust
    constexpr Color TextMute   = rgba(100, 90,  140);   // Nebula
    constexpr Color Accent     = rgba(232, 33,  58);    // Rocket Red
    constexpr Color AccentText = rgba(255, 255, 255);
    constexpr Color Highlight  = rgba(155, 109, 255);   // Lilac highlight
    // Gradient stops for background: Bg -> BgCard, top-to-bottom
    constexpr Color BgGrad0    = rgba(10,  10,  20);    // top
    constexpr Color BgGrad1    = rgba(20,  8,   36);    // bottom
}

// ── Light theme ───────────────────────────────────────────────────────────────
namespace light {
    constexpr Color Bg         = rgba(245, 243, 255);   // near-white with purple tint
    constexpr Color BgCard     = rgba(255, 255, 255);
    constexpr Color BgSurface  = rgba(235, 232, 250);
    constexpr Color BgHover    = rgba(220, 215, 245);
    constexpr Color Border     = rgba(155, 109, 255, 40);
    constexpr Color BorderFoc  = rgba(155, 109, 255, 200);
    constexpr Color BorderAccent = rgba(232, 33, 58, 140);
    constexpr Color Text       = rgba(15,  12,  30);
    constexpr Color TextSub    = rgba(60,  50,  100);
    constexpr Color TextMute   = rgba(130, 120, 165);
    constexpr Color Accent     = rgba(232, 33,  58);
    constexpr Color AccentText = rgba(255, 255, 255);
    constexpr Color Highlight  = rgba(155, 109, 255);
    constexpr Color BgGrad0    = rgba(245, 243, 255);
    constexpr Color BgGrad1    = rgba(230, 225, 255);
}

// ── Active theme ─────────────────────────────────────────────────────────────
// Read by every component; changed in Configuracoes (App::applyTheme).
enum class ThemeMode { Dark, Light };
extern ThemeMode activeTheme;   // defined in theme.cpp

inline bool isDark()           { return activeTheme == ThemeMode::Dark; }
inline Color Bg()              { return isDark() ? dark::Bg          : light::Bg; }
inline Color BgCard()          { return isDark() ? dark::BgCard      : light::BgCard; }
inline Color BgSurface()       { return isDark() ? dark::BgSurface   : light::BgSurface; }
inline Color BgHover()         { return isDark() ? dark::BgHover     : light::BgHover; }
inline Color Border()          { return isDark() ? dark::Border      : light::Border; }
inline Color BorderFoc()       { return isDark() ? dark::BorderFoc   : light::BorderFoc; }
inline Color BorderAccent()    { return isDark() ? dark::BorderAccent: light::BorderAccent; }
inline Color Text()            { return isDark() ? dark::Text        : light::Text; }
inline Color TextSub()         { return isDark() ? dark::TextSub     : light::TextSub; }
inline Color TextMute()        { return isDark() ? dark::TextMute    : light::TextMute; }
inline Color Accent()          { return isDark() ? dark::Accent      : light::Accent; }
inline Color AccentText()      { return isDark() ? dark::AccentText  : light::AccentText; }
inline Color Highlight()       { return isDark() ? dark::Highlight   : light::Highlight; }
inline Color BgGrad0()         { return isDark() ? dark::BgGrad0     : light::BgGrad0; }
inline Color BgGrad1()         { return isDark() ? dark::BgGrad1     : light::BgGrad1; }

// Aliases (backward compat)
inline Color Line()            { return Border(); }
inline Color TextDim()         { return TextSub(); }

// ── Component helpers ─────────────────────────────────────────────────────────
// Focus ring for a card or button: use this colour + 2px stroke
inline Color FocusRing()       { return BorderFoc(); }
// Sidebar gradient start/end
inline Color SidebarBg()       { return isDark() ? rgba(16, 6, 32) : rgba(230, 225, 255); }
inline Color SidebarBgSel()    { return isDark() ? rgba(45, 20, 80) : rgba(200, 192, 240); }

// Badge colours (notification dots, counts)
constexpr Color BadgeRed  = rgba(232, 33,  58);
constexpr Color BadgePurple = rgba(155, 109, 255);

// Tagline text (configurable, default "Seu universo de entretenimento.")
// Stored here as a compile-time default; App overrides from settings.
constexpr const char* kTaglineDefault = "Seu universo de entretenimento.";

}  // namespace ml::ui::theme
