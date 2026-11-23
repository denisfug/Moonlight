// Moonlight — Hybrid Recommendation Engine implementation
// No exceptions (-fno-exceptions), no static constructors.
#include "recommend/recommend.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/json.hpp"

namespace ml {
namespace recommend {

// ---- Tag weight delta per event kind -------------------------------------
static float eventWeight(EventKind k)
{
    switch (k) {
    case EventKind::PlayFinish:  return  1.5f;
    case EventKind::Favorite:    return  2.0f;
    case EventKind::AddPlaylist: return  1.2f;
    case EventKind::ClickRec:    return  0.5f;
    case EventKind::PlayStart:   return  0.3f;
    case EventKind::Search:      return  0.4f;
    case EventKind::PlaySkip:    return -0.4f;
    case EventKind::Unfavorite:  return -1.5f;
    case EventKind::Ignore:      return -0.8f;
    }
    return 0.f;
}

// ---- Profile helpers -------------------------------------------------------
static constexpr size_t kMaxTags = 200;

float Engine::tagScore(const std::string &tag) const
{
    for (const auto &ts : profile_.tags)
        if (ts.tag == tag) return ts.score;
    return 0.f;
}

void Engine::updateTag(const std::string &tag, float delta)
{
    if (tag.empty()) return;
    for (auto &ts : profile_.tags) {
        if (ts.tag == tag) { ts.score += delta; return; }
    }
    TagScore ts;
    ts.tag   = tag;
    ts.score = delta;
    profile_.tags.push_back(ts);
}

void Engine::trim()
{
    // Sort descending, keep top kMaxTags, drop entries with score <= 0.
    std::sort(profile_.tags.begin(), profile_.tags.end(),
              [](const TagScore &a, const TagScore &b){ return a.score > b.score; });
    while (profile_.tags.size() > kMaxTags)
        profile_.tags.pop_back();
    // Remove zero/negative entries beyond top-50 to save memory but keep intentional negatives.
    if (profile_.tags.size() > 50) {
        size_t i = profile_.tags.size();
        while (i > 50 && profile_.tags[i-1].score <= 0.f) --i;
        profile_.tags.resize(i);
    }
}

// ---- Persistence ----------------------------------------------------------
std::string Engine::storeKey() const
{
    switch (profile_.kind) {
    case ContentKind::Music:  return "rec_music.json";
    case ContentKind::Movie:  return "rec_movie.json";
    case ContentKind::Series: return "rec_series.json";
    case ContentKind::Game:   return "rec_game.json";
    }
    return "rec_other.json";
}

static const char *kKind  = "kind";
static const char *kCount = "count";
static const char *kTags  = "tags";
static const char *kTag   = "t";
static const char *kScore = "s";

Profile loadProfile(const Json &j)
{
    Profile p;
    p.kind       = ContentKind(int(j[kKind].asNumber(0)));
    p.eventCount = uint32_t(j[kCount].asNumber(0));
    const Json &arr = j[kTags];
    size_t n = arr.size();
    for (size_t i = 0; i < n; i++) {
        const Json &entry = arr[i];
        TagScore ts;
        ts.tag   = entry[kTag].str("");
        ts.score = float(entry[kScore].asNumber(0));
        if (!ts.tag.empty()) p.tags.push_back(ts);
    }
    return p;
}

Json saveProfile(const Profile &p)
{
    Json j = Json::object();
    j.set(kKind,  Json::number(double(int(p.kind))));
    j.set(kCount, Json::number(double(p.eventCount)));
    Json arr = Json::array();
    for (const auto &ts : p.tags) {
        Json entry = Json::object();
        entry.set(kTag,   Json::string(ts.tag));
        entry.set(kScore, Json::number(double(ts.score)));
        arr.push(entry);
    }
    j.set(kTags, arr);
    return j;
}

void Engine::save(JsonStore &store) const
{
    store.save(storeKey(), saveProfile(profile_));
}

void Engine::load(JsonStore &store, ContentKind kind)
{
    profile_.kind = kind;
    Json j = store.load(storeKey());
    if (!j.isNull()) {
        Profile loaded = loadProfile(j);
        if (loaded.kind == kind) profile_ = loaded;
    }
}

// ---- Record ---------------------------------------------------------------
void Engine::record(JsonStore &store, const Event &ev)
{
    float w = eventWeight(ev.type);
    if (!ev.genre.empty())  updateTag("g:" + ev.genre,  w);
    if (!ev.artist.empty()) updateTag("a:" + ev.artist, w * 0.8f);
    if (!ev.id.empty())     updateTag("i:" + ev.id,     w * 0.5f);
    profile_.eventCount++;
    trim();
    save(store);
}

// ---- Score ----------------------------------------------------------------
void Engine::score(std::vector<Candidate> &candidates, bool diversity) const
{
    // 1. Content-based score from profile tags.
    for (auto &c : candidates) {
        float s = 0.f;
        if (!c.genre.empty())    s += tagScore("g:" + c.genre)  * 1.0f;
        if (!c.subtitle.empty()) s += tagScore("a:" + c.subtitle) * 0.6f;
        if (!c.id.empty())       s += tagScore("i:" + c.id)      * 0.4f;
        c.score = s;
    }

    // 2. Diversity penalty: reduce score for items in already-high-scored genres.
    if (diversity) {
        // Count how many items already ranked high share each genre.
        // Simple approach: sort, then apply diminishing returns per genre.
        std::sort(candidates.begin(), candidates.end(),
                  [](const Candidate &a, const Candidate &b){ return a.score > b.score; });
        std::vector<std::pair<std::string,int>> genreCount;
        for (auto &c : candidates) {
            if (c.genre.empty()) continue;
            bool found = false;
            for (auto &gc : genreCount) {
                if (gc.first == c.genre) { gc.second++; found = true; break; }
            }
            if (!found) genreCount.push_back({c.genre, 1});
            int cnt = 1;
            for (auto &gc : genreCount) if (gc.first == c.genre) { cnt = gc.second; break; }
            if (cnt > 3) c.score *= 0.7f;   // already 3+ of same genre ranked higher
        }
    }

    // 3. Final sort.
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate &a, const Candidate &b){ return a.score > b.score; });
}

}  // namespace recommend
}  // namespace ml
