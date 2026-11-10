// See tls.hpp. No C++ objects with constructors at namespace scope (the PS4 binary has no .init_array):
// global state is plain C structs, zero-initialised, set up lazily under a spinlock.
#include "net/tls.hpp"

#include <cstdio>
#include <cstring>

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/ssl.h"
#include "mbedtls/threading.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"

namespace {
#include "net/ca_roots.inc"

// ---------------------------------------------------------------- spinlock mutexes for Mbed TLS
void spinLock(volatile int *l)
{
    while (__atomic_exchange_n(l, 1, __ATOMIC_ACQUIRE)) {
        while (__atomic_load_n(l, __ATOMIC_RELAXED)) __builtin_ia32_pause();
    }
}
void spinUnlock(volatile int *l) { __atomic_store_n(l, 0, __ATOMIC_RELEASE); }

void mtxInit(mbedtls_threading_mutex_t *m) { m->lock = 0; m->valid = 1; }
void mtxFree(mbedtls_threading_mutex_t *m) { m->valid = 0; }
int mtxLock(mbedtls_threading_mutex_t *m)
{
    if (!m->valid) return MBEDTLS_ERR_THREADING_BAD_INPUT_DATA;
    spinLock(&m->lock);
    return 0;
}
int mtxUnlock(mbedtls_threading_mutex_t *m)
{
    if (!m->valid) return MBEDTLS_ERR_THREADING_BAD_INPUT_DATA;
    spinUnlock(&m->lock);
    return 0;
}

// ---------------------------------------------------------------- global state (plain C structs)
volatile int gInitLock;
int gState;              // 0 not yet, 1 ready, -1 failed
int gRoots;              // roots parsed
char gInitErr[160];
mbedtls_x509_crt gChain;
const char *gExtraPem;   // tests only

std::string hex(int v)
{
    char b[16];
    std::snprintf(b, sizeof b, "-0x%04X", unsigned(-v));
    return b;
}

std::string mbedErr(int ret)
{
    char b[128];
    mbedtls_strerror(ret, b, sizeof b);
    return std::string(b) + " (" + hex(ret) + ")";
}

bool ensureInit(std::string *err)
{
    spinLock(&gInitLock);
    if (gState == 0) {
        mbedtls_threading_set_alt(mtxInit, mtxFree, mtxLock, mtxUnlock);
        psa_status_t ps = psa_crypto_init();
        if (ps != PSA_SUCCESS) {
            std::snprintf(gInitErr, sizeof gInitErr, "psa_crypto_init %d (gerador aleatorio do sistema?)", int(ps));
            gState = -1;
        } else {
            mbedtls_x509_crt_init(&gChain);
            for (int i = 0; i < ML_CA_ROOT_COUNT; ++i) {
                // PEM parsing wants the terminating NUL inside the length.
                if (mbedtls_x509_crt_parse(&gChain, reinterpret_cast<const unsigned char *>(kCaRoots[i]), kCaRootSizes[i] + 1) == 0) ++gRoots;
            }
            if (gExtraPem && mbedtls_x509_crt_parse(&gChain, reinterpret_cast<const unsigned char *>(gExtraPem), std::strlen(gExtraPem) + 1) == 0) ++gRoots;
            if (gRoots == 0) {
                std::snprintf(gInitErr, sizeof gInitErr, "nenhum certificado raiz carregou");
                gState = -1;
            } else {
                gState = 1;
            }
        }
    }
    int st = gState;
    spinUnlock(&gInitLock);
    if (st != 1 && err) *err = gInitErr;
    return st == 1;
}

int bioSend(void *ctx, const unsigned char *buf, size_t len)
{
    long n = static_cast<ml::TcpConn *>(ctx)->send(buf, len);
    if (n < 0) return -0x004E;   // MBEDTLS_ERR_NET_SEND_FAILED
    return int(n);
}

int bioRecv(void *ctx, unsigned char *buf, size_t len)
{
    long n = static_cast<ml::TcpConn *>(ctx)->recv(buf, len);
    if (n < 0) return -0x004C;   // MBEDTLS_ERR_NET_RECV_FAILED
    if (n == 0) return -0x0050;  // MBEDTLS_ERR_NET_CONN_RESET
    return int(n);
}

std::string verifyText(uint32_t flags)
{
    std::string s;
    auto add = [&](const char *t) { if (!s.empty()) s += ", "; s += t; };
    if (flags & MBEDTLS_X509_BADCERT_EXPIRED) add("certificado vencido (confira a data do console)");
    if (flags & MBEDTLS_X509_BADCERT_FUTURE) add("certificado ainda nao valido (data do console errada?)");
    if (flags & MBEDTLS_X509_BADCERT_CN_MISMATCH) add("certificado e de outro site");
    if (flags & MBEDTLS_X509_BADCERT_NOT_TRUSTED) add("emissor do certificado desconhecido");
    if (flags & MBEDTLS_X509_BADCERT_REVOKED) add("certificado revogado");
    if (s.empty()) {
        char b[256];
        mbedtls_x509_crt_verify_info(b, sizeof b, "", flags);
        s = b;
        while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) s.pop_back();
    }
    return "certificado recusado: " + s;
}

