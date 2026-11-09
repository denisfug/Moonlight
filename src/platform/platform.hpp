// The only interface between Moonlight and the machine it runs on.
// PS4 implementation: src/platform/ps4. PC implementation (tests, screenshots): src/platform/host.
// Nothing outside src/platform may include PS4 (orbis/*) headers.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/net.hpp"

namespace ml {

// ---------------------------------------------------------------- input
enum Button : uint32_t {
    BtnUp = 1u << 0, BtnDown = 1u << 1, BtnLeft = 1u << 2, BtnRight = 1u << 3,
    BtnCross = 1u << 4, BtnCircle = 1u << 5, BtnSquare = 1u << 6, BtnTriangle = 1u << 7,
    BtnL1 = 1u << 8, BtnR1 = 1u << 9, BtnL2 = 1u << 10, BtnR2 = 1u << 11,
    BtnOptions = 1u << 12, BtnTouchpad = 1u << 13, BtnL3 = 1u << 14, BtnR3 = 1u << 15,
};

struct PadState {
    bool connected = false;
    uint32_t buttons = 0;
    uint8_t lx = 128, ly = 128, rx = 128, ry = 128;   // 0..255, 128 = centre
};

class Input {
public:
    virtual ~Input() = default;
    virtual PadState read() = 0;
};

// ---------------------------------------------------------------- display
// A 32-bit 0xAARRGGBB framebuffer the UI renders into, then presents.
struct Framebuffer {
    uint32_t *pixels = nullptr;
    int width = 0, height = 0, stride = 0;   // stride in pixels
};

class Display {
public:
    virtual ~Display() = default;
    virtual Framebuffer backBuffer() = 0;
    virtual void present() = 0;   // shows the back buffer (waits for vsync on PS4)
};

// ---------------------------------------------------------------- audio
// Interleaved stereo int16 at 48 kHz, fixed blocks of framesPerBlock().
class AudioOut {
public:
    virtual ~AudioOut() = default;
    virtual bool open() = 0;
    virtual int framesPerBlock() const = 0;
    virtual bool write(const int16_t *stereo) = 0;   // blocks until the block is queued
    virtual void close() = 0;
};

// ---------------------------------------------------------------- files
struct DirEntry {
    std::string name;
    bool isDir = false;
    uint64_t size = 0;
};

class FileSystem {
public:
    virtual ~FileSystem() = default;
    virtual bool readFile(const std::string &path, std::string &out, size_t maxBytes) = 0;
    // Atomic: writes path.tmp then renames over path.
    virtual bool writeFile(const std::string &path, const std::string &data) = 0;
    virtual bool listDir(const std::string &path, std::vector<DirEntry> &out) = 0;
    virtual bool makeDirs(const std::string &path) = 0;
    virtual bool exists(const std::string &path) = 0;
};

// ---------------------------------------------------------------- everything together
class Platform {
public:
    virtual ~Platform() = default;
    virtual std::string name() const = 0;
    virtual Display &display() = 0;
    virtual Input &input() = 0;
    virtual FileSystem &fs() = 0;
    virtual std::unique_ptr<AudioOut> makeAudioOut() = 0;
    virtual HttpClient *http() = 0;               // nullptr = no network implementation yet
    virtual std::string dataDir() const = 0;      // persistent app data (JSON files)
    virtual std::string assetDir() const = 0;     // packaged read-only assets
    virtual std::vector<std::string> musicDirs() const = 0;
    virtual uint64_t nowMs() = 0;
    virtual void sleepMs(unsigned ms) = 0;
    virtual void notify(const std::string &text) = 0;   // system toast / log
    // Starts a detached worker thread (PS4: scePthreadCreate). Returns false on failure.
    virtual bool startThread(void (*fn)(void *), void *arg, const char *name) = 0;
    // Called once packaged assets (fonts) are in memory. PS4: leaves the sandbox here (afterwards the
    // package mount /app0 is no longer reachable). Default: nothing.
    virtual void afterAssetsLoaded() {}
};

}  // namespace ml
