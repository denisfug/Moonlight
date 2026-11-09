#include "music/music.hpp"

#include <algorithm>
#include <cctype>

namespace ml::music {

namespace {

std::string lower(std::string s)
{
    for (auto &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool isMp3(const std::string &n)
{
    return n.size() > 4 && lower(n.substr(n.size() - 4)) == ".mp3" && n[0] != '.';
}

uint32_t syncsafe(const unsigned char *b) { return (b[0] & 0x7F) << 21 | (b[1] & 0x7F) << 14 | (b[2] & 0x7F) << 7 | (b[3] & 0x7F); }
uint32_t be32(const unsigned char *b) { return uint32_t(b[0]) << 24 | uint32_t(b[1]) << 16 | uint32_t(b[2]) << 8 | b[3]; }

void putUtf8(std::string &o, uint32_t cp)
{
    if (cp < 0x80) o += char(cp);
    else if (cp < 0x800) { o += char(0xC0 | (cp >> 6)); o += char(0x80 | (cp & 0x3F)); }
    else { o += char(0xE0 | (cp >> 12)); o += char(0x80 | ((cp >> 6) & 0x3F)); o += char(0x80 | (cp & 0x3F)); }
}

// ID3 text frame payload -> UTF-8 (encodings: 0 Latin-1, 1 UTF-16 BOM, 2 UTF-16BE, 3 UTF-8)
std::string decodeText(const unsigned char *p, size_t n)
{
    if (n == 0) return "";
    unsigned enc = p[0];
    p++; n--;
    std::string o;
    if (enc == 0) {
        for (size_t i = 0; i < n && p[i]; i++) putUtf8(o, p[i]);
    } else if (enc == 3) {
        for (size_t i = 0; i < n && p[i]; i++) o += char(p[i]);
    } else {
        bool le = false;
        size_t i = 0;
        if (enc == 1 && n >= 2) {
            if (p[0] == 0xFF && p[1] == 0xFE) { le = true; i = 2; }
            else if (p[0] == 0xFE && p[1] == 0xFF) { i = 2; }
        }
        for (; i + 1 < n; i += 2) {
            uint32_t u = le ? (p[i] | p[i + 1] << 8) : (p[i] << 8 | p[i + 1]);
            if (u == 0) break;
            if (u >= 0xD800 && u <= 0xDFFF) { putUtf8(o, 0xFFFD); continue; }   // BMP is enough for tags
            putUtf8(o, u);
        }
    }
    while (!o.empty() && (o.back() == ' ' || o.back() == '\0')) o.pop_back();
    return o;
}

std::string trim(const std::string &s)
{
    size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
}

}  // namespace

Id3Info parseId3(const std::string &head)
{
    Id3Info info;
    const auto *b = reinterpret_cast<const unsigned char *>(head.data());
    if (head.size() < 10 || b[0] != 'I' || b[1] != 'D' || b[2] != '3') return info;
    unsigned ver = b[3];
    uint32_t size = syncsafe(b + 6);
    info.tagBytes = 10 + size + ((b[5] & 0x10) ? 10 : 0);
    size_t end = std::min<size_t>(head.size(), 10 + size);
    size_t p = 10;
    if ((b[5] & 0x40) && ver >= 3 && p + 4 <= end) {   // extended header
        uint32_t ext = ver == 4 ? syncsafe(b + p) : be32(b + p) + 4;
        p += ext;
    }
    while (p < end) {
        if (ver == 2) {
            if (p + 6 > end || b[p] == 0) break;
            std::string id(reinterpret_cast<const char *>(b + p), 3);
            uint32_t fs = uint32_t(b[p + 3]) << 16 | uint32_t(b[p + 4]) << 8 | b[p + 5];
            p += 6;
            if (fs > end - p) break;
            if (id == "TT2") info.title = decodeText(b + p, fs);
            else if (id == "TP1") info.artist = decodeText(b + p, fs);
            else if (id == "TAL") info.album = decodeText(b + p, fs);
            p += fs;
        } else {
            if (p + 10 > end || b[p] == 0) break;
            std::string id(reinterpret_cast<const char *>(b + p), 4);
            uint32_t fs = ver == 4 ? syncsafe(b + p + 4) : be32(b + p + 4);
            p += 10;
            if (fs > end - p) break;
            if (id == "TIT2") info.title = decodeText(b + p, fs);
            else if (id == "TPE1") info.artist = decodeText(b + p, fs);
            else if (id == "TALB") info.album = decodeText(b + p, fs);
            p += fs;
        }
    }
    return info;
}

void titleFromFilename(const std::string &filename, std::string &title, std::string &artist)
{
    std::string n = filename;
    size_t dot = n.rfind('.');
    if (dot != std::string::npos) n = n.substr(0, dot);
    // drop "NNN - " index prefixes written by playlist sync tools
    if (n.size() > 6 && std::isdigit((unsigned char)n[0]) && std::isdigit((unsigned char)n[1]) &&
        std::isdigit((unsigned char)n[2]) && n.compare(3, 3, " - ") == 0)
        n = n.substr(6);
    size_t sep = n.find(" - ");
    if (sep != std::string::npos) {
        if (artist.empty()) artist = trim(n.substr(0, sep));
        if (title.empty()) title = trim(n.substr(sep + 3));
    } else if (title.empty()) {
        title = trim(n);
    }
}

size_t MusicLibrary::scan(FileSystem &fs, const std::vector<std::string> &roots)
{
    size_t before = tracks_.size();
    auto addFile = [&](const std::string &dir, const DirEntry &e, Playlist &pl) {
        Track t;
        t.path = dir + "/" + e.name;
        t.sizeBytes = e.size;
        std::string head;
        if (fs.readFile(t.path, head, 64 * 1024)) {
            Id3Info id = parseId3(head);
            t.title = id.title;
            t.artist = id.artist;
            t.album = id.album;
        }
        titleFromFilename(e.name, t.title, t.artist);
        if (t.artist.empty()) t.artist = "Artista desconhecido";
        pl.tracks.push_back(tracks_.size());
        tracks_.push_back(std::move(t));
    };
    auto byName = [](const DirEntry &a, const DirEntry &b) { return a.name < b.name; };
    for (const auto &root : roots) {
        std::vector<DirEntry> ents;
        if (!fs.listDir(root, ents)) continue;
        std::sort(ents.begin(), ents.end(), byName);
        Playlist loose{"Soltas", root, {}};
        for (const auto &e : ents) {
            if (e.isDir) {
                if (e.name.empty() || e.name[0] == '.') continue;
                Playlist pl{e.name, root + "/" + e.name, {}};
                std::vector<DirEntry> files;
                if (!fs.listDir(pl.source, files)) continue;
                std::sort(files.begin(), files.end(), byName);
                for (const auto &f : files)
                    if (!f.isDir && isMp3(f.name)) addFile(pl.source, f, pl);
                if (!pl.tracks.empty()) playlists_.push_back(std::move(pl));
            } else if (isMp3(e.name)) {
                addFile(root, e, loose);
            }
        }
        if (!loose.tracks.empty()) playlists_.push_back(std::move(loose));
    }
    return tracks_.size() - before;
}

void MusicLibrary::addDemo()
{
    // Invented names: demonstration content only, no audio files behind them.
    struct D { const char *pl, *artist, *title, *album; };
    static const D demo[] = {
        {"Noite Neon", "Aurora Vale", "Luzes da Avenida", "Cidade Acesa"},
        {"Noite Neon", "Aurora Vale", "Satelite", "Cidade Acesa"},
        {"Noite Neon", "Os Paralelos", "Frequencia 88", "Ondas Curtas"},
        {"Noite Neon", "Mirela Costa", "Madrugada Azul", "Horizonte"},
        {"Foco", "Kaito Lindqvist", "Respiracao Lenta", "Estudos"},
        {"Foco", "Kaito Lindqvist", "Grafite e Chuva", "Estudos"},
        {"Foco", "Lumen Trio", "Biblioteca Vazia", "Silencio Util"},
        {"Treino", "Bruma", "Ultimo Round", "Corrida"},
        {"Treino", "Bruma", "Pulso 140", "Corrida"},
        {"Treino", "Vetor Sul", "Pista Molhada", "Asfalto"},
    };
    for (const auto &d : demo) {
        auto it = std::find_if(playlists_.begin(), playlists_.end(),
                               [&](const Playlist &p) { return p.source == "demo" && p.name == d.pl; });
        if (it == playlists_.end()) { playlists_.push_back({d.pl, "demo", {}}); it = playlists_.end() - 1; }
        Track t;
        t.title = d.title;
        t.artist = d.artist;
        t.album = d.album;
        t.demo = true;
        it->tracks.push_back(tracks_.size());
        tracks_.push_back(std::move(t));
    }
}

std::vector<size_t> MusicLibrary::search(const std::string &query) const
{
    std::vector<size_t> r;
    std::string q = lower(trim(query));
    if (q.empty()) return r;
    for (size_t i = 0; i < tracks_.size(); i++) {
        const Track &t = tracks_[i];
        if (lower(t.title).find(q) != std::string::npos || lower(t.artist).find(q) != std::string::npos ||
            lower(t.album).find(q) != std::string::npos)
            r.push_back(i);
    }
    return r;
}

// ---------------------------------------------------------------- queue
void Queue::set(std::vector<size_t> items, size_t startPos)
{
    items_ = std::move(items);
    order_ = items_;
    pos_ = items_.empty() ? 0 : std::min(startPos, items_.size() - 1);
    if (shuffle_) setShuffle(true, static_cast<uint32_t>(items_.size() * 2654435761u));
}

size_t Queue::current() const { return order_.empty() ? size_t(-1) : order_[pos_]; }

bool Queue::next(bool userAction)
{
    if (order_.empty()) return false;
    if (repeat_ == Repeat::One && !userAction) return true;
    if (pos_ + 1 < order_.size()) { pos_++; return true; }
    if (repeat_ == Repeat::Off && !userAction) return false;
    pos_ = 0;
    return true;
}

bool Queue::previous()
{
    if (order_.empty()) return false;
    pos_ = pos_ == 0 ? order_.size() - 1 : pos_ - 1;
    return true;
}

void Queue::setShuffle(bool on, uint32_t seed)
{
    if (order_.empty()) { shuffle_ = on; return; }
    size_t cur = current();
    shuffle_ = on;
    order_ = items_;
    if (on) {
        uint32_t s = seed ? seed : 0x9E3779B9u;
        for (size_t i = order_.size(); i > 1; i--) {   // Fisher-Yates with xorshift
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            std::swap(order_[i - 1], order_[s % i]);
        }
        // keep the current track playing: move it to the front
        auto it = std::find(order_.begin(), order_.end(), cur);
        std::iter_swap(order_.begin(), it);
        pos_ = 0;
    } else {
        auto it = std::find(order_.begin(), order_.end(), cur);
        pos_ = static_cast<size_t>(it - order_.begin());
    }
}

}  // namespace ml::music
