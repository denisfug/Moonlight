// Relevance ranking for multi-source music search results.
// Algorithm ported from Rheoson (Apache-2.0):
//   _norm / _tokens / _text_score / _score_candidate in
//   api/app/services/search_service.py
// Attribution: Rheoson contributors (Apache-2.0).
#include "music/rank.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <set>
#include <unordered_map>

namespace ml::rank {

namespace {

// Minimal NFKD-style accent stripping for the most common Latin diacritics.
// Full Unicode decomposition is not needed for music metadata matching.
// Map: accented char (UTF-8 single-byte range 0xC0-0xFF) → ASCII base.
static const char kAccentBase[64] = {
    // 0xC0-0xCF: À Á Â Ã Ä Å Æ Ç È É Ê Ë Ì Í Î Ï
    'a','a','a','a','a','a','a','c','e','e','e','e','i','i','i','i',
    // 0xD0-0xDF: Ð Ñ Ò Ó Ô Õ Ö × Ø Ù Ú Û Ü Ý Þ ß
    'd','n','o','o','o','o','o', 0 ,'o','u','u','u','u','y', 0 ,'s',
    // 0xE0-0xEF: à á â ã ä å æ ç è é ê ë ì í î ï
    'a','a','a','a','a','a','a','c','e','e','e','e','i','i','i','i',
    // 0xF0-0xFF: ð ñ ò ó ô õ ö ÷ ø ù ú û ü ý þ ÿ
    'd','n','o','o','o','o','o', 0 ,'o','u','u','u','u','y', 0 ,'y',
};

// Returns ASCII replacement for a byte in range [0xC0,0xFF], or 0.
static char accentMap(unsigned char c)
{
    if (c < 0xC0) return 0;
    char r = kAccentBase[c - 0xC0];
    return r;
}

// Skip stopwords (same set as Rheoson).
static bool isStopword(const std::string &w)
{
    static const char *kStop[] = {
        "the","a","an","and","with","feat","ft","featuring",
        "official","video","audio","lyric","lyrics","hd","hq",
        "remaster","remastered","version","edit","explicit","new",
        nullptr
    };
    for (int i = 0; kStop[i]; ++i)
        if (w == kStop[i]) return true;
    return false;
}

}  // namespace

std::string normalize(const std::string &s)
{
    std::string out;
    out.reserve(s.size());

    // State machine: skip bracketed annotations ((...), [...], {...})
    int depth = 0;
    for (size_t i = 0; i < s.size(); ) {
        unsigned char c = (unsigned char)s[i];

        // Track bracket depth
        if (c == '(' || c == '[' || c == '{') { depth++; i++; continue; }
        if (c == ')' || c == ']' || c == '}') { if (depth > 0) depth--; i++; continue; }
        if (depth > 0) { i++; continue; }

        // Handle multi-byte UTF-8 sequences
        if (c >= 0xC0 && c < 0xE0 && i + 1 < s.size()) {
            // 2-byte sequence: check if it's a Latin accented character
            unsigned char c2 = (unsigned char)s[i+1];
            // Latin-1 Supplement: 0xC3 0x80-0xBF (U+00C0-U+00FF)
            if (c == 0xC3 && c2 >= 0x80) {
                // Map to [0xC0,0xFF] range
                unsigned char latin1 = (unsigned char)(0xC0 + (c2 - 0x80));
                char base = accentMap(latin1);
                if (base) out += (char)std::tolower((unsigned char)base);
                i += 2; continue;
            }
            // Other 2-byte sequences: skip
            i += 2; continue;
        }
        if (c >= 0xE0) {
            // 3-byte or more: skip
            if (c < 0xF0) { i += 3; continue; }
            if (c < 0xF8) { i += 4; continue; }
            i++; continue;
        }

        // ASCII
        if (std::isalpha(c)) out += (char)std::tolower(c);
        else if (std::isdigit(c)) out += (char)c;
        else out += ' ';
        i++;
    }

    // Collapse whitespace
    std::string result;
    result.reserve(out.size());
    bool prev_space = true;
    for (char ch : out) {
        if (ch == ' ') {
            if (!prev_space) { result += ' '; prev_space = true; }
        } else {
            result += ch;
            prev_space = false;
        }
    }
    // Trim trailing space
    while (!result.empty() && result.back() == ' ') result.pop_back();
    return result;
}

// Split normalized string into non-stopword tokens
static std::vector<std::string> tokens(const std::string &s)
{
    std::string norm = normalize(s);
    std::vector<std::string> v;
    std::string tok;
    for (char c : norm) {
        if (c == ' ') {
            if (!tok.empty()) {
                if (!isStopword(tok)) v.push_back(tok);
                tok.clear();
            }
        } else {
            tok += c;
        }
    }
    if (!tok.empty() && !isStopword(tok)) v.push_back(tok);
    return v;
}

float textScore(const std::string &want, const std::string &got)
{
    std::string nw = normalize(want), ng = normalize(got);
    if (nw.empty() || ng.empty()) return 0.0f;
    if (nw == ng) return 1.0f;

    auto tw = tokens(want);
    auto tg = tokens(got);
    if (tw.empty() || tg.empty()) return 0.0f;

    std::set<std::string> sw(tw.begin(), tw.end());
    std::set<std::string> sg(tg.begin(), tg.end());

    // Subset check
    bool wInG = true, gInW = true;
    for (auto &t : sw) if (!sg.count(t)) { wInG = false; break; }
    for (auto &t : sg) if (!sw.count(t)) { gInW = false; break; }
    if (wInG || gInW) return 0.9f;

    // Jaccard
    int inter = 0;
    for (auto &t : sw) if (sg.count(t)) inter++;
    int uni = (int)(sw.size() + sg.size()) - inter;
    if (uni <= 0) return 0.0f;
    return (float)inter / (float)uni;
}

float score(const std::string &qTitle, const std::string &qArtist,
            const catalog::Song &s)
{
    float ts = textScore(qTitle, s.title);
    float as_ = qArtist.empty() ? 1.0f : textScore(qArtist, s.artist);
    float combined = 0.72f * ts + 0.28f * as_;

    // Cover/remix guard: right title, clearly wrong artist → cap below floor
    if (!qArtist.empty() && as_ < 0.4f)
        combined = std::min(combined, kFloor - 0.01f);

    return combined;
}

void rankInPlace(const std::string &qTitle, const std::string &qArtist,
                 std::vector<catalog::Song> &songs)
{
    // Score and filter
    std::vector<std::pair<float,size_t>> scored;
    scored.reserve(songs.size());
    for (size_t i = 0; i < songs.size(); i++) {
        float s = score(qTitle, qArtist, songs[i]);
        if (s >= kFloor) scored.push_back({s, i});
    }
    // Sort descending
    std::sort(scored.begin(), scored.end(),
              [](const auto &a, const auto &b){ return a.first > b.first; });
    // Rebuild
    std::vector<catalog::Song> result;
    result.reserve(scored.size());
    for (auto &[s, i] : scored) result.push_back(std::move(songs[i]));
    songs = std::move(result);
}

void sortFreeText(const std::string &query, std::vector<catalog::Song> &songs)
{
    std::vector<std::pair<float, size_t>> scored;
    scored.reserve(songs.size());
    for (size_t i = 0; i < songs.size(); i++) {
        const catalog::Song &s = songs[i];
        float best = std::max(textScore(query, s.title), textScore(query, s.artist));
        best = std::max(best, textScore(query, s.title + " " + s.artist));
        scored.push_back({best, i});
    }
    std::stable_sort(scored.begin(), scored.end(),
                     [](const auto &a, const auto &b) { return a.first > b.first; });
    std::vector<catalog::Song> result;
    result.reserve(scored.size());
    for (auto &p : scored) result.push_back(std::move(songs[p.second]));
    songs = std::move(result);
}

void capPerArtist(std::vector<catalog::Song> &songs, int maxPerArtist)
{
    std::unordered_map<std::string,int> counts;
    std::vector<catalog::Song> result;
    result.reserve(songs.size());
    for (auto &song : songs) {
        std::string key = normalize(song.artist);
        if (counts[key] < maxPerArtist) {
            counts[key]++;
            result.push_back(std::move(song));
        }
    }
    songs = std::move(result);
}

}  // namespace ml::rank
