#include "music/archive.hpp"

#include <algorithm>
#include <cstdlib>

#include "core/net.hpp"
#include "music/music.hpp"

namespace ml::archive {

namespace {
std::string lower(std::string s)
{
    for (char &ch : s) if (ch >= 'A' && ch <= 'Z') ch = char(ch - 'A' + 'a');
    return s;
}

// The Archive returns a field as a string or as an array of strings, depending on the item.
std::string firstString(const Json &j)
{
    if (j.isString()) return j.asString();
    if (j.isArray() && j.size() > 0 && j[size_t(0)].isString()) return j[size_t(0)].asString();
    return "";
}

// Lucene special characters would change the meaning of the query (or make it invalid).
std::string cleanQuery(const std::string &q)
{
    std::string out;
    for (char ch : q) {
        unsigned char u = static_cast<unsigned char>(ch);
        bool special = ch == '(' || ch == ')' || ch == '[' || ch == ']' || ch == '{' || ch == '}' || ch == '"' ||
                       ch == ':' || ch == '\\' || ch == '^' || ch == '~' || ch == '*' || ch == '?' || ch == '!' ||
                       ch == '+' || ch == '/' || u < 0x20;
        out += special ? ' ' : ch;
    }
    return out;
}

bool endsWithMp3(const std::string &name)
{
    return name.size() > 4 && lower(name.substr(name.size() - 4)) == ".mp3";
}

// Keeps "/" between folders of the file name, encodes everything else.
std::string encodePath(const std::string &name)
{
    std::string out;
    size_t start = 0;
    while (start <= name.size()) {
        size_t slash = name.find('/', start);
        if (slash == std::string::npos) slash = name.size();
        out += percentEncode(name.substr(start, slash - start));
        if (slash < name.size()) out += '/';
        start = slash + 1;
    }
    return out;
}

int leadingInt(const std::string &s)
{
    int v = 0;
    size_t i = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9' && v < 100000) v = v * 10 + (s[i++] - '0');
    return v;
}
}  // namespace

bool validId(const std::string &id)
{
    if (id.empty() || id.size() > 200) return false;
    for (char ch : id) {
        bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '.' ||
                  ch == '_' || ch == '-';
        if (!ok) return false;
    }
    return true;
}

std::string searchUrl(const std::string &query, int rows, const std::string &base)
{
    std::string q = "(" + cleanQuery(query) + ") AND mediatype:(audio OR etree) AND format:(\"VBR MP3\")";
    return base + "/advancedsearch.php?q=" + percentEncode(q) +
           "&fl%5B%5D=identifier&fl%5B%5D=title&fl%5B%5D=creator&sort%5B%5D=downloads%20desc&rows=" +
           std::to_string(rows) + "&page=1&output=json";
}

std::string metadataUrl(const std::string &id, const std::string &base)
{
    return validId(id) ? base + "/metadata/" + id : std::string();
}

std::vector<Item> parseSearch(const Json &root)
{
    std::vector<Item> v;
    const Json &docs = root["response"]["docs"];
    for (size_t i = 0; i < docs.size(); i++) {
        Item it;
        it.id = docs[i]["identifier"].asString();
        if (!validId(it.id)) continue;
        it.title = firstString(docs[i]["title"]);
        it.creator = firstString(docs[i]["creator"]);
        if (it.title.empty()) it.title = it.id;
        v.push_back(std::move(it));
    }
    return v;
}

int parseLength(const std::string &s)
{
    if (s.empty()) return 0;
    if (s.find(':') == std::string::npos) {
        double d = std::atof(s.c_str());
        return d > 0 && d < 360000 ? int(d + 0.5) : 0;
    }
    long total = 0, part = 0;
    for (char ch : s) {
        if (ch == ':') { total = total * 60 + part; part = 0; }
        else if (ch >= '0' && ch <= '9') { if (part < 100000) part = part * 10 + (ch - '0'); }
        else if (ch == '.') break;   // fraction of a second
        else return 0;
    }
    total = total * 60 + part;
    return total > 0 && total < 360000 ? int(total) : 0;
}

std::vector<Song> parseItem(const Json &root, const std::string &base)
{
    std::vector<Song> v;
    const Json &meta = root["metadata"];
    std::string id = meta["identifier"].asString();
    if (!validId(id)) return v;
    std::string album = firstString(meta["title"]);
    std::string creator = firstString(meta["creator"]);
    const Json &files = root["files"];
    bool hasVbr = false;
    for (size_t i = 0; i < files.size(); i++)
        if (endsWithMp3(files[i]["name"].asString()) && files[i]["format"].asString() == "VBR MP3") hasVbr = true;
    for (size_t i = 0; i < files.size() && v.size() < 500; i++) {
        const Json &f = files[i];
        std::string name = f["name"].asString();
        if (!endsWithMp3(name)) continue;
        if (hasVbr && f["format"].asString() != "VBR MP3") continue;   // one encoding per track
        if (f["private"].asString() == "true") continue;                // not downloadable
        Song s;
        s.url = base + "/download/" + id + "/" + encodePath(name);
        s.title = firstString(f["title"]);
        s.artist = firstString(f["artist"]);
        if (s.artist.empty()) s.artist = firstString(f["creator"]);
        s.album = firstString(f["album"]);
        if (s.album.empty()) s.album = album;
        if (s.title.empty()) {
            std::string file = name.substr(name.rfind('/') == std::string::npos ? 0 : name.rfind('/') + 1);
            std::string artistFromName;
            music::titleFromFilename(file, s.title, artistFromName);
            if (s.title.empty()) s.title = file;
            if (s.artist.empty()) s.artist = artistFromName;
        }
        if (s.artist.empty()) s.artist = creator.empty() ? "Internet Archive" : creator;
        s.durationSec = parseLength(f["length"].asString());
        s.track = leadingInt(f["track"].isNumber() ? std::to_string(f["track"].asInt()) : f["track"].asString());
        v.push_back(std::move(s));
    }
    // Track number first; files without one keep the order of the item's file list.
    std::stable_sort(v.begin(), v.end(), [](const Song &a, const Song &b) {
        if ((a.track > 0) != (b.track > 0)) return a.track > 0;
        return a.track < b.track;
    });
    return v;
}

}  // namespace ml::archive
