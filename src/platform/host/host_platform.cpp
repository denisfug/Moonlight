#include "platform/host/host_platform.hpp"

#include <arpa/inet.h>
#include <dirent.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

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

// ---------------------------------------------------------------- http (plain, for local tests)
HttpResponse HostHttp::get(const HttpRequest &req)
{
    HttpResponse r;
    Url u = parseUrl(req.url);
    if (!u.valid) { r.error = "URL invalida"; return r; }
    if (u.scheme != "http") { r.error = "https nao suportado no cliente de teste"; return r; }
    addrinfo hints{}, *res = nullptr;
    hints.ai_socktype = SOCK_STREAM;
    std::string host = u.host;
    if (!host.empty() && host[0] == '[') host = host.substr(1, host.size() - 2);
    if (getaddrinfo(host.c_str(), std::to_string(u.port).c_str(), &hints, &res) != 0 || !res) {
        r.error = "DNS falhou";
        return r;
    }
    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    timeval tv{req.timeoutMs / 1000, (req.timeoutMs % 1000) * 1000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    if (fd < 0 || connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
        freeaddrinfo(res);
        if (fd >= 0) close(fd);
        r.error = "conexao recusada";
        return r;
    }
    freeaddrinfo(res);
    std::string q = "GET " + u.path + (u.query.empty() ? "" : "?" + u.query) + " HTTP/1.1\r\nHost: " + u.host +
                    "\r\nConnection: close\r\nUser-Agent: MoonlightHB/0.1\r\n";
    for (const auto &h : req.headers) q += h.first + ": " + h.second + "\r\n";
    q += "\r\n";
    send(fd, q.data(), q.size(), 0);
    std::string raw;
    char buf[16384];
    const size_t cap = req.maxBytes + 64 * 1024;
    ssize_t n;
    while ((n = recv(fd, buf, sizeof buf, 0)) > 0) {
        raw.append(buf, static_cast<size_t>(n));
        if (raw.size() > cap) { close(fd); r.error = "resposta maior que o limite"; return r; }
    }
    close(fd);
    if (n < 0 && raw.empty()) { r.error = "tempo esgotado"; return r; }
    size_t he = raw.find("\r\n\r\n");
    if (he == std::string::npos || raw.compare(0, 5, "HTTP/") != 0) { r.error = "resposta HTTP invalida"; return r; }
    std::string head = raw.substr(0, he);
    std::string body = raw.substr(he + 4);
    r.status = std::atoi(head.c_str() + head.find(' ') + 1);
    bool chunked = false;
    for (size_t p = head.find("\r\n"); p != std::string::npos; p = head.find("\r\n", p + 2)) {
        size_t e = head.find("\r\n", p + 2);
        std::string line = head.substr(p + 2, e == std::string::npos ? std::string::npos : e - p - 2);
        std::string low = line;
        for (auto &c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (low.compare(0, 13, "content-type:") == 0) r.contentType = line.substr(13);
        if (low.find("transfer-encoding:") == 0 && low.find("chunked") != std::string::npos) chunked = true;
    }
    if (chunked) {
        std::string out;
        size_t p = 0;
        for (;;) {
            size_t le = body.find("\r\n", p);
            if (le == std::string::npos) { r.error = "chunked invalido"; return r; }
            size_t len = std::strtoul(body.substr(p, le - p).c_str(), nullptr, 16);
            p = le + 2;
            if (len == 0) break;
            if (p + len > body.size()) { r.error = "chunked truncado"; return r; }
            out.append(body, p, len);
            p += len + 2;
        }
        body.swap(out);
    }
    if (body.size() > req.maxBytes) { r.error = "resposta maior que o limite"; return r; }
    r.body = std::move(body);
    return r;
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
