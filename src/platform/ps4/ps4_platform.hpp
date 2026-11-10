// PS4 (OpenOrbis + GoldHEN) implementation of the platform. The only place that includes orbis/*.
// Status: compiles and links for the PS4 target; NOT yet validated on a console (docs/COMPATIBILITY.md).
#pragma once
#include <string>
#include <vector>

#include "platform/platform.hpp"

namespace ml::ps4 {

class VideoOut : public Display {
public:
    bool init(std::string *err);   // 1920x1080, two linear ARGB8888 buffers
    Framebuffer backBuffer() override;
    void present() override;

private:
    int handle_ = -1;
    void *flipQueue_ = nullptr;   // OrbisKernelEqueue
    uint32_t *buf_[2] = {nullptr, nullptr};
    int cur_ = 0;
    int64_t frame_ = 1;
};

class Pad : public Input {
public:
    bool init(std::string *err);
    PadState read() override;

private:
    int handle_ = -1;
};

class Files : public FileSystem {
public:
    bool readFile(const std::string &path, std::string &out, size_t maxBytes) override;
    bool writeFile(const std::string &path, const std::string &data) override;
    bool listDir(const std::string &path, std::vector<DirEntry> &out) override;
    bool makeDirs(const std::string &path) override;
    bool exists(const std::string &path) override;
};

std::unique_ptr<AudioOut> makeAudioOut();

class Http : public HttpClient {
public:
    bool init(std::string *err);
    HttpResponse get(const HttpRequest &req) override;
    std::unique_ptr<HttpStream> openStream(const HttpRequest &req, std::string *error) override;
    bool supportsHttps() const override { return true; }
    std::string name() const override { return "ps4-sceHttp"; }

private:
    int ctx_ = -1;
};

class Ps4Platform : public Platform {
public:
    bool init(std::string *err);
    std::string name() const override { return "ps4"; }
    Display &display() override { return video_; }
    Input &input() override { return pad_; }
    FileSystem &fs() override { return files_; }
    std::unique_ptr<AudioOut> makeAudioOut() override { return ps4::makeAudioOut(); }
    HttpClient *http() override { return httpOk_ ? &http_ : nullptr; }
    std::string dataDir() const override { return "/data/moonlight"; }
    std::string assetDir() const override { return "/app0/assets"; }
    std::vector<std::string> musicDirs() const override;
    uint64_t nowMs() override;
    void sleepMs(unsigned ms) override;
    void notify(const std::string &text) override;
    bool startThread(void (*fn)(void *), void *arg, const char *name) override;
    void afterAssetsLoaded() override;   // GoldHEN jailbreak for /data and USB access
    std::unique_ptr<VideoEngine> makeVideoEngine() override;   // libSceAvPlayer (experimental)
    std::vector<std::string> videoDirs() const override;
    bool startTextInput(const std::string &title, const std::string &initial, bool url) override;
    int pollTextInput(std::string *out) override;

private:
    VideoOut video_;
    Pad pad_;
    Files files_;
    Http http_;
    bool httpOk_ = false;
    bool jailbroken_ = false;
    bool imeReady_ = false;
    uint16_t imeText_[2049];
    uint16_t imeTitle_[128];
};

}  // namespace ml::ps4
