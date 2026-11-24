// Animated intro vignette (Frente E — v2: fully procedural, no PNG sprites).
//
// NARRATIVE TIMELINE:
//   0.0–1.0 s   CENA 1  Deep space: stars appear, crescent moon fades in at centre
//   1.0–2.5 s   CENA 2  Rocket enters far-right, tiny, accelerates toward the moon
//   2.5–4.0 s   CENA 3  Rocket sweeps past the moon in a red arc (orbital pass)
//                       Flash + particle burst at closest approach (~t=3.2s)
//   4.0–5.0 s   CENA 4  Elements settle to logo composition; trail stabilises to arc
//   5.0–6.0 s   CENA 5  "Moonlight" + tagline fade in; screen holds then exits
//   >6.2 s      Auto-pop
//
// All geometry built with Canvas primitives: fillCircle, fillRoundRect,
// fillConvex, line, shadow — zero external assets loaded at intro time.

#include "app/app.hpp"
#include "ui/theme.hpp"
#include "ui/widgets.hpp"

#include <cmath>

namespace ml {

using namespace ui;

// ──────────────────────────────────────────────────────────────────────────────
// Math helpers
// ──────────────────────────────────────────────────────────────────────────────

static inline float clamp01(float v) { return v < 0.f ? 0.f : v > 1.f ? 1.f : v; }
static inline float smoothstep(float t) { t = clamp01(t); return t*t*(3.f - 2.f*t); }
static inline float ease(float t)   // cubic in-out
{
    t = clamp01(t);
    return t < 0.5f ? 4*t*t*t : 1.f - (-2.f*t+2.f)*(-2.f*t+2.f)*(-2.f*t+2.f)*0.5f;
}
static inline float easeOut(float t) { t = clamp01(t); return 1.f-(1.f-t)*(1.f-t); }
static inline float easeIn(float t)  { t = clamp01(t); return t*t; }
static inline float lerp(float a, float b, float t) { return a + (b-a)*t; }

// Replace colour alpha channel only.
static inline uint32_t withA(uint32_t col, int a)
{
    return (col & 0x00FFFFFFu) | (uint32_t(a < 0 ? 0 : a > 255 ? 255 : a) << 24);
}

// ──────────────────────────────────────────────────────────────────────────────
// LCG deterministic RNG — same constants as C++ standard so seed is portable
// ──────────────────────────────────────────────────────────────────────────────
struct LCG {
    uint32_t s;
    explicit LCG(uint32_t seed) : s(seed) {}
    float next() {
        s = s * 1664525u + 1013904223u;
        return float(s >> 1) / float(0x7FFFFFFFu);
    }
};

// ──────────────────────────────────────────────────────────────────────────────
// Particle
// ──────────────────────────────────────────────────────────────────────────────
struct Particle {
    float x, y;        // 0..1 normalised
    float vx, vy;
    float life;        // 0..1
    float size;
    uint32_t color;
};

// ──────────────────────────────────────────────────────────────────────────────
// Trail point
// ──────────────────────────────────────────────────────────────────────────────
struct TrailPt {
    float x, y, age;   // age 0 = newest (fully opaque)
};

// ──────────────────────────────────────────────────────────────────────────────
// IntroScreen
// ──────────────────────────────────────────────────────────────────────────────
namespace {

class IntroScreen : public Screen {
public:
    explicit IntroScreen(App &a) : Screen(a)
    {
        initStars();
        initBurstParticles();
        for (int i = 0; i < kTrail; i++) trail_[i] = {-99.f, -99.f, 999.f};
        trailHead_ = 0;
        trailSize_ = 0;
        burstActive_ = false;
    }

    std::string title() const override { return ""; }
    bool fullscreen() const override { return true; }
    bool animating() const override { return t_ < kDuration; }
    bool busy() const override { return false; }
    Hints hints() const override { return {{BtnCross, "Pular"}, {BtnCircle, "Pular"}}; }
    void onEnter() override
    {
        t_ = 0.f;
        burstActive_ = false;
        trailSize_ = 0;
        trailHead_ = 0;
    }

    bool handle(uint32_t pressed) override
    {
        if (pressed & (BtnCross | BtnCircle | BtnTriangle)) { app.pop(); return true; }
        return false;
    }

    void update(float dt) override
    {
        if (t_ >= kDuration) { app.pop(); return; }
        t_ += dt;
        updateStars(dt);
        updateTrail(dt);
        updateBurst(dt);
    }

