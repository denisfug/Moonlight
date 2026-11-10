// PS4 (OpenOrbis + GoldHEN) implementation of the platform. The only place that includes orbis/*.
// Status: compiles and links for the PS4 target; NOT yet validated on a console (docs/COMPATIBILITY.md).
#pragma once
#include <string>
#include <vector>

#include "net/web_client.hpp"
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

// libSceNet sockets + DNS; HTTP and TLS on top are the app's own (src/net).
class Net : public Sockets {
public:
    bool init(std::string *err);
    std::unique_ptr<TcpConn> connect(const std::string &host, int port, int timeoutMs, std::string *err) override;
    std::unique_ptr<TcpListener> listen(int port, std::string *err) override;
    std::string localAddress() override;

private:
    int pool_ = -1;
    bool ctl_ = false;
};

class Ps4Platform : public Platform {
public:
    bool init(std::string *err);
    std::string name() const override { return "ps4"; }
    Display &display() override { return video_; }
    Input &input() override { return pad_; }
    FileSystem &fs() override { return files_; }
    std::unique_ptr<AudioOut> makeAudioOut() override { return ps4::makeAudioOut(); }
    HttpClient *http() override { return &web_; }   // always the same object; requests fail cleanly until init works
    Sockets *sockets() override { return &net_; }
    WebClient &web() { return web_; }
    std::string networkStatus() const override { return httpOk_ ? "" : netErr_; }
    bool retryNetwork() override;
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
    Net net_;
    WebClient web_{&net_, "MoonlightHB/" ML_VERSION " (PlayStation 4)"};
    bool httpOk_ = false;
    std::string netErr_;
    bool jailbroken_ = false;
    bool imeReady_ = false;
    uint16_t imeText_[2049];
    uint16_t imeTitle_[128];
};

}  // namespace ml::ps4
