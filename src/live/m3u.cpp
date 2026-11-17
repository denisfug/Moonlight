#include "live/m3u.hpp"

#include <algorithm>

#include "core/net.hpp"

namespace ml {

namespace {

std::string trim(const std::string &s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) b--;
    return s.substr(a, b - a);
}

// attr="value" from the #EXTINF line (before the last top-level comma)
std::string attr(const std::string &line, const std::string &key)
{
    std::string k = key + "=\"";
    size_t p = line.find(k);
    if (p == std::string::npos) return "";
    p += k.size();
    size_t e = line.find('"', p);
    return e == std::string::npos ? "" : line.substr(p, e - p);
}

// The display name is after the first comma that is not inside quotes.
std::string extinfName(const std::string &line)
{
    bool q = false;
    for (size_t i = 0; i < line.size(); i++) {
        if (line[i] == '"') q = !q;
        else if (line[i] == ',' && !q) return trim(line.substr(i + 1));
    }
    return "";
}

}  // namespace

M3uResult parseM3u(const std::string &text)
{
    M3uResult r;
    if (text.size() > 16 * 1024 * 1024) { r.error = "lista grande demais"; return r; }
    size_t start = 0;
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) start = 3;   // UTF-8 BOM
    bool sawHeader = false;
    Channel pending;
    bool havePending = false;
    while (start < text.size()) {
        size_t nl = text.find('\n', start);
        std::string line = trim(text.substr(start, nl == std::string::npos ? std::string::npos : nl - start));
        start = nl == std::string::npos ? text.size() : nl + 1;
        if (line.empty()) continue;
        if (line.compare(0, 7, "#EXTM3U") == 0) { sawHeader = true; continue; }
        if (line.compare(0, 8, "#EXTINF:") == 0) {
            pending = Channel();
            havePending = true;
            pending.name = extinfName(line);
            pending.group = attr(line, "group-title");
            pending.logo = attr(line, "tvg-logo");
            pending.tvgId = attr(line, "tvg-id");
            if (pending.name.empty()) pending.name = attr(line, "tvg-name");
            continue;
        }
        if (line.compare(0, 8, "#EXTGRP:") == 0) { if (havePending) pending.group = trim(line.substr(8)); continue; }
        if (line.compare(0, 10, "#KODIPROP:") == 0) {
            if (havePending && line.find("license_key") != std::string::npos) pending.drmProtected = true;
            continue;
        }
        if (line[0] == '#') continue;
        // URL line
        Channel c = havePending ? pending : Channel();
        havePending = false;
        c.url = line;
        if (!parseUrl(c.url).valid) { r.skipped++; continue; }
        if (c.name.empty()) c.name = c.url;
        if (c.group.empty()) c.group = "Sem categoria";
        if (std::find(r.groups.begin(), r.groups.end(), c.group) == r.groups.end()) r.groups.push_back(c.group);
        r.channels.push_back(std::move(c));
    }
    if (!sawHeader && r.channels.empty()) r.error = "nao parece uma lista M3U (#EXTM3U ausente)";
    return r;
}

}  // namespace ml