    void draw(Canvas &c, Rect /*area*/) override
    {
        const int W = theme::W;
        const int H = theme::H;

        drawBackground(c, W, H);
        drawStars(c, W, H);
        drawTrail(c, W, H);
        drawMoon(c, W, H);
        drawRocket(c, W, H);
        drawBurst(c, W, H);
        drawLogoText(c, W, H);
        drawSkipHint(c, W, H);
    }

private:
    // ── constants ──────────────────────────────────────────────────────────────
    static constexpr float kDuration = 6.3f;

    // Timeline (seconds)
    static constexpr float kMoonAppear   = 0.1f;   // moon starts fading in
    static constexpr float kRocketStart  = 1.0f;   // rocket enters
    static constexpr float kArcStart     = 2.5f;   // rocket begins orbital arc
    static constexpr float kBurstTime    = 3.2f;   // closest approach / flash
    static constexpr float kSettleStart  = 4.0f;   // elements lock to logo pose
    static constexpr float kTextIn       = 5.0f;   // "Moonlight" + tagline
    static constexpr float kFadeOut      = 5.9f;   // subtle scene fade (optional)

    // Moon geometry (final logo pose, centre of screen)
    // The moon is slightly left-of-centre so the rocket+arc sit to the right,
    // matching the logo composition.
    static constexpr float kMoonCX = 0.47f;   // normalised to W
    static constexpr float kMoonCY = 0.465f;  // normalised to H
    static constexpr float kMoonR  = 120.f;   // pixels (at 1920×1080)

    // Crescent cutout offset (right + slightly up), matched to logo proportions
    // cutout centre is offset so the visible crescent arc is ~35% of the circle
    static constexpr float kCutOX  =  52.f;
    static constexpr float kCutOY  = -24.f;
    static constexpr float kCutR   =  92.f;

    // Rocket trail
    static constexpr int kTrail = 48;

    // Background stars
    static constexpr int kStars = 80;

    // Burst particles (impact)
    static constexpr int kBurst = 32;

    // ── state ──────────────────────────────────────────────────────────────────
    float t_ = 0.f;

    // Stars
    struct Star { float x, y, size, twinkle, phase; uint32_t color; };
    Star stars_[kStars];

    // Rocket trail ring buffer
    TrailPt trail_[kTrail];
    int trailHead_ = 0;
    int trailSize_ = 0;
    float trailTimer_ = 0.f;

    // Burst particles
    Particle burst_[kBurst];
    bool burstActive_ = false;
    float burstAge_ = 0.f;

    // ── init ───────────────────────────────────────────────────────────────────
    void initStars()
    {
        LCG rng(0xC0FFEE42u);
        for (int i = 0; i < kStars; i++) {
            stars_[i].x       = rng.next();
            stars_[i].y       = rng.next();
            stars_[i].size    = 0.5f + rng.next() * 2.0f;
            stars_[i].twinkle = 0.4f + rng.next() * 0.6f;
            stars_[i].phase   = rng.next() * 6.283f;
            stars_[i].color   = (rng.next() > 0.75f)
                                ? theme::space::Lilac : theme::space::LunarWhite;
        }
    }

    void initBurstParticles()
    {
        LCG rng(0xABCD1234u);
        for (int i = 0; i < kBurst; i++) {
            float angle = float(i) / float(kBurst) * 6.283f + rng.next() * 0.4f;
            float spd   = 0.08f + rng.next() * 0.14f;
            burst_[i].vx   = std::cos(angle) * spd;
            burst_[i].vy   = std::sin(angle) * spd * 0.6f;
            burst_[i].size = 2.f + rng.next() * 4.f;
            burst_[i].life = 0.6f + rng.next() * 0.4f;
            burst_[i].color = (rng.next() > 0.5f)
                             ? theme::space::Lilac : theme::space::LunarWhite;
        }
    }

    // ── update ─────────────────────────────────────────────────────────────────
    void updateStars(float /*dt*/) {}  // stars twinkle via t_ in draw

