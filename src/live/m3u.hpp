// M3U / M3U8 playlist parser for Live TV (extended M3U with #EXTINF attributes).
// Only lists the user is authorised to use; DRM-protected entries (#KODIPROP license keys) are
// reported as unsupported instead of being worked around.
#pragma once
#include <string>
#include <vector>

namespace ml {

struct Channel {
    std::string name, url, group, logo, tvgId;
    bool drmProtected = false;
};

struct M3uResult {
    std::vector<Channel> channels;
    std::vector<std::string> groups;   // in order of first appearance
    int skipped = 0;                   // entries without a valid http(s) URL
    std::string error;
};

M3uResult parseM3u(const std::string &text);

}  // namespace ml
