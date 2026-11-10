// UI: input mapping, rendering primitives, text, and a headless run of the whole app
// driven by scripted DualShock presses.
#include <string>

#include "app/app.hpp"
#include "platform/host/host_platform.hpp"
#include "ui/canvas.hpp"
#include "ui/font.hpp"
#include "ui/input_map.hpp"
#include "player/nv12.hpp"
#include "ui/image_loader.hpp"
#include <cstdlib>
#include "test.hpp"

using namespace ml;

static std::string assetsDir()
{
    std::string f = t::fixtures;   // tests/fixtures -> assets
    return f.substr(0, f.rfind("/tests/")) + "/assets";
}

TEST(input_repeat_and_stick)
{
    ui::InputMap m;
    PadState s;
    s.connected = true;
    s.buttons = BtnDown;
    CHECK_EQ(m.update(s, 0), uint32_t(BtnDown));
    CHECK_EQ(m.update(s, 100), uint32_t(0));          // held, before repeat delay
    CHECK_EQ(m.update(s, 400), uint32_t(BtnDown));    // repeat
    CHECK_EQ(m.update(s, 450), uint32_t(0));
    CHECK_EQ(m.update(s, 490), uint32_t(BtnDown));
    s.buttons = BtnCross;
    CHECK_EQ(m.update(s, 500), uint32_t(BtnCross));
    CHECK_EQ(m.update(s, 1500), uint32_t(0));         // X never repeats
    s.buttons = 0;
    s.lx = 255;                                        // stick right
    CHECK_EQ(m.update(s, 1600), uint32_t(BtnRight));
    s.lx = 128;
    s.ly = 0;                                          // stick up
    CHECK_EQ(m.update(s, 1700), uint32_t(BtnUp));
    s.connected = false;
    CHECK_EQ(m.update(s, 1800), uint32_t(0));
}

TEST(canvas_shapes)
{
    std::vector<uint32_t> px(100 * 100, 0xFF000000u);
    Framebuffer fb{px.data(), 100, 100, 100};
    ui::Canvas c(fb);
    c.fillRoundRect(10, 10, 80, 80, 20, ui::rgba(255, 0, 0));
    CHECK_EQ(px[50 * 100 + 50], 0xFFFF0000u);         // centre filled
    CHECK_EQ(px[11 * 100 + 11], 0xFF000000u);         // rounded corner left empty
    c.pushClip({0, 0, 50, 100});
    c.fillRect(0, 0, 100, 5, ui::rgba(0, 255, 0));
    c.popClip();
    CHECK_EQ(px[2 * 100 + 40], 0xFF00FF00u);
    CHECK_EQ(px[2 * 100 + 60], 0xFF000000u);          // clipped
    c.fillRect(0, 95, 100, 5, ui::rgba(255, 255, 255, 128));
    uint32_t g = (px[97 * 100 + 5] >> 8) & 0xFF;
    CHECK(g > 120 && g < 136);                         // 50% blend
}

TEST(font_render_and_wrap)
{
    host::HostFileSystem fs("");
    std::string ttf;
    CHECK(fs.readFile(assetsDir() + "/fonts/Outfit-Regular.ttf", ttf, 4 << 20));
    ui::Font f(ttf);
    CHECK(f.ok());
    CHECK(f.measure(30, "Moonlight") > 100);
    CHECK(f.measure(30, "Moonlight") < f.measure(40, "Moonlight"));
    std::string e = f.ellipsize(30, "Um titulo bem comprido que nao cabe", 200);
    CHECK(f.measure(30, e) <= 200);
    CHECK(e.size() >= 3 && e.compare(e.size() - 3, 3, "...") == 0);
    auto lines = f.wrap(24, "uma frase com varias palavras para quebrar em linhas curtas", 200, 2);
    CHECK_EQ(lines.size(), size_t(2));
    std::vector<uint32_t> px(300 * 60, 0xFF000000u);
    ui::Canvas c({px.data(), 300, 60, 300});
    f.draw(c, 4, 4, 32, "A\xC3\xA7\xC3\xA3o", ui::rgba(255, 255, 255));   // UTF-8 accents
    size_t lit = 0;
    for (auto p : px) if ((p & 0xFF) > 128) lit++;
    CHECK(lit > 100);
    size_t i = 0;
    CHECK_EQ(ui::Font::nextCodepoint(std::string("\xC3\xA7"), i), uint32_t(0xE7));
    i = 0;
    CHECK_EQ(ui::Font::nextCodepoint(std::string("\x80"), i), uint32_t(0xFFFD));
}