    void updateTrail(float dt)
    {
        // Only accumulate trail while rocket is moving (cena 2 + 3)
        if (t_ < kRocketStart || t_ > kSettleStart + 0.5f) return;

        trailTimer_ += dt;
        static constexpr float kInterval = 1.f / 24.f;
        if (trailTimer_ < kInterval) return;
        trailTimer_ = 0.f;

        float rx, ry;
        rocketPos(t_, rx, ry);

        trail_[trailHead_] = {rx, ry, 0.f};
        trailHead_ = (trailHead_ + 1) % kTrail;
        if (trailSize_ < kTrail) trailSize_++;

        // Age all trail points
        for (int i = 0; i < trailSize_; i++) {
            int idx = ((trailHead_ - 1 - i) % kTrail + kTrail) % kTrail;
            trail_[idx].age += dt;
        }
    }

    void updateBurst(float dt)
    {
        if (!burstActive_ && t_ >= kBurstTime && t_ < kBurstTime + 0.1f) {
            // Spawn burst at closest-approach position
            float rx, ry;
            rocketPos(kBurstTime, rx, ry);
            for (int i = 0; i < kBurst; i++) {
                burst_[i].x = rx;
                burst_[i].y = ry;
                burst_[i].life = 0.6f + float(i) / float(kBurst) * 0.4f;
            }
            burstActive_ = true;
            burstAge_ = 0.f;
        }
        if (!burstActive_) return;
        burstAge_ += dt;
        for (int i = 0; i < kBurst; i++) {
            burst_[i].x += burst_[i].vx * dt;
            burst_[i].y += burst_[i].vy * dt;
            burst_[i].life -= dt * 0.9f;
        }
    }

    // ── rocket trajectory ──────────────────────────────────────────────────────
    // Phase 1 (1–2.5s): straight approach from far right, converging on moon
    // Phase 2 (2.5–4.0s): orbital arc sweeping past moon (parametric arc)
    // Phase 3 (4.0–5.0s): decelerate to final logo position
    void rocketPos(float t, float &rx, float &ry) const
    {
        const float W = float(theme::W);
        const float H = float(theme::H);
        const float mcx = kMoonCX * W;
        const float mcy = kMoonCY * H;

        if (t < kRocketStart) {
            // Off-screen right
            rx = W + 60.f;
            ry = mcy - 80.f;
            return;
        }
        if (t < kArcStart) {
            // Straight approach from far right, slightly above moon
            float p = ease(clamp01((t - kRocketStart) / (kArcStart - kRocketStart)));
            float startX = W + 60.f;
            float startY = mcy - 160.f;
            float endX   = mcx + kMoonR * 1.4f;  // arriving just outside moon
            float endY   = mcy - kMoonR * 0.5f;
            rx = lerp(startX, endX, p);
            ry = lerp(startY, endY, p);
            return;
        }
        if (t < kSettleStart) {
            // Orbital arc: sweeps from upper-right, past the moon face, to lower-right
            // Uses an elliptical parametric arc centred on the moon
            float p = ease(clamp01((t - kArcStart) / (kSettleStart - kArcStart)));
            // arc from angle -60° to +120° (degrees, 0 = right)
            float a0 = -1.05f; // -60°
            float a1 =  0.80f; //  ~46° — rocket ends lower-right
            float angle = lerp(a0, a1, p);
            float orbitRX = kMoonR * 1.35f;   // horizontal semi-axis
            float orbitRY = kMoonR * 1.00f;   // vertical semi-axis (flatter)
            rx = mcx + std::cos(angle) * orbitRX;
            ry = mcy + std::sin(angle) * orbitRY;
            return;
        }
        // Settle phase: rocket moves to logo final position
        // Final logo position: upper-right of moon (matches logo reference)
        float finalX = mcx + kMoonR * 1.05f;
        float finalY = mcy - kMoonR * 0.60f;
        // Start from end of arc
        float arcEndAngle = 0.80f;
        float arcEndX = mcx + std::cos(arcEndAngle) * kMoonR * 1.35f;
        float arcEndY = mcy + std::sin(arcEndAngle) * kMoonR * 1.00f;
        float p = ease(clamp01((t - kSettleStart) / 0.8f));
        rx = lerp(arcEndX, finalX, p);
        ry = lerp(arcEndY, finalY, p);
    }

    // Rocket heading angle (radians) — derived from velocity direction
    float rocketAngle(float t) const
    {
        if (t < kRocketStart) return -0.5f;
        float dt = 0.04f;
        float x0, y0, x1, y1;
        rocketPos(t,      x0, y0);
        rocketPos(t + dt, x1, y1);
        float dx = x1 - x0, dy = y1 - y0;
        if (dx*dx + dy*dy < 0.0001f) return -0.5f;
        return std::atan2(dy, dx);
    }

