// Animated intro vignette (Frente E).
// A fullscreen Screen pushed onto the stack at startup; exits after ~6 s or on Skip.
// Draws everything with Canvas software primitives — no external assets loaded.
//
// Timeline:
//   0.0–1.0 s   Phase 1  Space black + glowing purple point + converging particles
//   1.0–2.5 s   Phase 2  Luminous lines drawing the crescent moon
//   2.5–4.0 s   Phase 3  Red shapes forming the rocket
//   4.0–5.0 s   Phase 4  Full logo revealed (moon + rocket glowing)
//   5.0–6.0 s   Phase 5  Tagline fades in; screen holds briefly then exits
//   >6.0 s      Auto-pop

#include "app/app.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

#include <cmath>
#include <cstdlib>   // rand / srand

namespace ml {

using namespace ui;

namespace {

// ── helpers ──────────────────────────────────────────────────────────────────

static inline float clamp01(float v) { return v < 0.f ? 0.f : v > 1.f ? 1.f : v; }
static inline float smoothstep(float t) { t = clamp01(t); return t * t * (3.f - 2.f * t); }

// easeInOut cubic
static inline float ease(float t) { t = clamp01(t); return t < 0.5f ? 4*t*t*t : 1-(-2*t+2)*(-2*t+2)*(-2*t+2)/2; }

static uint32_t withA(uint32_t col, int alpha)
{
    return (col & 0x00FFFFFFu) | (uint32_t(alpha & 0xFF) << 24);
}

// ── Particle ─────────────────────────────────────────────────────────────────

struct Particle {
    float x, y;        // current position (0..1 normalised to screen)
    float vx, vy;      // velocity (per second, normalised)
    float life;        // 0..1
    float size;
    uint32_t color;
};

// ── IntroScreen ──────────────────────────────────────────────────────────────

class IntroScreen : public Screen {
public:
    explicit IntroScreen(App &a) : Screen(a)
    {
        initParticles();
    }

    std::string title() const override { return ""; }
    bool fullscreen() const override { return true; }
    bool animating() const override { return t_ < kDuration; }
    bool busy() const override { return false; }

    Hints hints() const override
    {
        return {{BtnCross, "Pular"}, {BtnCircle, "Pular"}};
    }

    void onEnter() override { t_ = 0.f; }

    bool handle(uint32_t pressed) override
    {
        if (pressed & (BtnCross | BtnCircle | BtnTriangle)) {
            app.pop();
            return true;
        }
        return false;
    }

    void update(float dt) override
    {
        if (t_ >= kDuration) {
            app.pop();
            return;
        }
        t_ += dt;
        updateParticles(dt);
    }

    void draw(Canvas &c, Rect /*area*/) override
    {
        const int W = theme::W;
        const int H = theme::H;

        // ── Background ──────────────────────────────────────────────────────
        // Deep space black, always
        c.fillRect(0, 0, W, H, theme::space::SpaceBlack);

        // Phase-dependent nebula glow
        float nebAlpha = 0.f;
        if (t_ < 1.f)      nebAlpha = smoothstep(t_);
        else if (t_ < 5.f) nebAlpha = 1.f;
        else               nebAlpha = smoothstep((6.f - t_));

        if (nebAlpha > 0.01f) {
            // Large soft purple cloud at centre-right
            uint32_t neb = withA(theme::space::DeepPurple, int(80 * nebAlpha));
            c.fillCircle(float(W) * 0.72f, float(H) * 0.38f, 420.f, neb);
            uint32_t neb2 = withA(theme::space::Lilac, int(18 * nebAlpha));
            c.fillCircle(float(W) * 0.5f, float(H) * 0.5f, 300.f, neb2);
        }

        // ── Particles ───────────────────────────────────────────────────────
        drawParticles(c, W, H);

        // ── Phase 1 : luminous purple point ─────────────────────────────────
        if (t_ < 1.0f) {
            float a = smoothstep(t_ * 3.f);           // fade in fast
            float pulse = 1.f + 0.15f * std::sin(t_ * 8.f);
            int r = int(14.f * pulse);
            uint32_t col = withA(theme::space::Lilac, int(220 * a));
            uint32_t glow = withA(theme::space::Lilac, int(40 * a));
            int cx = W / 2, cy = H / 2;
            c.fillCircle(float(cx), float(cy), float(r) * 3.f, glow);
            c.fillCircle(float(cx), float(cy), float(r), col);
        }

        // ── Phase 2 : crescent moon drawing ─────────────────────────────────
        if (t_ >= 1.0f && t_ < 4.5f) {
            float prog = clamp01((t_ - 1.0f) / 1.5f);  // 0→1 over 1–2.5s
            float moonAlpha = t_ < 2.5f ? ease(prog) : 1.f;
            if (t_ > 4.0f) moonAlpha = smoothstep((4.5f - t_) / 0.5f);
            drawMoon(c, W, H, moonAlpha);
        }

        // ── Phase 3 : rocket forming ─────────────────────────────────────────
        if (t_ >= 2.5f && t_ < 5.0f) {
            float prog = clamp01((t_ - 2.5f) / 1.5f);
            float rktAlpha = ease(prog);
            if (t_ > 4.5f) rktAlpha = smoothstep((5.0f - t_) / 0.5f);
            drawRocket(c, W, H, rktAlpha);
        }

        // ── Phase 4 : full logo ─────────────────────────────────────────────
        if (t_ >= 4.0f && t_ < 6.0f) {
            float prog = clamp01((t_ - 4.0f) / 0.8f);
            float logoAlpha = ease(prog);
            drawFullLogo(c, W, H, logoAlpha);
        }

        // ── Phase 5 : tagline ───────────────────────────────────────────────
        if (t_ >= 5.0f) {
            float prog = clamp01((t_ - 5.0f) / 0.7f);
            float tagAlpha = ease(prog);
            drawTagline(c, W, H, tagAlpha);
        }

        // ── Skip hint ───────────────────────────────────────────────────────
        {
            float hintAlpha = 1.f;
            if (t_ < 0.5f) hintAlpha = t_ / 0.5f;
            if (t_ > 5.5f) hintAlpha = (6.f - t_) / 0.5f;
            uint32_t hintCol = withA(theme::TextSub(), int(140 * clamp01(hintAlpha)));
            const char *hint = "Pressione X para pular";
            if (app.fonts.regular) {
                int tw = app.fonts.regular->measure(20, hint);
                app.fonts.regular->draw(c, W - tw - 32, H - 36, 20, hint, hintCol);
            }
        }
    }

private:
    static constexpr float kDuration = 6.2f;   // seconds before auto-pop
    static constexpr int kParticleCount = 60;

