// Moonlight HB visual identity: deep night background, violet -> blue accents.
#pragma once
#include "ui/canvas.hpp"

namespace ml::ui::theme {

constexpr int W = 1920, H = 1080;
constexpr int SidebarW = 132;   // narrow floating icon pill + its margins (was a 300px solid rail)
constexpr int Margin = 56;

constexpr Color Bg = rgba(8, 7, 15);        // #08070f, the design's deep night
constexpr Color BgTop = rgba(20, 16, 46);   // indigo lift at the top of the page gradient
constexpr Color Sidebar = rgba(10, 9, 20);  // used as the cut-out colour inside the moon logo
// Glass: translucent white panels over the night background (faked, no real blur on the PS4 CPU).
constexpr Color Surface = rgba(255, 255, 255, 15);
constexpr Color SurfaceHi = rgba(255, 255, 255, 30);
constexpr Color Line = rgba(255, 255, 255, 26);
constexpr Color Text = rgba(243, 241, 255);
constexpr Color TextDim = rgba(185, 179, 214);
constexpr Color TextMute = rgba(140, 134, 170);
constexpr Color Violet = rgba(139, 108, 255);   // #8b6cff
constexpr Color Indigo = rgba(110, 120, 255);
constexpr Color Blue = rgba(79, 140, 255);      // #4f8cff
constexpr Color Glow = rgba(170, 150, 255);
constexpr Color Good = rgba(74, 222, 128);
constexpr Color Warn = rgba(245, 197, 66);
constexpr Color Bad = rgba(248, 113, 113);

}  // namespace ml::ui::theme