    // ── draw helpers ───────────────────────────────────────────────────────────
    void drawBackground(Canvas &c, int W, int H) const
    {
        c.fillRect(0, 0, W, H, theme::space::SpaceBlack);

        // Nebula glow that evolves with the scene
        float a = 0.f;
        if      (t_ < 1.f)  a = smoothstep(t_);
        else if (t_ < 5.f)  a = 1.f;
        else                a = smoothstep(6.f - t_);

        if (a > 0.01f) {
            c.fillCircle(float(W) * 0.68f, float(H) * 0.42f, 460.f,
                         withA(theme::space::DeepPurple, int(70 * a)));
            c.fillCircle(float(W) * kMoonCX, float(H) * kMoonCY, 280.f,
                         withA(theme::space::Lilac, int(14 * a)));
        }
    }

    void drawStars(Canvas &c, int W, int H) const
    {
        // Stars twinkle as a sine over time; fade in during cena 1
        float vis = smoothstep(clamp01(t_ / 0.8f));
        if (vis < 0.01f) return;

        for (int i = 0; i < kStars; i++) {
            const Star &s = stars_[i];
            float tw = 0.6f + 0.4f * std::sin(t_ * s.twinkle * 2.5f + s.phase);
            float alpha = tw * vis;
            uint32_t col = withA(s.color, int(190 * alpha));
            c.fillCircle(s.x * float(W), s.y * float(H), s.size, col);
        }
    }

    void drawTrail(Canvas &c, int W, int H) const
    {
        if (trailSize_ < 2) return;

        // Trail fades out after kSettleStart; leave a stable arc in cena 4
        float settle = clamp01((t_ - kSettleStart) / 0.6f);
        // During settle the trail fades out gently (the arc "stabilises")
        float trailVis = t_ < kSettleStart ? 1.f : (1.f - settle * 0.7f);

        for (int i = 0; i < trailSize_ - 1; i++) {
            int idx0 = ((trailHead_ - 1 - i) % kTrail + kTrail) % kTrail;
            int idx1 = ((trailHead_ - 2 - i) % kTrail + kTrail) % kTrail;
            const TrailPt &p0 = trail_[idx0];
            const TrailPt &p1 = trail_[idx1];
            if (p0.age > 2.5f || p1.age > 2.5f) continue;

            float alpha0 = clamp01(1.f - p0.age / 1.6f) * trailVis;
            float alpha1 = clamp01(1.f - p1.age / 1.6f) * trailVis;
            float alpha  = (alpha0 + alpha1) * 0.5f;
            if (alpha < 0.02f) continue;

            float thick = lerp(3.f, 1.f, float(i) / float(trailSize_));
            uint32_t col = withA(theme::Red, int(220 * alpha));
            c.line(p0.x * float(W), p0.y * float(H),
                   p1.x * float(W), p1.y * float(H), thick, col);
        }
    }

    void drawMoon(Canvas &c, int W, int H) const
    {
        // Fade in during cena 1
        float moonA = 0.f;
        if      (t_ < kMoonAppear)         moonA = 0.f;
        else if (t_ < kMoonAppear + 1.0f)  moonA = ease(clamp01((t_ - kMoonAppear) / 1.0f));
        else                               moonA = 1.f;
        if (moonA < 0.01f) return;

        float cx = kMoonCX * float(W);
        float cy = kMoonCY * float(H);

        // Soft glow ring (outer — very subtle, large but near-transparent)
        c.fillCircle(cx, cy, kMoonR * 1.9f, withA(theme::space::Lilac,      int(8 * moonA)));
        c.fillCircle(cx, cy, kMoonR * 1.4f, withA(theme::space::LunarWhite, int(6 * moonA)));

        // Moon body
        c.fillCircle(cx, cy, kMoonR, withA(theme::space::LunarWhite, int(230 * moonA)));

        // Subtle surface shading — a slightly darker circle off-centre
        c.fillCircle(cx - kMoonR * 0.15f, cy + kMoonR * 0.1f, kMoonR * 0.72f,
                     withA(theme::space::LunarWhite, int(18 * moonA)));  // very faint

        // Crescent cutout — dark circle offset right+up
        float cotX = cx + kCutOX;
        float cotY = cy + kCutOY;
        c.fillCircle(cotX, cotY, kCutR, withA(theme::space::SpaceBlack, int(255 * moonA)));
        // Add faint purple so cutout blends with nebula background
        c.fillCircle(cotX, cotY, kCutR, withA(theme::space::DeepPurple, int(90 * moonA)));

        // Impact flash ring at closest approach
        if (t_ > kBurstTime && t_ < kBurstTime + 0.6f) {
            float fa = 1.f - clamp01((t_ - kBurstTime) / 0.6f);
            c.strokeCircle(cx, cy, kMoonR * (1.f + fa * 0.2f), 2.f,
                           withA(theme::space::Lilac, int(180 * fa)));
        }
    }