    float t_ = 0.f;
    Particle parts_[kParticleCount];

    void initParticles()
    {
        // Seed with a fixed value so the pattern is deterministic (no static-ctor risk)
        unsigned seed = 42;
        auto rnd = [&]() -> float {
            seed = seed * 1664525u + 1013904223u;
            return float(seed >> 1) / float(0x7FFFFFFFu);
        };
        for (int i = 0; i < kParticleCount; i++) {
            float angle = rnd() * 6.283f;
            float dist = 0.3f + rnd() * 0.5f;   // start near edges
            parts_[i].x = 0.5f + std::cos(angle) * dist;
            parts_[i].y = 0.5f + std::sin(angle) * dist * 0.7f;
            // velocity toward centre (slow)
            parts_[i].vx = -(parts_[i].x - 0.5f) * (0.03f + rnd() * 0.04f);
            parts_[i].vy = -(parts_[i].y - 0.5f) * (0.03f + rnd() * 0.04f);
            parts_[i].life = rnd();
            parts_[i].size = 1.5f + rnd() * 2.5f;
            // white/lilac stars
            parts_[i].color = (rnd() > 0.6f) ? theme::space::Lilac : theme::space::LunarWhite;
        }
    }

    void updateParticles(float dt)
    {
        unsigned seed = unsigned(t_ * 100000.f) ^ 0xDEADBEEFu;
        auto rnd = [&]() -> float {
            seed = seed * 1664525u + 1013904223u;
            return float(seed >> 1) / float(0x7FFFFFFFu);
        };
        for (int i = 0; i < kParticleCount; i++) {
            Particle &p = parts_[i];
            p.x += p.vx * dt;
            p.y += p.vy * dt;
            p.life -= dt * 0.08f;
            if (p.life <= 0.f) {
                // respawn near edge
                float angle = rnd() * 6.283f;
                float dist = 0.35f + rnd() * 0.45f;
                p.x = 0.5f + std::cos(angle) * dist;
                p.y = 0.5f + std::sin(angle) * dist * 0.7f;
                p.vx = -(p.x - 0.5f) * (0.02f + rnd() * 0.04f);
                p.vy = -(p.y - 0.5f) * (0.02f + rnd() * 0.04f);
                p.life = 0.5f + rnd() * 0.5f;
            }
        }
    }

    void drawParticles(Canvas &c, int W, int H) const
    {
        // Phase modulates particle visibility
        float vis = 1.f;
        if (t_ > 5.0f) vis = clamp01((6.0f - t_));

        for (int i = 0; i < kParticleCount; i++) {
            const Particle &p = parts_[i];
            float alpha = p.life * vis;
            if (alpha < 0.02f) continue;
            uint32_t col = withA(p.color, int(180 * alpha));
            float px = p.x * float(W);
            float py = p.y * float(H);
            c.fillCircle(px, py, p.size, col);
        }
    }

    // ── Moon ─────────────────────────────────────────────────────────────────
    // Crescent drawn as a filled white circle with a purple cutout offset.
    // Position: left-centre of screen, used as reference for logo phase.
    static void moonCenter(int W, int H, int &mx, int &my)
    {
        mx = W / 2 - 60;
        my = H / 2;
    }

