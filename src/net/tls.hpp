// TLS client on top of a platform TcpConn, using Mbed TLS 3.6 (third_party/mbedtls, Apache-2.0).
// Why: the PS4 system TLS of older firmware refuses modern servers (0x8095F00C on every HTTPS site,
// seen on the console with 01.07/01.08). Here the app verifies certificates itself:
//   - trust anchors: current Mozilla roots in ca_roots.inc (tools/make_ca_roots.py);
//   - chain, validity dates and host name are always verified; there is no option to skip it.
#pragma once
#include <memory>
#include <string>

#include "core/net.hpp"

namespace ml::tls {

// Wraps an open TCP connection and performs the handshake for `host` (SNI + name check).
// nullptr with *err in Portuguese ("certificado ...", "handshake ...") on failure.
std::unique_ptr<TcpConn> connect(std::unique_ptr<TcpConn> tcp, const std::string &host, std::string *err);

// Number of bundled roots that parsed (for "Testar internet"); initialises the library on first use.
int trustedRoots(std::string *err);

// Tests only: trust an extra PEM root (e.g. a local test CA). Must be called before the first connect.
bool addTrustedRootForTests(const std::string &pem);

}  // namespace ml::tls

// Implemented by each platform: fills buf with len cryptographically secure random bytes; 0 = ok.
extern "C" int ml_platform_random(unsigned char *buf, size_t len);