    void drawRocket(Canvas &c, int W, int H) const
    {
        if (t_ < kRocketStart - 0.05f) return;

        // Fade in
        float rktA = ease(clamp01((t_ - kRocketStart) / 0.5f));
        // After final settle phase slightly pulsate
        if (t_ > kSettleStart + 0.8f) {
            float pulse = 1.f + 0.04f * std::sin(t_ * 6.f);
            rktA = clamp01(rktA * pulse);
        }
        if (rktA < 0.01f) return;

        float rx, ry;
        rocketPos(t_, rx, ry);
        float angle = rocketAngle(t_);

        // Scale: rocket grows as it approaches (perspective feel)
        // During approach (cena 2) it starts at 0.3× and grows to 1×
        float scaleFactor = 1.f;
        if (t_ < kArcStart) {
            float p = clamp01((t_ - kRocketStart) / (kArcStart - kRocketStart));
            scaleFactor = lerp(0.25f, 1.f, ease(p));
        }

        drawRocketAt(c, rx, ry, angle, scaleFactor, rktA);
    }

    // Draw the rocket body at an arbitrary position/angle/scale/alpha.
    // The rocket body is defined in its own local coordinate system:
    //   +Y = forward (nose direction), origin = body centre.
    // We use 2D rotation and fillConvex / fillCircle / fillRoundRect.
    void drawRocketAt(Canvas &c, float cx, float cy,
                      float angle, float scale, float alpha) const
    {
        // sinA/cosA for rotating local coordinates
        // angle=0 means moving right (+x), so "forward" is +x.
        // We want nose to point in the direction of travel.
        float sinA = std::sin(angle);
        float cosA = std::cos(angle);

        // Helper: rotate local (lx, ly) around (cx, cy)
        auto rot = [&](float lx, float ly, float &ox, float &oy) {
            ox = cx + (lx * cosA - ly * sinA);
            oy = cy + (lx * sinA + ly * cosA);
        };

        float R   = scale;   // scale multiplier
        uint32_t red   = withA(theme::Red,              int(255 * alpha));
        uint32_t redD  = withA(theme::Red,              int(160 * alpha));  // darker red for window edge
        uint32_t dark  = withA(theme::space::SpaceBlack, int(220 * alpha));
        uint32_t lilac = withA(theme::space::Lilac,     int(200 * alpha));
        uint32_t lilac2= withA(theme::space::Lilac,     int(80  * alpha));
        uint32_t white = withA(theme::space::LunarWhite,int(240 * alpha));

        // ── Body: elongated rounded rect (approximated as a polygon) ──
        // Body: 24×52px at scale=1, centred at origin, nose toward +x
        float bHalf = 26.f * R;
        float bWid  = 11.f * R;
        // 4 corners of the body rectangle in local space (nose = +x side)
        float bPts[8];
        float bx0 = bHalf, by0 = -bWid;   // nose-top
        float bx1 = bHalf, by1 =  bWid;   // nose-bottom
        float bx2 =-bHalf, by2 =  bWid;   // tail-bottom
        float bx3 =-bHalf, by3 = -bWid;   // tail-top
        rot(bx0, by0, bPts[0], bPts[1]);
        rot(bx1, by1, bPts[2], bPts[3]);
        rot(bx2, by2, bPts[4], bPts[5]);
        rot(bx3, by3, bPts[6], bPts[7]);
        c.fillConvex(bPts, 4, red);

        // ── Nose cone (pointed — equilateral-ish triangle) ──
        float nLen = 18.f * R;
        float nWid =  9.f * R;
        float nPts[6];
        rot(bHalf + nLen, 0,     nPts[0], nPts[1]);  // tip
        rot(bHalf,       -nWid, nPts[2], nPts[3]);  // back-top
        rot(bHalf,        nWid, nPts[4], nPts[5]);  // back-bottom
        c.fillConvex(nPts, 3, white);

        // ── Fins (two triangles at tail, top/bottom) ──
        float fOff = 11.f * R;  // how far from centre the fin starts
        float fLen = 16.f * R;  // fin length along body axis
        float fWid = 10.f * R;  // fin width perpendicular

        // Top fin
        float ftPts[6];
        rot(-bHalf + fOff,  -bWid,          ftPts[0], ftPts[1]);
        rot(-bHalf,          -bWid - fWid,   ftPts[2], ftPts[3]);
        rot(-bHalf + fLen,   -bWid,          ftPts[4], ftPts[5]);
        c.fillConvex(ftPts, 3, red);

        // Bottom fin
        float fbPts[6];
        rot(-bHalf + fOff,   bWid,          fbPts[0], fbPts[1]);
        rot(-bHalf,           bWid + fWid,   fbPts[2], fbPts[3]);
        rot(-bHalf + fLen,    bWid,          fbPts[4], fbPts[5]);
        c.fillConvex(fbPts, 3, red);

        // ── Porthole window ──
        float wx, wy;
        rot(4.f * R, 0.f, wx, wy);
        c.fillCircle(wx, wy, 5.f * R, dark);
        c.strokeCircle(wx, wy, 5.f * R, 1.5f * R, redD);

        // ── Flame (lilac glow behind tail) ──
        // Flame position is at the tail (-x direction)
        float fmLen = (8.f + 6.f * std::sin(t_ * 18.f)) * R;  // flicker
        float fmX, fmY;
        rot(-bHalf - fmLen, 0.f, fmX, fmY);
        c.fillCircle(fmX, fmY, 7.f * R, lilac);

        // Outer glow halo behind rocket
        float gx, gy;
        rot(-bHalf, 0.f, gx, gy);
        c.fillCircle(gx, gy, 20.f * R, lilac2);

        // ── Red glow around body ──
        c.fillCircle(cx, cy, 36.f * R, withA(theme::Red, int(22 * alpha)));
    }