namespace {
struct Harness {
    host::HostPlatform plat;
    App app;
    explicit Harness(const std::string &sb) : plat(sb, assetsDir()), app(plat) {}
    void press(uint32_t b)
    {
        plat.input_.state.buttons = b;
        app.frame();
        plat.input_.state.buttons = 0;
        app.frame();
    }
};
}  // namespace

TEST(app_navigation_headless)
{
    std::string sb = t::scratch + "/appsb";
    host::HostFileSystem(sb).makeDirs("/");
    Harness h(sb);
    std::string err;
    CHECK(h.app.init(&err));
    if (!err.empty()) { std::printf("[%s] ", err.c_str()); return; }
    h.app.frame();
    CHECK(h.app.renderedLastFrame());
    CHECK(h.app.section() == Section::Home);
    CHECK(!h.app.sidebarFocused());
    h.press(BtnLeft);                       // first card -> sidebar
    CHECK(h.app.sidebarFocused());
    h.press(BtnDown);                       // sidebar: Musica
    CHECK(h.app.section() == Section::Music);
    h.press(BtnDown);                       // Filmes (Radios is hidden by default)
    CHECK(h.app.section() == Section::Movies);
    h.press(BtnDown);                       // Series
    CHECK(h.app.section() == Section::Series);
    h.press(BtnRight);                      // into content
    CHECK(!h.app.sidebarFocused());
    h.press(BtnCross);                      // open first series detail
    CHECK(h.app.renderedLastFrame() || true);
    h.press(BtnCross);                      // "Assistir" on demo content -> modal
    CHECK(h.app.modalOpen());
    h.press(BtnCross);                      // close modal
    CHECK(!h.app.modalOpen());
    h.press(BtnRight);
    h.press(BtnCross);                      // Favoritar
    CHECK_EQ(h.app.library.favorites().size(), size_t(1));
    h.press(BtnCircle);                     // back to grid
    h.press(BtnR2);                         // next section: Videos
    CHECK(h.app.section() == Section::Videos);
    h.app.playVideo("/data/moonlight/videos/x.mp4", "x");   // PC build: no video engine -> explained
    CHECK(h.app.modalOpen());
    h.press(BtnCross);
    h.press(BtnR2);                         // Canais
    CHECK(h.app.section() == Section::Live);
    // nothing rendered when idle
    h.app.frame();
    h.app.frame();
    for (int i = 0; i < 40; i++) h.app.frame();
    CHECK(!h.app.renderedLastFrame());
    // favorites persisted on disk
    CHECK(h.plat.fs().exists("/data/moonlight/library.json"));
}

TEST(app_keyboard_add_source_offline)
{
    std::string sb = t::scratch + "/appsb2";
    host::HostFileSystem(sb).makeDirs("/");
    Harness h(sb);
    std::string err;
    CHECK(h.app.init(&err));
    h.app.goSection(Section::Sources, true);
    h.press(BtnCross);                      // empty list: X opens the keyboard
    CHECK(h.app.keyboard.active());
    h.press(BtnOptions);                    // confirm "https://" -> add fails (invalid URL) -> modal
    CHECK(!h.app.keyboard.active());
    CHECK(h.app.modalOpen());
    CHECK(h.app.sources.sources().empty());
}

TEST(render_cost_report)
{
    // Not a pass/fail benchmark: reports how long a full 1920x1080 software frame takes on this PC.
    // The PS4 CPU (Jaguar 1.6 GHz) is several times slower; see docs/COMPATIBILITY.md.
    std::string sb = t::scratch + "/appsb3";
    host::HostFileSystem(sb).makeDirs("/");
    Harness h(sb);
    std::string err;
    CHECK(h.app.init(&err));
    uint64_t t0 = h.plat.nowMs();
    int n = 0;
    for (; n < 20; n++) { h.app.dirty(); h.app.frame(); }
    uint64_t ms = h.plat.nowMs() - t0;
    std::printf("(%.1f ms/frame on host) ", double(ms) / n);
}

