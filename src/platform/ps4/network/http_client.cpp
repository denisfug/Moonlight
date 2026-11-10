// PS4 network: raw TCP through libSceNet (sockets + DNS resolver) under the app's own HTTP/TLS client
// (src/net). The system HTTP/TLS libraries (libSceHttp/libSceSsl) are no longer used: on the owner's
// console every HTTPS request failed in sceHttpSendRequest with 0x8095F00C (01.07), and adding current
// roots with sceHttpsLoadCert (01.08) did not change it. Certificate verification is done by Mbed TLS.
// Interface facts (socket option numbers, sockaddr layout, sceRandomGetRandomNumber signature) come
// from the shadPS4 emulator sources (src/core/libraries/net, random); no code was copied.
// Status: compiles for PS4; NOT yet tested on a console.
#include <cstddef>
#include <cstdint>

#include <orbis/Net.h>
#include <orbis/NetCtl.h>
#include <orbis/Sysmodule.h>

#include <cstdio>
#include <cstring>

#include "platform/ps4/ps4_platform.hpp"

// OpenOrbis declares this without parameters; signature per shadPS4 (random.h): at most 64 bytes per call.
extern "C" int32_t sceRandomGetRandomNumber(uint8_t *buf, size_t size);

extern "C" int ml_platform_random(unsigned char *buf, size_t len)
{
    while (len > 0) {
        size_t n = len > 64 ? 64 : len;
        if (sceRandomGetRandomNumber(buf, n) < 0) return -1;
        buf += n;
        len -= n;
    }
    return 0;
}

namespace ml::ps4 {

namespace {
// Values from shadPS4 src/core/libraries/net/net_types.h (Sony numbering, not BSD's).
constexpr int kSolSocket = 0xFFFF;
constexpr int kSoReuseAddr = 0x0004;
constexpr int kSoSndTimeo = 0x1105;      // int, microseconds
constexpr int kSoRcvTimeo = 0x1106;
constexpr int kSoAcceptTimeo = 0x1108;
constexpr int kSoConnectTimeo = 0x1109;

struct SockaddrIn {      // OrbisNetSockaddrIn
    uint8_t len;
    uint8_t family;
    uint16_t port;       // network byte order
    uint32_t addr;       // network byte order
    uint16_t vport;
    char zero[6];
};
static_assert(sizeof(SockaddrIn) == 16, "sockaddr_in layout");

std::string hex(int v)
{
    char b[16];
    std::snprintf(b, sizeof b, "0x%08X", unsigned(v));
    return b;
}

// Sony codes 0x8041xxxx: the low byte is the BSD errno.
std::string netError(int code)
{
    switch (unsigned(code)) {
    case 0x8041013Cu: return "tempo esgotado";
    case 0x8041013Du: return "conexao recusada";
    case 0x80410136u: return "conexao reiniciada pelo servidor";
    case 0x80410141u: return "sem rota para o servidor";
    case 0x80410133u: return "rede inacessivel";
    default: return "erro " + hex(code);
    }
}

void setTimeouts(int s, int ms)
{
    int us = ms * 1000;
    sceNetSetsockopt(s, kSolSocket, kSoRcvTimeo, &us, sizeof us);
    sceNetSetsockopt(s, kSolSocket, kSoSndTimeo, &us, sizeof us);
}

class Ps4Conn : public TcpConn {
public:
    int s;
    std::string err;
    explicit Ps4Conn(int sock) : s(sock) {}
    ~Ps4Conn() override { sceNetSocketClose(s); }
    long send(const void *buf, size_t len) override
    {
        int n = sceNetSend(s, buf, len, 0);
        if (n < 0) { err = netError(n); return -1; }
        return n;
    }
    long recv(void *buf, size_t len) override
    {
        int n = sceNetRecv(s, buf, len, 0);
        if (n < 0) { err = netError(n); return -1; }
        return n;
    }
    std::string lastError() const override { return err; }
};

class Ps4Listener : public TcpListener {
public:
    int s;
    explicit Ps4Listener(int sock) : s(sock) {}
    ~Ps4Listener() override { sceNetSocketClose(s); }
    std::unique_ptr<TcpConn> accept(int timeoutMs, std::string *peer) override
    {
        int us = timeoutMs * 1000;
        sceNetSetsockopt(s, kSolSocket, kSoAcceptTimeo, &us, sizeof us);
        SockaddrIn a{};
        OrbisNetSocklen_t al = sizeof a;
        int c = sceNetAccept(s, reinterpret_cast<OrbisNetSockaddr *>(&a), &al);
        if (c < 0) return nullptr;
        setTimeouts(c, 10000);
        if (peer) {
            const uint8_t *ip = reinterpret_cast<const uint8_t *>(&a.addr);
            char b[20];
            std::snprintf(b, sizeof b, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
            *peer = b;
        }
        return std::unique_ptr<TcpConn>(new Ps4Conn(c));
    }
};
}  // namespace

bool Net::init(std::string *err)
{
    int r = sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET);
    if (r < 0) { *err = "carregar modulo NET " + hex(r); return false; }
    if (pool_ < 0) {
        sceNetInit();
        int p = sceNetPoolCreate("ml_net", 16 * 1024, 0);
        if (p < 0) { *err = "sceNetPoolCreate " + hex(p); return false; }
        pool_ = p;
    }
    if (!ctl_ && sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NETCTL) >= 0 && sceNetCtlInit() >= 0) ctl_ = true;
    return true;
}

