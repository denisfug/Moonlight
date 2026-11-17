// Moonlight HB visual identity: black / white / red, in a dark (default) and a light variant.
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

// ── Fixed palette (does not change with the theme) ───────────────────────────
constexpr Color Red    = rgba(220, 38,  38);    // #DC2626 identity
constexpr Color RedDim = rgba(185, 28,  28);    // #B91C1C hover/pressed
constexpr Color Good   = rgba(34,  197, 94);    // status green
constexpr Color Warn   = rgba(234, 179, 8);     // warning yellow
constexpr Color Bad    = rgba(239, 68,  68);    // error (distinct from Red)

// ── Dark theme (default) ─────────────────────────────────────────────────────
namespace dark {
    constexpr Color Bg         = rgba(0,   0,   0);
    constexpr Color BgCard     = rgba(18,  18,  18);
    constexpr Color BgSurface  = rgba(28,  28,  28);
    constexpr Color BgHover    = rgba(40,  40,  40);
    constexpr Color Border     = rgba(255, 255, 255, 20);
    constexpr Color BorderFoc  = rgba(220, 38,  38, 180);
    constexpr Color Text       = rgba(255, 255, 255);
    constexpr Color TextSub    = rgba(179, 179, 179);
    constexpr Color TextMute   = rgba(115, 115, 115);
    constexpr Color Accent     = rgba(220, 38,  38);
    constexpr Color AccentText = rgba(255, 255, 255);
}

// ── Light theme ──────────────────────────────────────────────────────────────
namespace light {
    constexpr Color Bg         = rgba(255, 255, 255);
    constexpr Color BgCard     = rgba(245, 245, 245);
    constexpr Color BgSurface  = rgba(235, 235, 235);
    constexpr Color BgHover    = rgba(220, 220, 220);
    constexpr Color Border     = rgba(0,   0,   0,  20);
    constexpr Color BorderFoc  = rgba(220, 38,  38, 180);
    constexpr Color Text       = rgba(0,   0,   0);
    constexpr Color TextSub    = rgba(80,  80,  80);
    constexpr Color TextMute   = rgba(140, 140, 140);
    constexpr Color Accent     = rgba(220, 38,  38);
    constexpr Color AccentText = rgba(255, 255, 255);
}

// ── Active theme ─────────────────────────────────────────────────────────────
// Read by every component; changed in Configuracoes (App::applyTheme).
enum class ThemeMode { Dark, Light };
extern ThemeMode activeTheme;   // defined in theme.cpp

inline bool isDark()      { return activeTheme == ThemeMode::Dark; }
inline Color Bg()         { return isDark() ? dark::Bg         : light::Bg; }
inline Color BgCard()     { return isDark() ? dark::BgCard     : light::BgCard; }
inline Color BgSurface()  { return isDark() ? dark::BgSurface  : light::BgSurface; }
inline Color BgHover()    { return isDark() ? dark::BgHover    : light::BgHover; }
inline Color Border()     { return isDark() ? dark::Border     : light::Border; }
inline Color BorderFoc()  { return isDark() ? dark::BorderFoc  : light::BorderFoc; }
inline Color Text()       { return isDark() ? dark::Text       : light::Text; }
inline Color TextSub()    { return isDark() ? dark::TextSub    : light::TextSub; }
inline Color TextMute()   { return isDark() ? dark::TextMute   : light::TextMute; }
inline Color Accent()     { return isDark() ? dark::Accent     : light::Accent; }
inline Color AccentText() { return isDark() ? dark::AccentText : light::AccentText; }

// Aliases
inline Color Line()       { return Border(); }
inline Color TextDim()    { return TextSub(); }

}  // namespace ml::ui::theme
