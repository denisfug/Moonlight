// Small in-process HTTP server for tests: answers by path, optionally after a delay, and records
// the requests it saw. One connection at a time (give each simulated source its own server).
#pragma once
#include <unistd.h>

#include <atomic>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "platform/host/host_platform.hpp"

namespace t {

struct PathServer {
    ml::host::HostSockets sockets;
    std::unique_ptr<ml::TcpListener> lis;
    std::map<std::string, std::string> files;   // path -> body (200); anything else is 404
    std::map<std::string, int> status;          // path -> status to answer instead of 200
    int delayMs = 0;                            // wait before answering paths that contain delayPath
    std::string delayPath;
    std::vector<std::string> requests;          // read only after stop()
    std::atomic<bool> quit{false};
    std::atomic<int> hits{0};
    std::thread th;
    int port = 0;

    bool start(int basePort)
    {
        for (port = basePort + int(getpid() % 200); port < basePort + 400; ++port) {
            std::string e;
            if ((lis = sockets.listen(port, &e))) break;
        }
        if (!lis) return false;
        th = std::thread([this] {
            while (!quit) {
                std::string peer;
                auto c = lis->accept(50, &peer);
                if (!c) continue;
                std::string req;
                char b[2048];
                while (req.find("\r\n\r\n") == std::string::npos) {
                    long n = c->recv(b, sizeof b);
                    if (n <= 0) break;
                    req.append(b, size_t(n));
                }
                hits++;
                requests.push_back(req);
                size_t a = req.find(' '), z = req.find(' ', a + 1);
                std::string path = a == std::string::npos || z == std::string::npos ? "" : req.substr(a + 1, z - a - 1);
                if (delayMs > 0 && (delayPath.empty() || path.find(delayPath) != std::string::npos)) {
                    for (int waited = 0; waited < delayMs && !quit; waited += 10) usleep(10 * 1000);
                }
                std::string out;
                auto st = status.find(path);
                auto f = files.find(path);
                if (st != status.end()) out = "HTTP/1.1 " + std::to_string(st->second) + " X\r\nContent-Length: 0\r\n\r\n";
                else if (f == files.end()) out = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
                else out = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(f->second.size()) + "\r\n\r\n" + f->second;
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
    std::string url(const std::string &path) const { return "http://127.0.0.1:" + std::to_string(port) + path; }
    void stop()
    {
        quit = true;
        if (th.joinable()) th.join();
    }
    ~PathServer() { stop(); }
};

}  // namespace t
