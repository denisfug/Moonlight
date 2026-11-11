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
    // Why the last write/makeDirs failed (step + system code), "" if unknown. Not thread-safe:
    // read it right after the failing call on the same thread.
    virtual std::string lastError() const { return ""; }
};

// ---------------------------------------------------------------- video
// Hardware/system video playback (PS4: libSceAvPlayer). Audio is played by the engine itself.
class VideoEngine {
public:
    virtual ~VideoEngine() = default;
    // Extra HTTP headers for an http(s) location (the source's proxyHeaders.request); call before open().
    virtual void setRequestHeaders(const std::vector<std::pair<std::string, std::string>> &) {}
    virtual bool open(const std::string &location, std::string *error) = 0;   // starts playing
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual void stop() = 0;
    virtual bool seek(int64_t ms) = 0;
    virtual int64_t positionMs() = 0;
    virtual int64_t durationMs() = 0;
    virtual bool paused() = 0;
    virtual bool finished() = 0;
    // If a new decoded frame is due, converts it into `fb` (scaled to fit) and returns true.
    virtual bool updateFrame(Framebuffer fb) = 0;
    virtual std::string status() = 0;   // diagnostics shown in the player overlay
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
    virtual Sockets *sockets() { return nullptr; } // raw TCP (phone remote server); nullptr = unavailable
    // Installs a .pkg already on disk. 0 = installation started (system notification shows progress),
    // 1 = this app is already installed (call again with replaceInstalled), -1 = error in *err.
    virtual int installPackage(const std::string &path, bool replaceInstalled, std::string *err)
    {
        // 01.10 linked the system installer (libSceBgft/libSceAppInstUtil) and crashed on start on the
        // owner's console (CE-34878-0); until that is understood, installing is left to GoldHEN.
        if (err) *err = "instalacao automatica desativada por enquanto";
        return -1;
    }
    // Human-readable state of the network stack ("" = OK) and a way to try initialising it again.
    virtual std::string networkStatus() const { return ""; }
    virtual bool retryNetwork() { return http() != nullptr; }
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
    // System on-screen keyboard (PS4: IME dialog). start returns false when unavailable (the app then
    // uses its own keyboard). poll: 0 = still open, 1 = confirmed (text in *out), -1 = cancelled/failed.
    virtual bool startTextInput(const std::string &title, const std::string &initial, bool url) { return false; }
    virtual int pollTextInput(std::string *out) { return -1; }
    // nullptr = no video engine on this platform (the PC test build).
    virtual std::unique_ptr<VideoEngine> makeVideoEngine() { return nullptr; }
    virtual std::vector<std::string> videoDirs() const { return {}; }
};

}  // namespace ml
