// The app's own HTTP/TLS client (src/net) against local servers:
// - a scripted plain-HTTP server on a thread (chunked, Content-Length, redirects, ICY);
// - `openssl s_server` with test certificates (tests/fixtures/tls) for verification.
#include <signal.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <thread>

#include "net/tls.hpp"
#include "net/web_client.hpp"
#include "platform/host/host_platform.hpp"
#include "test.hpp"

using namespace ml;

namespace {
// Answers each connection with the next canned response (raw bytes), then closes.
struct ScriptServer {
    host::HostSockets sockets;
    std::unique_ptr<TcpListener> lis;
    std::vector<std::string> replies;
    std::vector<std::string> requests;
    std::atomic<bool> stop{false};
    std::thread th;
    int port = 0;

    bool open()
    {
        for (port = 47100 + int(getpid() % 500); port < 47900; ++port) {
            std::string e;
            lis = sockets.listen(port, &e);
            if (lis) break;
        }
        return lis != nullptr;
    }
    bool start() { return (lis || open()) && launch(); }
    bool launch()
    {
        th = std::thread([this] {
            size_t i = 0;
            while (!stop && i < replies.size()) {
                std::string peer;
                auto c = lis->accept(100, &peer);
                if (!c) continue;
                std::string req;
                char b[2048];
                while (req.find("\r\n\r\n") == std::string::npos) {
                    long n = c->recv(b, sizeof b);
                    if (n <= 0) break;
                    req.append(b, size_t(n));
                }
                requests.push_back(req);
                const std::string &r = replies[i++];
                c->send(r.data(), r.size());
            }
        });
        return true;
    }
    std::string url(const std::string &path) const { return "http://127.0.0.1:" + std::to_string(port) + path; }
    ~ScriptServer()
    {
        stop = true;
        if (th.joinable()) th.join();
    }
};
}  // namespace

TEST(web_client_framing_and_redirects)
{
    ScriptServer s;
    s.replies = {
        "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 7\r\n\r\n{\"a\":1}",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n",
        "HTTP/1.1 302 Found\r\nLocation: /final\r\nContent-Length: 0\r\n\r\n",
        "HTTP/1.1 200 OK\r\n\r\nclose-delimited body",
        "ICY 200 OK\r\ncontent-type: audio/mpeg\r\n\r\nMP3DATA",
        "HTTP/1.1 404 Not Found\r\nContent-Length: 3\r\n\r\nnop",
        "HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\nshort",
    };
    CHECK(s.start());
    host::HostHttp http;
    HttpRequest q;
    q.timeoutMs = 3000;

    q.url = s.url("/a.json");
    HttpResponse r = http.get(q);
    CHECK(r.ok());
    CHECK_EQ(r.body, std::string("{\"a\":1}"));
    CHECK_EQ(r.contentType, std::string("application/json"));

    q.url = s.url("/chunked");
    r = http.get(q);
    CHECK(r.ok());
    CHECK_EQ(r.body, std::string("hello world"));

    q.url = s.url("/start");   // 302 -> /final (close-delimited)
    r = http.get(q);
    CHECK(r.ok());
    CHECK_EQ(r.body, std::string("close-delimited body"));
    CHECK(s.requests.size() >= 4 && s.requests[3].compare(0, 11, "GET /final ") == 0);

    q.url = s.url("/radio");
    std::string e;
    auto st = http.openStream(q, &e);
    CHECK(st != nullptr);
    if (st) {
        char b[32];
        long n = st->read(b, sizeof b);
        CHECK_EQ(std::string(b, size_t(n > 0 ? n : 0)), std::string("MP3DATA"));
        CHECK_EQ(st->contentType(), std::string("audio/mpeg"));
    }

    q.url = s.url("/missing");
    r = http.get(q);
    CHECK(r.error.empty());
    CHECK_EQ(r.status, 404);

    q.url = s.url("/truncated");   // Content-Length 100 but only 5 bytes
    r = http.get(q);
    CHECK(!r.error.empty());
    CHECK(s.requests.size() == 7);
    CHECK(s.requests[0].find("Host: 127.0.0.1:") != std::string::npos);
}

TEST(web_client_resolve_location)
{
    Url b = parseUrl("https://a.org/x/y/z.json?q=1");
    CHECK_EQ(resolveLocation(b, "https://b.org/k"), std::string("https://b.org/k"));
    CHECK_EQ(resolveLocation(b, "//c.org/k"), std::string("https://c.org/k"));
    CHECK_EQ(resolveLocation(b, "/root"), std::string("https://a.org/root"));
    CHECK_EQ(resolveLocation(b, "rel.json"), std::string("https://a.org/x/y/rel.json"));
}

