// Reusable drawing pieces: generated cover art, line icons, controller button glyphs,
// progress bars and the on-screen keyboard.
#pragma once
#include <string>
#include <utility>
#include <vector>

#include "ui/canvas.hpp"
#include "ui/font.hpp"

namespace ml::ui {

enum class Icon { Home, Music, Film, Series, Live, Library, Sources, Settings, Search, Play, Pause, Heart,
                  HeartFilled, Shuffle, Repeat, Next, Prev, Plus, Volume, Info, Warning };

struct Fonts {
    Font *regular = nullptr;
    Font *bold = nullptr;
};

uint32_t hashStr(const std::string &s);

// Original procedural artwork used when a title has no poster image (demo content, offline).
void drawGeneratedArt(Canvas &c, Fonts &f, Rect r, int radius, const std::string &seed, const std::string &title,
                      bool showTitle);
void drawIcon(Canvas &c, Icon icon, float cx, float cy, float size, Color color);
// Controller glyph for a button (cross/circle/square/triangle shapes, L1/R1/... pills). Returns width.
int drawButtonGlyph(Canvas &c, Fonts &f, uint32_t button, int x, int y, int size);
// Hint bar: "glyph label" pairs, laid out right-to-left ending at xRight.
void drawHints(Canvas &c, Fonts &f, const std::vector<std::pair<uint32_t, std::string>> &hints, int xRight, int y);
void drawProgress(Canvas &c, int x, int y, int w, int h, double frac, Color fg, Color bg);
void drawFocusRing(Canvas &c, Rect r, int radius);
std::string formatTime(int64_t ms);

// D-pad driven keyboard for search and URLs.
class Keyboard {
public:
    enum class Result { Editing, Done, Cancelled };
    void open(std::string title, std::string initial, bool urlMode);
    bool active() const { return active_; }
    Result handle(uint32_t pressed);
    void draw(Canvas &c, Fonts &f);
    const std::string &text() const { return text_; }
    const std::string &title() const { return title_; }
    void close() { active_ = false; }

private:
    bool active_ = false, urlMode_ = false, upper_ = false;
    std::string title_, text_;
    int row_ = 0, col_ = 0;
    std::vector<std::vector<std::string>> rows() const;
};

}  // namespace ml::ui
