#include "net/range_reader.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace ml {

namespace {
constexpr uint64_t kSkipLimit = 512 * 1024;   // forward jumps up to this are read through, not re-requested
}

std::string RangeReader::openAt(uint64_t pos)
{
    stream_.reset();
    HttpRequest q;
    q.url = url_;
    q.timeoutMs = 20000;
    q.headers.push_back({"Range", "bytes=" + std::to_string(pos) + "-"});
    std::string e;
    ++requests_;
    std::unique_ptr<WebStream> s = client_->open(q, &e);
    if (!s) return e;
    int st = s->status();
    if (st == 206) {
        ranges_ = true;
        // Content-Range: bytes 0-999/1000
        std::string cr = s->header("content-range");
        size_t slash = cr.rfind('/');
        if (slash != std::string::npos && cr.compare(slash + 1, 1, "*") != 0) size_ = std::strtoll(cr.c_str() + slash + 1, nullptr, 10);
        stream_ = std::move(s);
        streamPos_ = pos;
        return "";
    }
    if (st == 200) {
        // Server ignores Range: whole file from 0. Fine at 0; elsewhere read through up to the offset.
        if (s->contentLength() >= 0) size_ = s->contentLength();
        stream_ = std::move(s);
        streamPos_ = 0;
        if (pos > 0) {
            if (pos > 64ull * 1024 * 1024) { stream_.reset(); return "o servidor nao permite pular no video (sem Range)"; }
            std::vector<char> tmp(16384);   // heap: callers may run on small stacks
            while (streamPos_ < pos) {
                long n = stream_->read(tmp.data(), size_t(std::min<uint64_t>(tmp.size(), pos - streamPos_)));
                if (n <= 0) { stream_.reset(); return "leitura falhou ao pular"; }
                streamPos_ += uint64_t(n);
            }
        }
        return "";
    }
    if (st == 416) return "posicao alem do fim do arquivo";
    return "servidor respondeu HTTP " + std::to_string(st);
}

std::string RangeReader::open()
{
    lock();
    std::string e = openAt(0);
    if (e.empty() && size_ < 0) e = "o servidor nao informou o tamanho do arquivo";
    unlock();
    return e;
}

long RangeReader::readAt(uint64_t pos, void *buf, size_t len)
{
    lock();
    long total = 0;
    if (size_ >= 0 && pos >= uint64_t(size_)) { unlock(); return 0; }
    if (size_ >= 0 && pos + len > uint64_t(size_)) len = size_t(uint64_t(size_) - pos);
    if (stream_ && pos > streamPos_ && pos - streamPos_ <= kSkipLimit) {
        std::vector<char> tmp(16384);
        while (stream_ && streamPos_ < pos) {
            long n = stream_->read(tmp.data(), size_t(std::min<uint64_t>(tmp.size(), pos - streamPos_)));
            if (n <= 0) stream_.reset();
            else streamPos_ += uint64_t(n);
        }
    }
    for (int attempt = 0; total < long(len) && attempt < 3;) {
        uint64_t at = pos + uint64_t(total);
        if (!stream_ || streamPos_ != at) {
            std::string e = openAt(at);
            if (!e.empty()) { err_ = e; ++attempt; continue; }
        }
        long n = stream_->read(static_cast<char *>(buf) + total, len - size_t(total));
        if (n > 0) { total += n; streamPos_ += uint64_t(n); continue; }
        // End or broken connection: a fresh request continues from here (servers drop long downloads).
        err_ = n < 0 ? stream_->lastError() : "fim inesperado";
        stream_.reset();
        if (n == 0 && size_ < 0) break;
        ++attempt;
    }
    unlock();
    if (total == 0 && len > 0) return -1;
    return total;
}

std::string RangeReader::lastError()
{
    lock();
    std::string e = err_;
    unlock();
    return e;
}

}  // namespace ml