class TlsConn : public ml::TcpConn {
public:
    std::unique_ptr<ml::TcpConn> tcp;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    std::string err;

    TlsConn()
    {
        mbedtls_ssl_init(&ssl);
        mbedtls_ssl_config_init(&conf);
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&drbg);
    }
    ~TlsConn() override
    {
        if (tcp) mbedtls_ssl_close_notify(&ssl);
        mbedtls_ssl_free(&ssl);
        mbedtls_ssl_config_free(&conf);
        mbedtls_ctr_drbg_free(&drbg);
        mbedtls_entropy_free(&entropy);
    }

    bool handshake(const std::string &host)
    {
        static const char kPers[] = "moonlight-tls";
        int ret = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, reinterpret_cast<const unsigned char *>(kPers), sizeof kPers - 1);
        if (ret != 0) { err = "gerador aleatorio: " + mbedErr(ret); return false; }
        ret = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
        if (ret != 0) { err = "config TLS: " + mbedErr(ret); return false; }
        mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
        mbedtls_ssl_conf_ca_chain(&conf, &gChain, nullptr);
        mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &drbg);
        ret = mbedtls_ssl_setup(&ssl, &conf);
        if (ret != 0) { err = "setup TLS: " + mbedErr(ret); return false; }
        ret = mbedtls_ssl_set_hostname(&ssl, host.c_str());
        if (ret != 0) { err = "nome do site: " + mbedErr(ret); return false; }
        mbedtls_ssl_set_bio(&ssl, tcp.get(), bioSend, bioRecv, nullptr);
        while ((ret = mbedtls_ssl_handshake(&ssl)) != 0) {
            if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
            uint32_t flags = mbedtls_ssl_get_verify_result(&ssl);
            if (ret == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED && flags != 0 && flags != uint32_t(-1)) err = verifyText(flags);
            else if (ret == -0x004C || ret == -0x0050) err = "conexao caiu durante o TLS (" + tcp->lastError() + ")";
            else err = "handshake TLS: " + mbedErr(ret);
            return false;
        }
        return true;
    }

    long send(const void *buf, size_t len) override
    {
        const unsigned char *p = static_cast<const unsigned char *>(buf);
        size_t done = 0;
        while (done < len) {
            int n = mbedtls_ssl_write(&ssl, p + done, len - done);
            if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
            if (n < 0) { err = "envio TLS: " + mbedErr(n); return -1; }
            done += size_t(n);
        }
        return long(done);
    }

    long recv(void *buf, size_t len) override
    {
        for (;;) {
            int n = mbedtls_ssl_read(&ssl, static_cast<unsigned char *>(buf), len);
            if (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
#ifdef MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET
            if (n == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) continue;
#endif
            if (n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || n == 0) return 0;
            // Many servers just close the socket after the body instead of sending close_notify.
            if (n == -0x0050) return 0;
            if (n < 0) { err = (n == -0x004C ? "leitura: " + tcp->lastError() : "leitura TLS: " + mbedErr(n)); return -1; }
            return n;
        }
    }

    std::string lastError() const override { return err; }
};
}  // namespace

// Entropy source for Mbed TLS (MBEDTLS_ENTROPY_HARDWARE_ALT). A failure makes TLS unusable: no fallback seed.
extern "C" int mbedtls_hardware_poll(void *, unsigned char *output, size_t len, size_t *olen)
{
    if (ml_platform_random(output, len) != 0) { *olen = 0; return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED; }
    *olen = len;
    return 0;
}

namespace ml::tls {

std::unique_ptr<TcpConn> connect(std::unique_ptr<TcpConn> tcp, const std::string &host, std::string *err)
{
    std::string ie;
    if (!ensureInit(&ie)) { if (err) *err = "TLS nao iniciou: " + ie; return nullptr; }
    std::unique_ptr<TlsConn> c(new TlsConn);
    c->tcp = std::move(tcp);
    if (!c->handshake(host)) {
        if (err) *err = c->err;
        c->tcp.reset();   // no close_notify on a failed handshake
        return nullptr;
    }
    return std::unique_ptr<TcpConn>(c.release());
}

int trustedRoots(std::string *err)
{
    return ensureInit(err) ? gRoots : 0;
}

bool addTrustedRootForTests(const std::string &pem)
{
    if (gState != 0) return false;
    // Leaked on purpose: lives as long as the process, like the bundled roots.
    char *copy = new char[pem.size() + 1];
    std::memcpy(copy, pem.c_str(), pem.size() + 1);
    gExtraPem = copy;
    return true;
}

}  // namespace ml::tls
