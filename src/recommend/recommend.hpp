// Moonlight — Hybrid Recommendation Engine
// Content-based + collaborative filtering + contextual signals.
// Runs fully local (C++17, no network), async-safe.
// All state is persisted via JsonStore; no static constructors.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/json.hpp"
#include "storage/store.hpp"

namespace ml {
namespace recommend {

// ---- Event types ----------------------------------------------------------

enum class EventKind : uint8_t {
    PlayStart   = 0,  // user pressed play
    PlayFinish  = 1,  // track/movie played to end (strong positive)
    PlaySkip    = 2,  // user skipped (mild negative)
    Favorite    = 3,  // added to favorites (strong positive)
    Unfavorite  = 4,
    Search      = 5,  // user searched for this term
    ClickRec    = 6,  // user clicked a recommendation
    Ignore      = 7,  // user dismissed / "not interested"
    AddPlaylist = 8,  // added to playlist
};

enum class ContentKind : uint8_t { Music = 0, Movie = 1, Series = 2, Game = 3 };

struct Event {
    ContentKind kind;
    EventKind   type;
    std::string id;       // track videoId / movie id / series id
    std::string genre;    // primary genre tag (may be empty)
    std::string artist;   // for music: artist name
    uint64_t    tsMs = 0; // unix epoch ms
};

// ---- Profile --------------------------------------------------------------
// Lightweight taste model — a frequency map of tags (genre/artist/etc.)
// Updated incrementally; never requires a full re-scan.

struct TagScore {
    std::string tag;
    float       score = 0.f;
};

struct Profile {
    ContentKind kind;
    std::vector<TagScore> tags;   // sorted by score desc
    uint32_t eventCount = 0;      // total events seen
};

// ---- Candidate scoring ----------------------------------------------------

struct Candidate {
    std::string id;
    std::string title;
    std::string subtitle;   // artist / year / network
    std::string genre;
    std::string art;
    float       score = 0.f;
};

// ---- Engine ---------------------------------------------------------------

class Engine {
public:
    explicit Engine() = default;

    // Load persisted profile from store; call once at app init.
    void load(JsonStore &store, ContentKind kind);

    // Record an interaction event (call from UI thread; O(log n)).
    void record(JsonStore &store, const Event &ev);

    // Score a list of candidates using the current profile + diversity penalty.
    // candidates is modified in-place (scores filled); sorted by score desc on return.
    void score(std::vector<Candidate> &candidates, bool diversity = true) const;

    // Cold-start: returns true when profile has fewer than kColdStartThreshold events.
    bool isColdStart() const { return profile_.eventCount < kColdStartThreshold; }

    const Profile &profile() const { return profile_; }

private:
    static constexpr uint32_t kColdStartThreshold = 10;
    static constexpr float    kDecayHalfLifeDays  = 30.f;  // events older than this lose weight

    Profile profile_;

    std::string storeKey() const;
    void        updateTag(const std::string &tag, float delta);
    void        trim();   // keep top-N tags only
    void        save(JsonStore &store) const;
    float       tagScore(const std::string &tag) const;
};

// ---- Persistence helpers --------------------------------------------------
// Profile is stored as JSON: {"kind":0,"count":N,"tags":[{"t":"pop","s":1.5},...]}

Profile loadProfile(const Json &j);
Json    saveProfile(const Profile &p);

}  // namespace recommend
}  // namespace ml