TEST(tls_verifies_certificates)
{
    if (std::system("command -v openssl >/dev/null 2>&1") != 0) { std::printf("(skipped: no openssl) "); return; }
    std::string dir = t::fixtures + "/tls";
    std::ifstream caf(dir + "/ca.pem");
    std::string ca((std::istreambuf_iterator<char>(caf)), std::istreambuf_iterator<char>());
    CHECK(!ca.empty());
    CHECK(tls::addTrustedRootForTests(ca));
    std::string ie;
    CHECK(tls::trustedRoots(&ie) >= 20);   // bundled roots + the test root

    // Web root for s_server -WWW (serves files relative to its working directory).
    char tmpl[] = "/tmp/ml_tls_XXXXXX";
    std::string www = mkdtemp(tmpl);
    std::ofstream(www + "/hello.txt") << "secure hello";
    int port = 48100 + int(getpid() % 500);
    auto serve = [&](const char *cert, const char *key, int p, const char *extra = "") {
        std::string cmd = "cd " + www + " && openssl s_server " + extra + " -quiet -WWW -accept " + std::to_string(p) + " -cert " + dir + "/" +
                          cert + " -key " + dir + "/" + key + " >/dev/null 2>&1 & echo $!";
        FILE *f = popen(cmd.c_str(), "r");
        int pid = 0;
        if (f) { if (std::fscanf(f, "%d", &pid) != 1) pid = 0; pclose(f); }
        usleep(400 * 1000);
        return pid;
    };
    int good = serve("server.pem", "server.key", port);
    int rogue = serve("rogue.pem", "rogue.key", port + 1);
    int tls12 = serve("server.pem", "server.key", port + 2, "-tls1_2");

    host::HostHttp http;
    HttpRequest q;
    q.timeoutMs = 5000;
    q.url = "https://localhost:" + std::to_string(port) + "/hello.txt";
    HttpResponse r = http.get(q);
    CHECK(r.error.empty());
    CHECK_EQ(r.status, 200);
    CHECK_EQ(r.body, std::string("secure hello"));

    // Same server reached by IP: the certificate is only for "localhost".
    q.url = "https://127.0.0.1:" + std::to_string(port) + "/hello.txt";
    r = http.get(q);
    CHECK(r.error.find("outro site") != std::string::npos);

    // Self-signed certificate nobody trusts.
    q.url = "https://localhost:" + std::to_string(port + 1) + "/hello.txt";
    r = http.get(q);
    CHECK(r.error.find("desconhecido") != std::string::npos);
    std::printf("[%s] ", r.error.c_str());

    // TLS 1.2-only server (most of the web still negotiates 1.2).
    q.url = "https://localhost:" + std::to_string(port + 2) + "/hello.txt";
    r = http.get(q);
    CHECK(r.error.empty());
    CHECK_EQ(r.body, std::string("secure hello"));

    if (tls12 > 0) kill(tls12, SIGTERM);
    if (good > 0) kill(good, SIGTERM);
    if (rogue > 0) kill(rogue, SIGTERM);
    if (std::system(("rm -rf " + www).c_str()) != 0) std::printf("(tmp not removed) ");
}

// ---------------------------------------------------------------- phone remote
#include "app/remote.hpp"