    void drawBurst(Canvas &c, int W, int H) const
    {
        if (!burstActive_) return;
        for (int i = 0; i < kBurst; i++) {
            const Particle &p = burst_[i];
            if (p.life < 0.f) continue;
            float a = clamp01(p.life / 0.6f);
            uint32_t col = withA(p.color, int(220 * a));
            c.fillCircle(p.x * float(W), p.y * float(H), p.size * (1.f + (1.f - a)), col);
        }
    }

    void drawLogoText(Canvas &c, int W, int H) const
    {
        if (t_ < kTextIn) return;
        float a = ease(clamp01((t_ - kTextIn) / 0.6f));
        if (a < 0.01f) return;

        float cy = kMoonCY * float(H);

        // "Moonlight"
        if (app.fonts.bold) {
            const char *name = "Moonlight";
            int tw = app.fonts.bold->measure(52, name);
            uint32_t col = withA(theme::space::LunarWhite, int(240 * a));
            app.fonts.bold->draw(c, W/2 - tw/2, int(cy) + int(kMoonR) + 30, 52, name, col);
        }

        // Tagline
        if (t_ >= kTextIn + 0.3f && app.fonts.regular) {
            float ta = ease(clamp01((t_ - kTextIn - 0.3f) / 0.5f));
            const char *tag = theme::kTaglineDefault;
            int tw = app.fonts.regular->measure(26, tag);
            uint32_t col = withA(theme::TextSub(), int(200 * ta));
            app.fonts.regular->draw(c, W/2 - tw/2, int(cy) + int(kMoonR) + 96, 26, tag, col);
        }
    }

    void drawSkipHint(Canvas &c, int W, int H) const
    {
        float a = 1.f;
        if (t_ < 0.5f)  a = t_ / 0.5f;
        if (t_ > 5.5f)  a = (6.f - t_) / 0.5f;
        a = clamp01(a);
        if (a < 0.01f) return;
        uint32_t col = withA(theme::TextSub(), int(130 * a));
        const char *hint = "Pressione X para pular";
        if (app.fonts.regular) {
            int tw = app.fonts.regular->measure(20, hint);
            app.fonts.regular->draw(c, W - tw - 32, H - 36, 20, hint, col);
        }
    }
};

}  // namespace

std::unique_ptr<Screen> makeIntro(App &app)
{
    return std::unique_ptr<Screen>(new IntroScreen(app));
}

}  // namespace ml
