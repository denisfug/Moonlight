// Moonlight HB visual identity: deep night background, violet -> blue accents.
#pragma once
#include "ui/canvas.hpp"

namespace ml::ui::theme {

constexpr int W = 1920, H = 1080;
constexpr int SidebarW = 300;
constexpr int Margin = 56;

constexpr Color Bg = rgba(6, 7, 16);
constexpr Color BgTop = rgba(14, 12, 34);
constexpr Color Sidebar = rgba(10, 11, 24);
constexpr Color Surface = rgba(20, 21, 42);
constexpr Color SurfaceHi = rgba(30, 31, 60);
constexpr Color Line = rgba(255, 255, 255, 22);
constexpr Color Text = rgba(238, 236, 255);
constexpr Color TextDim = rgba(160, 158, 196);
constexpr Color TextMute = rgba(108, 106, 146);
constexpr Color Violet = rgba(139, 92, 246);
constexpr Color Indigo = rgba(99, 102, 241);
constexpr Color Blue = rgba(56, 152, 255);
constexpr Color Glow = rgba(150, 120, 255);
constexpr Color Good = rgba(74, 222, 128);
constexpr Color Warn = rgba(251, 191, 36);
constexpr Color Bad = rgba(248, 113, 113);

}  // namespace ml::ui::theme
