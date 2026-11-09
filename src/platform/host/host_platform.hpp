// PC implementation of the platform, used for automated tests and screenshots.
// - Display: off-screen 1920x1080 buffer; present() can save PNG frames.
// - Input: scripted button events ("t_ms:BUTTONS" list) or set programmatically.
// - FileSystem: PS4-style absolute paths mapped under a sandbox directory.
// - HTTP: plain http:// only (for the local test addon server); https is reported unsupported.
#pragma once
#include <string>
#include <vector>

#include "platform/platform.hpp"

namespace ml::host {

class HostDisplay : public Display {
public:
    HostDisplay(int w, int h);
    Framebuffer backBuffer() override { return {pixels_.data(), w_, h_, w_}; }
    void present() override;
    bool savePng(const std::string &path) const;
    int frames() const { return frames_; }
    std::string autoSaveDir;   // when set, every present() writes frame_NNNN.png there

private:
    int w_, h_, frames_ = 0;
    std::vector<uint32_t> pixels_;
};

class ScriptedInput : public Input {
public:
    PadState read() override { return state; }
    PadState state{true, 0};
};

class HostFileSystem : public FileSystem {
public:
    explicit HostFileSystem(std::string sandbox) : root_(std::move(sandbox)) {}
    bool readFile(const std::string &path, std::string &out, size_t maxBytes) override;
    bool writeFile(const std::string &path, const std::string &data) override;
    bool listDir(const std::string &path, std::vector<DirEntry> &out) override;
    bool makeDirs(const std::string &path) override;
    bool exists(const std::string &path) override;
    // "/app0/assets/..." (packaged assets on PS4) maps to assetsRoot; everything else to the sandbox.
    std::string map(const std::string &path) const;
    std::string assetsRoot;

private:
    std::string root_;
};

class HostHttp : public HttpClient {
public:
    HttpResponse get(const HttpRequest &req) override;
    std::unique_ptr<HttpStream> openStream(const HttpRequest &req, std::string *error) override;
    bool supportsHttps() const override { return false; }
    std::string name() const override { return "host-http"; }
};

class HostPlatform : public Platform {
public:
    HostPlatform(std::string sandbox, std::string assetsRoot, int w = 1920, int h = 1080);
    std::string name() const override { return "host"; }
    Display &display() override { return display_; }
    Input &input() override { return input_; }
    FileSystem &fs() override { return fs_; }
    std::unique_ptr<AudioOut> makeAudioOut() override;
    HttpClient *http() override { return &http_; }
    std::string dataDir() const override { return "/data/moonlight"; }
    std::string assetDir() const override { return "/app0/assets"; }
    std::vector<std::string> musicDirs() const override { return {"/data/moonlight/music", "/mnt/usb0/Music"}; }
    uint64_t nowMs() override;
    void sleepMs(unsigned ms) override;
    void notify(const std::string &text) override;
    bool startThread(void (*fn)(void *), void *arg, const char *name) override;

    HostDisplay display_;
    ScriptedInput input_;
    HostFileSystem fs_;
    HostHttp http_;
    bool realtimeAudio = false;      // false: audio blocks are consumed ~10x faster (quick tests)
    std::string audioDumpPath;       // optional raw s16le stereo dump
    std::vector<std::string> notifications;
};

}  // namespace ml::host