TEST(remote_pin_and_commands)
{
    host::HostPlatform p("/tmp/ml_remote_sandbox", "assets");
    RemoteControl rc(p);
    std::string e;
    CHECK(rc.start(&e));
    CHECK_EQ(rc.pin().size(), size_t(4));
    CHECK(rc.address().find(":80") != std::string::npos);

    auto req = [](const std::string &method, const std::string &path, const std::string &pin, const std::string &body) {
        return method + " " + path + " HTTP/1.1\r\nHost: x\r\nX-Pin: " + pin + "\r\nContent-Length: " + std::to_string(body.size()) +
               "\r\n\r\n" + body;
    };
    CHECK(rc.handle(req("GET", "/", "", ""), 1000).find("Moonlight HB") != std::string::npos);
    CHECK(rc.handle(req("GET", "/api/state", "0000x", ""), 1000).compare(0, 12, "HTTP/1.1 403") == 0);
    CHECK(rc.handle(req("POST", "/api/key", rc.pin(), "cross"), 1000).compare(0, 12, "HTTP/1.1 200") == 0);
    CHECK(rc.handle(req("POST", "/api/text", rc.pin(), "olá mundo"), 1000).compare(0, 12, "HTTP/1.1 200") == 0);
    CHECK(rc.handle(req("POST", "/api/source", rc.pin(), "https://a.org/manifest.json\n"), 1000).compare(0, 12, "HTTP/1.1 200") == 0);
    CHECK(rc.handle(req("POST", "/api/key", rc.pin(), "selfdestruct"), 1000).compare(0, 12, "HTTP/1.1 400") == 0);
    auto cmds = rc.take();
    CHECK_EQ(cmds.size(), size_t(3));
    if (cmds.size() == 3) {
        CHECK_EQ(cmds[0].buttons, uint32_t(BtnCross));
        CHECK_EQ(cmds[1].text, std::string("olá mundo"));
        CHECK_EQ(cmds[2].text, std::string("https://a.org/manifest.json"));
    }
    rc.setState("Filmes", true, "Buscar", "aba", false);
    CHECK(rc.handle(req("GET", "/api/state", rc.pin(), ""), 1000).find("\"title\":\"Buscar\"") != std::string::npos);

    // 5 wrong PINs lock the API for 60 s, even for the right PIN.
    for (int i = 0; i < 5; i++) rc.handle(req("GET", "/api/state", "9999x", ""), 2000);
    CHECK(rc.handle(req("GET", "/api/state", rc.pin(), ""), 3000).compare(0, 12, "HTTP/1.1 403") == 0);
    CHECK(rc.handle(req("GET", "/api/state", rc.pin(), ""), 63000).compare(0, 12, "HTTP/1.1 200") == 0);

    // Over the real socket, with the app's own client.
    host::HostHttp http;
    HttpRequest q;
    q.url = rc.address().replace(0, rc.address().find(':', 5), "http://127.0.0.1");
    q.timeoutMs = 3000;
    q.url += "/api/state";
    q.headers.push_back({"X-Pin", rc.pin()});
    HttpResponse r = http.get(q);
    CHECK(r.ok());
    CHECK(r.body.find("Filmes") != std::string::npos);
    rc.stop();
    CHECK(!rc.running());
}

// ---------------------------------------------------------------- range reads (video over HTTP(S))
#include "net/range_reader.hpp"

namespace {
// Serves a 1 MiB deterministic file with Range support (or ignoring Range when plain is set).
struct RangeServer {
    host::HostSockets sockets;
    std::unique_ptr<TcpListener> lis;
    std::string file;
    std::atomic<bool> stop{false};
    std::atomic<int> hits{0};
    bool plain = false;
    std::thread th;
    int port = 0;
    bool start()
    {
        file.resize(1 << 20);
        for (size_t i = 0; i < file.size(); i++) file[i] = char((i * 131 + i / 977) & 0xFF);
        for (port = 47950 + int(getpid() % 40); port < 48090; ++port) {
            std::string e;
            if ((lis = sockets.listen(port, &e))) break;
        }
        if (!lis) return false;
        th = std::thread([this] {
            while (!stop) {
                std::string peer;
                auto c = lis->accept(100, &peer);
                if (!c) continue;
                hits++;
                std::string req;
                char b[2048];
                while (req.find("\r\n\r\n") == std::string::npos) {
                    long n = c->recv(b, sizeof b);
                    if (n <= 0) break;
                    req.append(b, size_t(n));
                }
                size_t r = req.find("Range: bytes=");
                std::string out;
                if (r != std::string::npos && !plain) {
                    size_t from = std::strtoul(req.c_str() + r + 13, nullptr, 10);
                    out = "HTTP/1.1 206 Partial Content\r\nContent-Range: bytes " + std::to_string(from) + "-" +
                          std::to_string(file.size() - 1) + "/" + std::to_string(file.size()) +
                          "\r\nContent-Length: " + std::to_string(file.size() - from) + "\r\n\r\n" + file.substr(from);
                } else {
                    out = "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(file.size()) + "\r\n\r\n" + file;
                }
                size_t done = 0;
                while (done < out.size()) {
                    long n = c->send(out.data() + done, out.size() - done);
                    if (n <= 0) break;
                    done += size_t(n);
                }
            }
        });
        return true;
    }
    ~RangeServer()
    {
        stop = true;
        if (th.joinable()) th.join();
    }
};
}  // namespace

