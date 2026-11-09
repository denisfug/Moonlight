// Entry point. PS4: runs the app loop forever. Host: headless runner that feeds a scripted
// sequence of DualShock presses and saves screenshots (used by tests and docs).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "app/app.hpp"

#if defined(ML_PLATFORM_PS4)
#include "platform/ps4/ps4_platform.hpp"

int main()
{
    ml::ps4::Ps4Platform platform;
    std::string err;
    if (!platform.init(&err)) {
        platform.notify("Moonlight HB: " + err);
        for (;;) platform.sleepMs(1000);
    }
    ml::App app(platform);
    if (!app.init(&err)) {
        platform.notify("Moonlight HB: " + err);
        for (;;) platform.sleepMs(1000);
    }
    for (;;) {
        app.frame();
        if (!app.renderedLastFrame()) platform.sleepMs(16);   // present() already waits for vsync
    }
}

#else
#include "platform/host/host_platform.hpp"

static uint32_t buttonByName(const std::string &n)
{
    struct { const char *n; uint32_t b; } map[] = {
        {"up", ml::BtnUp}, {"down", ml::BtnDown}, {"left", ml::BtnLeft}, {"right", ml::BtnRight},
        {"x", ml::BtnCross}, {"o", ml::BtnCircle}, {"square", ml::BtnSquare}, {"triangle", ml::BtnTriangle},
        {"l1", ml::BtnL1}, {"r1", ml::BtnR1}, {"l2", ml::BtnL2}, {"r2", ml::BtnR2}, {"options", ml::BtnOptions},
        {"r3", ml::BtnR3},
    };
    for (auto &m : map) if (n == m.n) return m.b;
    return 0;
}

// usage: moonlight_host <sandbox> <assets> <outdir> "step step ..."
//   steps: a button name (press+release), "wait:MS", "shot:NAME" (save PNG)
int main(int argc, char **argv)
{
    if (argc < 5) {
        std::fprintf(stderr, "usage: %s <sandbox> <assets> <outdir> \"x down shot:home ...\"\n", argv[0]);
        return 2;
    }
    ml::host::HostPlatform platform(argv[1], argv[2]);
    ml::App app(platform);
    std::string err;
    if (!app.init(&err)) { std::fprintf(stderr, "init failed: %s\n", err.c_str()); return 1; }
    if (const char *r = std::getenv("ML_RADIO_URL")) app.radio.servers = {r};   // local test directory
    std::string outdir = argv[3];
    char *script = strdup(argv[4]);
    auto runFrames = [&](int n) { for (int i = 0; i < n; i++) { app.frame(); platform.sleepMs(16); } };
    runFrames(5);
    for (char *tok = std::strtok(script, " "); tok; tok = std::strtok(nullptr, " ")) {
        std::string s = tok;
        if (s.compare(0, 5, "wait:") == 0) { runFrames(std::atoi(s.c_str() + 5) / 16 + 1); continue; }
        if (s.compare(0, 5, "shot:") == 0) {
            runFrames(30);   // let animations settle
            std::string path = outdir + "/" + s.substr(5) + ".png";
            if (!platform.display_.savePng(path)) { std::fprintf(stderr, "cannot write %s\n", path.c_str()); return 1; }
            std::printf("saved %s\n", path.c_str());
            continue;
        }
        uint32_t b = buttonByName(s);
        if (!b) { std::fprintf(stderr, "unknown step %s\n", s.c_str()); return 2; }
        platform.input_.state.buttons = b;
        runFrames(2);
        platform.input_.state.buttons = 0;
        runFrames(3);
    }
    free(script);
    return 0;
}
#endif
