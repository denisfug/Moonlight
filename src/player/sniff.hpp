// Recognises a media container from its first bytes, so the player can say "formato MKV" instead of
// a bare system error code (01.13: sceAvPlayerAddSource 0x806A0002 OPERATION_FAILED on a series).
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace ml {

// "mp4", "mkv" (also WebM), "ts", "hls", "avi", "flv", "html", or "" when unknown.
inline std::string sniffContainer(const uint8_t *b, size_t n)
{
    if (n >= 8 && std::memcmp(b + 4, "ftyp", 4) == 0) return "mp4";
    if (n >= 8 && (std::memcmp(b + 4, "moov", 4) == 0 || std::memcmp(b + 4, "mdat", 4) == 0 || std::memcmp(b + 4, "free", 4) == 0)) return "mp4";
    if (n >= 4 && b[0] == 0x1A && b[1] == 0x45 && b[2] == 0xDF && b[3] == 0xA3) return "mkv";
    if (n >= 7 && std::memcmp(b, "#EXTM3U", 7) == 0) return "hls";
    if (n >= 1 && b[0] == 0x47 && (n < 189 || b[188] == 0x47)) return "ts";
    if (n >= 12 && std::memcmp(b, "RIFF", 4) == 0 && std::memcmp(b + 8, "AVI ", 4) == 0) return "avi";
    if (n >= 3 && std::memcmp(b, "FLV", 3) == 0) return "flv";
    size_t i = 0;
    while (i < n && (b[i] == ' ' || b[i] == '\n' || b[i] == '\r' || b[i] == '\t' || b[i] == 0xEF || b[i] == 0xBB || b[i] == 0xBF)) i++;
    if (i < n && b[i] == '<') return "html";
    return "";
}

// Guess from a file name, URL or free text (a stream's description) before downloading anything:
// "mp4", "mkv", "avi", "hls", "ts" or "". Query strings are ignored for URLs.
inline std::string guessContainerFromText(const std::string &text)
{
    std::string t;
    t.reserve(text.size());
    for (char ch : text) t += char(ch >= 'A' && ch <= 'Z' ? ch + 32 : ch);
    static const struct { const char *ext; const char *fmt; } kExt[] = {
        {".mp4", "mp4"}, {".m4v", "mp4"}, {".mov", "mp4"}, {".mkv", "mkv"}, {".webm", "mkv"},
        {".avi", "avi"}, {".m3u8", "hls"}, {".ts", "ts"},
    };
    size_t best = std::string::npos;
    std::string fmt;
    for (const auto &e : kExt) {
        size_t len = std::strlen(e.ext);
        for (size_t pos = t.find(e.ext); pos != std::string::npos; pos = t.find(e.ext, pos + 1)) {
            // the extension must end the word: ".ts" must not match ".tsx" or "x.tsunami"
            char next = pos + len < t.size() ? t[pos + len] : ' ';
            bool wordEnd = !((next >= 'a' && next <= 'z') || (next >= '0' && next <= '9'));
            if (wordEnd && (best == std::string::npos || pos > best)) { best = pos; fmt = e.fmt; }
        }
    }
    return fmt;
}

inline std::string guessContainer(const std::string &filename, const std::string &url, const std::string &description)
{
    std::string f = guessContainerFromText(filename);
    if (!f.empty()) return f;
    f = guessContainerFromText(url.substr(0, url.find_first_of("?#")));
    if (!f.empty()) return f;
    return guessContainerFromText(description);
}

// Portuguese explanation for formats the PS4 system player is not expected to open.
inline std::string containerProblem(const std::string &fmt)
{
    if (fmt == "mkv") return "o arquivo e MKV/WebM: o player do PS4 nao abre esse formato (so MP4 H.264)";
    if (fmt == "avi") return "o arquivo e AVI: o player do PS4 nao abre esse formato";
    if (fmt == "flv") return "o arquivo e FLV: o player do PS4 nao abre esse formato";
    if (fmt == "html") return "o link abriu uma pagina, nao um video (o servidor pode exigir algo que o app nao faz)";
    return "";
}

}  // namespace ml
