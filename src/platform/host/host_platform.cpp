#include "platform/host/host_platform.hpp"

#include <arpa/inet.h>
#include <dirent.h>
#include <ifaddrs.h>
#include <poll.h>
#include <sys/random.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <thread>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

extern "C" int ml_platform_random(unsigned char *buf, size_t len)
{
    while (len > 0) {
        ssize_t n = getrandom(buf, len, 0);
        if (n <= 0) return -1;
        buf += n;
        len -= size_t(n);
    }
    return 0;
}

namespace ml::host {

// ---------------------------------------------------------------- display
HostDisplay::HostDisplay(int w, int h) : w_(w), h_(h), pixels_(static_cast<size_t>(w) * h, 0xFF000000u) {}

void HostDisplay::present()
{
    frames_++;
    if (!autoSaveDir.empty()) {
        char name[64];
        std::snprintf(name, sizeof name, "/frame_%04d.png", frames_);
        savePng(autoSaveDir + name);
    }
}

bool HostDisplay::savePng(const std::string &path) const
{
    std::vector<unsigned char> rgb(static_cast<size_t>(w_) * h_ * 3);
    for (size_t i = 0; i < pixels_.size(); i++) {
        uint32_t p = pixels_[i];
        rgb[i * 3] = (p >> 16) & 0xFF;
        rgb[i * 3 + 1] = (p >> 8) & 0xFF;
        rgb[i * 3 + 2] = p & 0xFF;
    }
    return stbi_write_png(path.c_str(), w_, h_, 3, rgb.data(), w_ * 3) != 0;
}

// ---------------------------------------------------------------- files
std::string HostFileSystem::map(const std::string &path) const
{
    static const std::string kAssets = "/app0/assets";
    if (!assetsRoot.empty() && path.compare(0, kAssets.size(), kAssets) == 0) return assetsRoot + path.substr(kAssets.size());
    return root_ + path;
}

bool HostFileSystem::readFile(const std::string &path, std::string &out, size_t maxBytes)
{
    FILE *f = std::fopen(map(path).c_str(), "rb");
    if (!f) return false;
    out.clear();
    char buf[65536];
    size_t n;
    while (out.size() < maxBytes && (n = std::fread(buf, 1, sizeof buf, f)) > 0)
        out.append(buf, std::min(n, maxBytes - out.size()));
    std::fclose(f);
    return true;
}

bool HostFileSystem::writeFile(const std::string &path, const std::string &data)
{
    std::string tmp = map(path) + ".tmp";
    FILE *f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    ok = (std::fclose(f) == 0) && ok;
    if (!ok) { std::remove(tmp.c_str()); return false; }
    return std::rename(tmp.c_str(), map(path).c_str()) == 0;
}

bool HostFileSystem::listDir(const std::string &path, std::vector<DirEntry> &out)
{
    out.clear();
    DIR *d = opendir(map(path).c_str());
    if (!d) return false;
    while (dirent *e = readdir(d)) {
        if (!std::strcmp(e->d_name, ".") || !std::strcmp(e->d_name, "..")) continue;
        struct stat st;
        std::string full = map(path) + "/" + e->d_name;
        if (stat(full.c_str(), &st) != 0) continue;
        out.push_back({e->d_name, S_ISDIR(st.st_mode), static_cast<uint64_t>(st.st_size)});
    }
    closedir(d);
    return true;
}

bool HostFileSystem::makeDirs(const std::string &path)
{
    std::string full = map(path);
    for (size_t i = root_.size() + 1; i <= full.size(); i++) {
        if (i == full.size() || full[i] == '/') {
            std::string part = full.substr(0, i);
            if (mkdir(part.c_str(), 0777) != 0 && errno != EEXIST) return false;
        }
    }
    return true;
}

bool HostFileSystem::exists(const std::string &path)
{
    struct stat st;
    return stat(map(path).c_str(), &st) == 0;
}

// ---------------------------------------------------------------- sockets (POSIX)
namespace {
class PosixConn : public TcpConn {
public:
    int fd;
    std::string err;
    explicit PosixConn(int f) : fd(f) {}
    ~PosixConn() override { if (fd >= 0) close(fd); }
    long send(const void *buf, size_t len) override
    {
        ssize_t n = ::send(fd, buf, len, MSG_NOSIGNAL);
        if (n < 0) err = (errno == EAGAIN || errno == EWOULDBLOCK) ? "tempo esgotado" : std::strerror(errno);
        return long(n);
    }
    long recv(void *buf, size_t len) override
    {
        ssize_t n = ::recv(fd, buf, len, 0);
        if (n < 0) err = (errno == EAGAIN || errno == EWOULDBLOCK) ? "tempo esgotado" : std::strerror(errno);
        return long(n);
    }
    std::string lastError() const override { return err; }
};

void setTimeouts(int fd, int ms)
{
    timeval tv{ms / 1000, (ms % 1000) * 1000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
}

class PosixListener : public TcpListener {
public:
    int fd;
    explicit PosixListener(int f) : fd(f) {}
    ~PosixListener() override { close(fd); }
    std::unique_ptr<TcpConn> accept(int timeoutMs, std::string *peer) override
    {
        pollfd p{fd, POLLIN, 0};
        if (poll(&p, 1, timeoutMs) <= 0) return nullptr;
        sockaddr_in a{};
        socklen_t al = sizeof a;
        int c = ::accept(fd, reinterpret_cast<sockaddr *>(&a), &al);
        if (c < 0) return nullptr;
        setTimeouts(c, 10000);
        if (peer) {
            char b[INET_ADDRSTRLEN] = "";
            inet_ntop(AF_INET, &a.sin_addr, b, sizeof b);
            *peer = b;
        }
        return std::unique_ptr<TcpConn>(new PosixConn(c));
    }
};
}  // namespace

std::unique_ptr<TcpConn> HostSockets::connect(const std::string &host, int port, int timeoutMs, std::string *err)
{
    addrinfo hints{}, *res = nullptr;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) {
        if (err) *err = "DNS falhou";
        return nullptr;
    }
    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd >= 0) setTimeouts(fd, timeoutMs);
    bool ok = fd >= 0 && ::connect(fd, res->ai_addr, res->ai_addrlen) == 0;
    freeaddrinfo(res);
    if (!ok) {
        if (err) *err = std::string("conexao recusada (") + std::strerror(errno) + ")";
        if (fd >= 0) close(fd);
        return nullptr;
    }
    return std::unique_ptr<TcpConn>(new PosixConn(fd));
}