    void drawMoon(Canvas &c, int W, int H, float alpha) const
    {
        int cx, cy;
        moonCenter(W, H, cx, cy);

        // Glow
        uint32_t glow = withA(theme::space::Lilac, int(30 * alpha));
        c.fillCircle(float(cx), float(cy), 70.f, glow);
        uint32_t glow2 = withA(theme::space::LunarWhite, int(15 * alpha));
        c.fillCircle(float(cx), float(cy), 55.f, glow2);

        // Moon body
        uint32_t moonCol = withA(theme::space::LunarWhite, int(230 * alpha));
        c.fillCircle(float(cx), float(cy), 40.f, moonCol);

        // Crescent cutout (deep purple to match background)
        uint32_t cutout = withA(theme::space::SpaceBlack, int(255 * alpha));
        // offset right and slightly up
        c.fillCircle(float(cx + 22), float(cy - 10), 32.f, cutout);
        // soften with space colour overlay so it blends into nebula
        uint32_t nebOver = withA(theme::space::DeepPurple, int(80 * alpha));
        c.fillCircle(float(cx + 22), float(cy - 10), 32.f, nebOver);
    }

    // ── Rocket ───────────────────────────────────────────────────────────────
    void drawRocket(Canvas &c, int W, int H, float alpha) const
    {
        // Rocket sits to the right of the moon
        int bx = W / 2 + 30;
        int by = H / 2 - 50;

        uint32_t red  = withA(theme::Red, int(240 * alpha));
        uint32_t glow = withA(theme::Red, int(40 * alpha));
        uint32_t flame= withA(theme::space::Lilac, int(180 * alpha));

        // Body glow
        c.fillCircle(float(bx), float(by + 30), 32.f, glow);

        // Nose cone (small circle at top)
        c.fillCircle(float(bx), float(by), 8.f, red);
        // Body (rounded rect)
        c.fillRoundRect(bx - 10, by + 6, 20, 48, 6, red);
        // Left fin
        c.fillRoundRect(bx - 20, by + 42, 12, 18, 4, red);
        // Right fin
        c.fillRoundRect(bx + 8, by + 42, 12, 18, 4, red);
        // Flame
        c.fillCircle(float(bx), float(by + 68), 6.f, flame);
        uint32_t flame2 = withA(theme::space::Lilac, int(80 * alpha));
        c.fillCircle(float(bx), float(by + 76), 10.f, flame2);
    }

    // ── Full logo (moon + rocket, brighter, stable) ─────────────────────────
    void drawFullLogo(Canvas &c, int W, int H, float alpha) const
    {
        // Moon
        int cx, cy;
        moonCenter(W, H, cx, cy);

        uint32_t glow = withA(theme::space::Lilac, int(50 * alpha));
        c.fillCircle(float(cx), float(cy), 80.f, glow);
        uint32_t moonCol = withA(theme::space::LunarWhite, int(240 * alpha));
        c.fillCircle(float(cx), float(cy), 44.f, moonCol);
        uint32_t cutout = withA(theme::space::SpaceBlack, int(255 * alpha));
        c.fillCircle(float(cx + 22), float(cy - 10), 34.f, cutout);
        uint32_t nebOver = withA(theme::space::DeepPurple, int(90 * alpha));
        c.fillCircle(float(cx + 22), float(cy - 10), 34.f, nebOver);

        // Rocket
        int bx = W / 2 + 30;
        int by = H / 2 - 50;
        uint32_t red  = withA(theme::Red, int(250 * alpha));
        uint32_t rglow= withA(theme::Red, int(60 * alpha));
        uint32_t flame= withA(theme::space::Lilac, int(200 * alpha));

        c.fillCircle(float(bx), float(by + 30), 36.f, rglow);
        c.fillCircle(float(bx), float(by), 8.f, red);
        c.fillRoundRect(bx - 10, by + 6, 20, 48, 6, red);
        c.fillRoundRect(bx - 20, by + 42, 12, 18, 4, red);
        c.fillRoundRect(bx + 8, by + 42, 12, 18, 4, red);
        c.fillCircle(float(bx), float(by + 68), 7.f, flame);
        uint32_t flame2 = withA(theme::space::Lilac, int(100 * alpha));
        c.fillCircle(float(bx), float(by + 76), 12.f, flame2);

        // App name "Moonlight" below the logo
        if (app.fonts.bold) {
            const char *name = "Moonlight";
            int tw = app.fonts.bold->measure(52, name);
            uint32_t nameCol = withA(theme::space::LunarWhite, int(240 * alpha));
            app.fonts.bold->draw(c, W / 2 - tw / 2, H / 2 + 90, 52, name, nameCol);
        }
    }

    // ── Tagline ──────────────────────────────────────────────────────────────
    void drawTagline(Canvas &c, int W, int H, float alpha) const
    {
        if (!app.fonts.regular) return;
        const char *tag = theme::kTaglineDefault;
        int tw = app.fonts.regular->measure(26, tag);
        uint32_t col = withA(theme::TextSub(), int(200 * alpha));
        app.fonts.regular->draw(c, W / 2 - tw / 2, H / 2 + 152, 26, tag, col);
    }
};

}  // namespace

std::unique_ptr<Screen> makeIntro(App &app)
{
    return std::unique_ptr<Screen>(new IntroScreen(app));
}

}  // namespace ml