std::unique_ptr<TcpConn> Net::connect(const std::string &host, int port, int timeoutMs, std::string *err)
{
    if (pool_ < 0) {
        std::string ie;
        if (!init(&ie)) { if (err) *err = "rede do PS4 nao iniciou (" + ie + ")"; return nullptr; }
    }
    OrbisNetInAddr addr{};
    if (sceNetInetPton(ORBIS_NET_AF_INET, host.c_str(), &addr) != 1) {
        int rid = sceNetResolverCreate("ml_dns", pool_, 0);
        if (rid < 0) { if (err) *err = "DNS: resolver " + hex(rid); return nullptr; }
        int r = sceNetResolverStartNtoa(rid, host.c_str(), &addr, 0, 0, 0);
        sceNetResolverDestroy(rid);
        if (r < 0) { if (err) *err = "DNS nao encontrou o site (" + hex(r) + ")"; return nullptr; }
    }
    int s = sceNetSocket("ml_tcp", ORBIS_NET_AF_INET, ORBIS_NET_SOCK_STREAM, 0);
    if (s < 0) { if (err) *err = "socket " + hex(s); return nullptr; }
    int us = timeoutMs * 1000;
    sceNetSetsockopt(s, kSolSocket, kSoConnectTimeo, &us, sizeof us);
    setTimeouts(s, timeoutMs);
    SockaddrIn sa{};
    sa.len = sizeof sa;
    sa.family = ORBIS_NET_AF_INET;
    sa.port = sceNetHtons(uint16_t(port));
    sa.addr = addr.s_addr;
    int r = sceNetConnect(s, reinterpret_cast<OrbisNetSockaddr *>(&sa), sizeof sa);
    if (r < 0) {
        sceNetSocketClose(s);
        if (err) *err = netError(r);
        return nullptr;
    }
    return std::unique_ptr<TcpConn>(new Ps4Conn(s));
}

std::unique_ptr<TcpListener> Net::listen(int port, std::string *err)
{
    if (pool_ < 0) {
        std::string ie;
        if (!init(&ie)) { if (err) *err = ie; return nullptr; }
    }
    int s = sceNetSocket("ml_srv", ORBIS_NET_AF_INET, ORBIS_NET_SOCK_STREAM, 0);
    if (s < 0) { if (err) *err = "socket " + hex(s); return nullptr; }
    int one = 1;
    sceNetSetsockopt(s, kSolSocket, kSoReuseAddr, &one, sizeof one);
    SockaddrIn sa{};
    sa.len = sizeof sa;
    sa.family = ORBIS_NET_AF_INET;
    sa.port = sceNetHtons(uint16_t(port));
    sa.addr = 0;   // INADDR_ANY
    int r = sceNetBind(s, reinterpret_cast<OrbisNetSockaddr *>(&sa), sizeof sa);
    if (r >= 0) r = sceNetListen(s, 8);
    if (r < 0) {
        sceNetSocketClose(s);
        if (err) *err = "porta " + std::to_string(port) + ": " + netError(r);
        return nullptr;
    }
    return std::unique_ptr<TcpListener>(new Ps4Listener(s));
}

std::string Net::localAddress()
{
    if (!ctl_) return "";
    OrbisNetCtlInfo info;
    std::memset(&info, 0, sizeof info);
    if (sceNetCtlGetInfo(ORBIS_NET_CTL_INFO_IP_ADDRESS, &info) < 0) return "";
    info.ip_address[sizeof info.ip_address - 1] = 0;
    return info.ip_address;
}

}  // namespace ml::ps4
