// HTTP/HTTPS through the system libraries (libSceNet + libSceSsl + libSceHttp), following the
// init order of the OpenOrbis net_http sample. Unlike the sample we do NOT install an SSL callback
// that accepts every certificate: the system's default server verification stays on.
// Status: compiles for PS4; not yet tested on a console.
#include <orbis/Http.h>
#include <orbis/Net.h>
#include <orbis/Ssl.h>
#include <orbis/Sysmodule.h>

#include <cstring>

#include "platform/ps4/ps4_platform.hpp"

namespace ml::ps4 {

namespace {
constexpr const char *kUserAgent = "MoonlightHB/1.0 (PlayStation 4)";

struct Request {
    int tpl = -1, conn = -1, req = -1;
    ~Request()
    {
        if (req >= 0) sceHttpDeleteRequest(req);
        if (conn >= 0) sceHttpDeleteConnection(conn);
        if (tpl >= 0) sceHttpDeleteTemplate(tpl);
    }
};

std::string hex(int v)
{
    char b[16];
    std::snprintf(b, sizeof b, "0x%08X", unsigned(v));
    return b;
}

class Ps4Stream : public HttpStream {
public:
    std::unique_ptr<Request> r;
    int code = 0;
    std::string ctype;
    long read(void *buf, size_t len) override
    {
        int n = sceHttpReadData(r->req, buf, uint32_t(len > 0x7FFFFFFF ? 0x7FFFFFFF : len));
        return n < 0 ? -1 : long(n);
    }
    int status() const override { return code; }
    std::string contentType() const override { return ctype; }
};
}  // namespace

bool Http::init(std::string *err)
{
    if (sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET) & 0x80000000u) { *err = "modulo de rede nao carregou"; return false; }
    if (sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_HTTP) & 0x80000000u) { *err = "modulo HTTP nao carregou"; return false; }
    if (sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SSL) & 0x80000000u) { *err = "modulo SSL nao carregou"; return false; }
    sceNetInit();
    int net = sceNetPoolCreate("ml_net", 16 * 1024, 0);
    if (net < 0) { *err = "sceNetPoolCreate " + hex(net); return false; }
    int ssl = sceSslInit(SSL_POOLSIZE * 2);
    if (ssl < 0) { *err = "sceSslInit " + hex(ssl); return false; }
    int http = sceHttpInit(net, ssl, LIBHTTP_POOLSIZE * 4);
    if (http < 0) { *err = "sceHttpInit " + hex(http); return false; }
    ctx_ = http;
    return true;
}

// Creates template/connection/request, sends it, and fills status. Returns "" or an error.
static std::string begin(int ctx, const HttpRequest &q, Request &r, int &status)
{
    r.tpl = sceHttpCreateTemplate(ctx, kUserAgent, ORBIS_HTTP_VERSION_1_1, 1);
    if (r.tpl < 0) return "template " + hex(r.tpl);
    uint32_t us = uint32_t(q.timeoutMs) * 1000u;
    sceHttpSetConnectTimeOut(r.tpl, us);
    sceHttpSetResolveTimeOut(r.tpl, us);
    sceHttpSetSendTimeOut(r.tpl, us);
    r.conn = sceHttpCreateConnectionWithURL(r.tpl, q.url.c_str(), true);
    if (r.conn < 0) return "conexao " + hex(r.conn);
    r.req = sceHttpCreateRequestWithURL(r.conn, ORBIS_METHOD_GET, q.url.c_str(), 0);
    if (r.req < 0) return "requisicao " + hex(r.req);
    for (const auto &h : q.headers) sceHttpAddRequestHeader(r.req, h.first.c_str(), h.second.c_str(), 0);
    int rc = sceHttpSendRequest(r.req, nullptr, 0);
    if (rc < 0) return "envio " + hex(rc) + " (sem internet ou certificado recusado?)";
    int32_t st = 0;
    rc = sceHttpGetStatusCode(r.req, &st);
    if (rc < 0) return "status " + hex(rc);
    status = st;
    return "";
}

HttpResponse Http::get(const HttpRequest &q)
{
    HttpResponse resp;
    if (ctx_ < 0) { resp.error = "rede nao inicializada"; return resp; }
    Request r;
    std::string e = begin(ctx_, q, r, resp.status);
    if (!e.empty()) { resp.error = e; return resp; }
    char buf[16384];
    for (;;) {
        int n = sceHttpReadData(r.req, buf, sizeof buf);
        if (n < 0) { resp.error = "leitura " + hex(n); return resp; }
        if (n == 0) break;
        if (resp.body.size() + size_t(n) > q.maxBytes) { resp.error = "resposta maior que o limite"; resp.body.clear(); return resp; }
        resp.body.append(buf, size_t(n));
    }
    return resp;
}

std::unique_ptr<HttpStream> Http::openStream(const HttpRequest &q, std::string *error)
{
    if (ctx_ < 0) { if (error) *error = "rede nao inicializada"; return nullptr; }
    std::unique_ptr<Ps4Stream> s(new Ps4Stream);
    s->r.reset(new Request);
    std::string e = begin(ctx_, q, *s->r, s->code);
    if (e.empty() && (s->code < 200 || s->code >= 300)) e = "servidor respondeu HTTP " + std::to_string(s->code);
    if (!e.empty()) { if (error) *error = e; return nullptr; }
    return std::unique_ptr<HttpStream>(s.release());
}

}  // namespace ml::ps4