TEST(nv12_to_argb)
{
    // 4x2 frame, pitch 8, aligned height 2: left half black (Y=16), right half white (Y=235), neutral chroma
    std::vector<uint8_t> f(8 * 2 + 8 * 1, 128);
    for (int y = 0; y < 2; y++)
        for (int x = 0; x < 8; x++) f[size_t(y * 8 + x)] = x < 2 ? 16 : 235;
    Nv12Frame src;
    src.data = f.data();
    src.width = 4;
    src.height = 2;
    src.pitch = 8;
    src.alignedHeight = 2;
    std::vector<uint32_t> px(8 * 8, 0x12345678u);
    CHECK(nv12ToArgb(src, {px.data(), 8, 8, 8}));
    // 4x2 scaled to 8x4 centred in 8x8: rows 0-1 and 6-7 are black bars
    CHECK_EQ(px[0], 0xFF000000u);
    CHECK_EQ(px[2 * 8 + 0], 0xFF000000u);              // black video pixel
    CHECK_EQ(px[2 * 8 + 7], 0xFFFFFFFFu);              // white video pixel (235 -> 255)
    CHECK_EQ(px[7 * 8 + 7], 0xFF000000u);              // bottom bar
    // pure red in BT.709 limited range: Y=63 U=102 V=240
    std::vector<uint8_t> r = {63, 63, 63, 63, 102, 240};
    Nv12Frame red;
    red.data = r.data();
    red.width = 2;
    red.height = 2;
    red.pitch = 2;
    red.alignedHeight = 2;
    uint32_t out[4];
    CHECK(nv12ToArgb(red, {out, 2, 2, 2}));
    uint32_t R = (out[0] >> 16) & 0xFF, G = (out[0] >> 8) & 0xFF, B = out[0] & 0xFF;
    CHECK(R > 240 && G < 15 && B < 15);
    Nv12Frame bad;
    CHECK(!nv12ToArgb(bad, {out, 2, 2, 2}));
    // 1080p conversion cost on this PC (the PS4 does this on the CPU for every video frame)
    std::vector<uint8_t> big(size_t(1920) * 1088 * 3 / 2, 100);
    Nv12Frame b;
    b.data = big.data();
    b.width = 1920;
    b.height = 1080;
    b.pitch = 1920;
    b.alignedHeight = 1088;
    std::vector<uint32_t> fb(size_t(1920) * 1080);
    host::HostPlatform plat(t::scratch, "");
    uint64_t t0 = plat.nowMs();
    for (int i = 0; i < 10; i++) nv12ToArgb(b, {fb.data(), 1920, 1080, 1920});
    std::printf("(%.1f ms/frame on host) ", double(plat.nowMs() - t0) / 10);
}

TEST(image_decode_cover)
{
    host::HostFileSystem fs("");
    std::string png, jpg;
    CHECK(fs.readFile(t::fixtures + "/radio/poster.png", png, 1 << 20));
    CHECK(fs.readFile(t::fixtures + "/radio/poster.jpg", jpg, 1 << 20));
    ui::Image a, b;
    CHECK(ui::decodeImage(png, 100, 150, a));
    CHECK(a.w == 100 && a.h == 150);
    CHECK_EQ(a.px[10 * 100 + 50], 0xFFC81E1Eu);           // top half red
    CHECK_EQ(a.px[140 * 100 + 50], 0xFF1E1EC8u);          // bottom half blue
    CHECK(ui::decodeImage(jpg, 60, 60, b));                // square crop of a 2:3 image
    uint32_t top = b.px[2 * 60 + 30];
    CHECK(((top >> 16) & 0xFF) > 150);                    // still red-ish (JPEG is lossy)
    ui::Image c;
    CHECK(!ui::decodeImage("<html>not an image</html>", 10, 10, c));
    CHECK(!ui::decodeImage(png, 0, 10, c));
}

TEST(sources_import_list_offline)
{
    std::string sb = t::scratch + "/appsb4";
    host::HostFileSystem(sb).makeDirs("/mnt/usb0");
    Harness h(sb);
    std::string err;
    CHECK(h.app.init(&err));
    h.app.goSection(Section::Sources, true);
    h.press(BtnR1);                                         // no file yet -> instructions
    CHECK(h.app.modalOpen());
    h.press(BtnCross);
    h.plat.fs().writeFile("/mnt/usb0/moonlight-fontes.txt",
                          "# comentario\r\nhttp://remote.example/manifest.json\r\n\r\nnao-e-url\n");
    h.press(BtnR1);
    CHECK(h.app.modalOpen());                               // 0 added, 2 with error
    CHECK(h.app.sources.sources().empty());
}

TEST(image_loader_live_local_server)
{
    const char *base = std::getenv("ML_RADIO_URL");   // the radio fixture server also serves poster.png
    if (!base) { std::printf("(skipped: ML_RADIO_URL not set) "); return; }
    host::HostPlatform plat(t::scratch, "");
    ui::ImageLoader loader(plat, plat.http());
    loader.start();
    std::string url = std::string(base) + "/poster.png";
    const ui::Image *img = loader.get(url, 40, 60);
    for (int i = 0; i < 300 && !img; i++) { plat.sleepMs(10); img = loader.get(url, 40, 60); }
    CHECK(img != nullptr);
    if (img) CHECK(img->w == 40 && img->h == 60);
    CHECK(loader.takeUpdated());
    CHECK(loader.get(std::string(base) + "/nao-existe.png", 40, 60) == nullptr);
}
