// Files through libkernel (sceKernelOpen/Getdents...), the API set used by our earlier console-tested builds.
#include <orbis/libkernel.h>

#include <cstring>

#include "platform/ps4/ps4_platform.hpp"

namespace ml::ps4 {

namespace {
constexpr int kRdOnly = 0x0000, kWrOnly = 0x0001, kCreat = 0x0200, kTrunc = 0x0400, kDir = 0x00020000;
constexpr uint32_t kEExist = 0x80020011u;

struct DirentBsd {
    uint32_t fileno;
    uint16_t reclen;
    uint8_t type;
    uint8_t namlen;
    char name[256];
};
}  // namespace

bool Files::readFile(const std::string &path, std::string &out, size_t maxBytes)
{
    int fd = sceKernelOpen(path.c_str(), kRdOnly, 0);
    if (fd < 0) return false;
    out.clear();
    char buf[65536];
    while (out.size() < maxBytes) {
        int64_t n = int64_t(sceKernelRead(fd, buf, sizeof buf));
        if (n <= 0) break;
        out.append(buf, size_t(std::min<int64_t>(n, int64_t(maxBytes - out.size()))));
    }
    sceKernelClose(fd);
    return true;
}

bool Files::writeFile(const std::string &path, const std::string &data)
{
    std::string tmp = path + ".tmp";
    int fd = sceKernelOpen(tmp.c_str(), kWrOnly | kCreat | kTrunc, 0666);
    if (fd < 0) return false;
    size_t off = 0;
    bool ok = true;
    while (off < data.size()) {
        int64_t n = int64_t(sceKernelWrite(fd, data.data() + off, data.size() - off));
        if (n <= 0) { ok = false; break; }
        off += size_t(n);
    }
    sceKernelClose(fd);
    if (!ok) { sceKernelUnlink(tmp.c_str()); return false; }
    return sceKernelRename(tmp.c_str(), path.c_str()) == 0;
}

bool Files::listDir(const std::string &path, std::vector<DirEntry> &out)
{
    out.clear();
    int fd = sceKernelOpen(path.c_str(), kDir, 0);
    if (fd < 0) return false;
    std::vector<char> buf(8192);
    for (;;) {
        int n = sceKernelGetdents(fd, buf.data(), int(buf.size()));
        if (n <= 0) break;
        for (int off = 0; off < n;) {
            auto *d = reinterpret_cast<DirentBsd *>(buf.data() + off);
            if (d->reclen == 0) break;
            off += d->reclen;
            std::string name(d->name, d->namlen);
            if (name == "." || name == "..") continue;
            DirEntry e;
            e.name = name;
            OrbisKernelStat st;
            if (sceKernelStat((path + "/" + name).c_str(), &st) == 0) {
                e.isDir = (st.st_mode & 0170000) == 0040000;
                e.size = uint64_t(st.st_size);
            } else {
                e.isDir = d->type == 4;
            }
            out.push_back(std::move(e));
        }
    }
    sceKernelClose(fd);
    return true;
}

bool Files::makeDirs(const std::string &path)
{
    for (size_t i = 1; i <= path.size(); i++) {
        if (i == path.size() || path[i] == '/') {
            int r = sceKernelMkdir(path.substr(0, i).c_str(), 0777);
            if (r < 0 && uint32_t(r) != kEExist && i == path.size()) return false;
        }
    }
    return true;
}

bool Files::exists(const std::string &path)
{
    OrbisKernelStat st;
    return sceKernelStat(path.c_str(), &st) == 0;
}

}  // namespace ml::ps4