std::unique_ptr<TcpListener> HostSockets::listen(int port, std::string *err)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(uint16_t(port));
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (fd < 0 || bind(fd, reinterpret_cast<sockaddr *>(&a), sizeof a) != 0 || ::listen(fd, 8) != 0) {
        if (err) *err = std::string("porta ocupada (") + std::strerror(errno) + ")";
        if (fd >= 0) close(fd);
        return nullptr;
    }
    return std::unique_ptr<TcpListener>(new PosixListener(fd));
}

std::string HostSockets::localAddress()
{
    ifaddrs *list = nullptr;
    std::string out;
    if (getifaddrs(&list) != 0) return out;
    for (ifaddrs *i = list; i; i = i->ifa_next) {
        if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET) continue;
        char b[INET_ADDRSTRLEN] = "";
        inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in *>(i->ifa_addr)->sin_addr, b, sizeof b);
        if (std::strncmp(b, "127.", 4) != 0) { out = b; break; }
    }
    freeifaddrs(list);
    return out.empty() ? "127.0.0.1" : out;
}

// ---------------------------------------------------------------- audio
namespace {
class HostAudioOut : public AudioOut {
public:
    HostAudioOut(bool realtime, std::string dump) : realtime_(realtime), dumpPath_(std::move(dump)) {}
    bool open() override
    {
        if (!dumpPath_.empty()) f_ = std::fopen(dumpPath_.c_str(), "ab");
        return true;
    }
    int framesPerBlock() const override { return 1024; }
    bool write(const int16_t *s) override
    {
        if (f_) std::fwrite(s, 4, 1024, f_);
        std::this_thread::sleep_for(std::chrono::microseconds(realtime_ ? 21333 : 2000));
        return true;
    }
    void close() override
    {
        if (f_) std::fclose(f_);
        f_ = nullptr;
    }

private:
    bool realtime_;
    std::string dumpPath_;
    FILE *f_ = nullptr;
};
}  // namespace

// ---------------------------------------------------------------- platform
HostPlatform::HostPlatform(std::string sandbox, std::string assetsRoot, int w, int h) : display_(w, h), fs_(std::move(sandbox))
{
    fs_.assetsRoot = std::move(assetsRoot);
}

std::unique_ptr<AudioOut> HostPlatform::makeAudioOut()
{
    return std::unique_ptr<AudioOut>(new HostAudioOut(realtimeAudio, audioDumpPath));
}

uint64_t HostPlatform::nowMs()
{
    using namespace std::chrono;
    return static_cast<uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

void HostPlatform::sleepMs(unsigned ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

void HostPlatform::notify(const std::string &text)
{
    notifications.push_back(text);
    std::fprintf(stderr, "[notify] %s\n", text.c_str());
}

bool HostPlatform::startThread(void (*fn)(void *), void *arg, const char *)
{
    std::thread(fn, arg).detach();
    return true;
}

}  // namespace ml::host