TEST(range_reader_random_access)
{
    RangeServer s;
    CHECK(s.start());
    host::HostHttp http;
    RangeReader rr(&http, "http://127.0.0.1:" + std::to_string(s.port) + "/movie.mp4");
    CHECK(rr.open().empty());
    CHECK_EQ(rr.size(), int64_t(1 << 20));
    CHECK(rr.seekable());
    std::string buf(70000, '\0');
    CHECK_EQ(rr.readAt(0, &buf[0], 70000), 70000L);              // sequential: same response
    CHECK(buf == s.file.substr(0, 70000));
    CHECK_EQ(rr.readAt(70000, &buf[0], 1000), 1000L);
    CHECK(buf.compare(0, 1000, s.file, 70000, 1000) == 0);
    uint64_t before = rr.requests();
    CHECK_EQ(rr.readAt(900000, &buf[0], 5000), 5000L);           // far jump (moov at the end): new request
    CHECK(buf.compare(0, 5000, s.file, 900000, 5000) == 0);
    CHECK_EQ(rr.requests(), before + 1);
    CHECK_EQ(rr.readAt(1048000, &buf[0], 5000), 576L);           // clipped at the end of the file
    CHECK(buf.compare(0, 576, s.file, 1048000, 576) == 0);
    CHECK_EQ(rr.readAt(1 << 20, &buf[0], 10), 0L);

    s.plain = true;                                              // server that ignores Range
    RangeReader rp(&http, "http://127.0.0.1:" + std::to_string(s.port) + "/movie.mp4");
    CHECK(rp.open().empty());
    CHECK(!rp.seekable());
    CHECK_EQ(rp.readAt(300000, &buf[0], 4000), 4000L);
    CHECK(buf.compare(0, 4000, s.file, 300000, 4000) == 0);
}

// ---------------------------------------------------------------- updates
#include "app/updater.hpp"

TEST(updater_versions_and_release_json)
{
    CHECK(parseVersion("v01.10") > parseVersion("01.09"));
    CHECK(parseVersion("01.10") == parseVersion("v1.10"));
    CHECK(parseVersion("2.00") > parseVersion("01.99"));
    CHECK_EQ(parseVersion("sem numero"), -1);
    std::string tag, url, title;
    int64_t size = 0;
    CHECK(Updater::parseRelease(R"({"tag_name":"v01.10","name":"Moonlight 01.10","assets":[{"name":"notes.txt","browser_download_url":"https://x/n"},{"name":"Moonlight-HB_01.10.pkg","size":123,"browser_download_url":"https://x/p.pkg"}]})",
                                &tag, &url, &size, &title).empty());
    CHECK_EQ(tag, std::string("v01.10"));
    CHECK_EQ(url, std::string("https://x/p.pkg"));
    CHECK_EQ(size, int64_t(123));
    CHECK(!Updater::parseRelease(R"({"tag_name":"v01.10","assets":[]})", &tag, &url, &size, &title).empty());
}

TEST(updater_check_and_download_local)
{
    std::string pkg = std::string("\x7F" "CNT") + std::string(5000, 'p');
    ScriptServer s;
    CHECK(s.open());   // port first: the replies below contain it
    std::string base = s.url("");
    std::string rel = "{\"tag_name\":\"v99.01\",\"name\":\"teste\",\"assets\":[{\"name\":\"Moonlight-HB_99.01.pkg\",\"size\":" +
                      std::to_string(pkg.size()) + ",\"browser_download_url\":\"" + base + "/dl/x.pkg\"}]}";
    s.replies = {
        "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(rel.size()) + "\r\n\r\n" + rel,
        "HTTP/1.1 302 Found\r\nLocation: /cdn/x.pkg\r\nContent-Length: 0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(pkg.size()) + "\r\n\r\n" + pkg,
    };
    CHECK(s.launch());
    host::HostFileSystem(t::scratch + "/upd").makeDirs("/");
    host::HostPlatform p(t::scratch + "/upd", "");
    Updater u(p, "denisfug/Moonlight", "01.09");
    u.apiBase = base;
    u.check();
    for (int i = 0; i < 300 && (u.state().phase == Updater::Phase::Checking); i++) p.sleepMs(10);
    Updater::State st = u.state();
    CHECK(st.phase == Updater::Phase::Available);
    CHECK_EQ(st.latest, std::string("99.01"));
    u.download();
    for (int i = 0; i < 300 && (u.state().phase == Updater::Phase::Downloading); i++) p.sleepMs(10);
    st = u.state();
    CHECK(st.phase == Updater::Phase::Downloaded);
    if (st.phase != Updater::Phase::Downloaded) std::printf("[%s] ", st.error.c_str());
    std::string got;
    CHECK(p.fs().readFile("/data/pkg/Moonlight-HB_99.01.pkg", got, 1 << 20));
    CHECK(got == pkg);
    CHECK(s.requests.size() == 3 && s.requests[0].find("GET /repos/denisfug/Moonlight/releases/latest") == 0);
    std::string e;
    CHECK_EQ(p.installPackage(st.path, false, &e), -1);   // only on the PS4
}
