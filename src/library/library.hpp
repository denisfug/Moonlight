// User library: favorites, watch history and playback progress ("continue watching").
// Video items and music tracks are kept in separate lists (different identities and fields).
#pragma once
#include <string>
#include <vector>

#include "storage/store.hpp"

namespace ml {

struct LibraryItem {
    std::string id;        // meta id (movie/series/channel)
    std::string type;      // movie | series | tv | channel
    std::string name;
    std::string poster;
    std::string videoId;   // episode id for series progress
    int season = 0, episode = 0;
    int64_t positionMs = 0, durationMs = 0;
    int64_t updatedAt = 0;
    double progress() const { return durationMs > 0 ? double(positionMs) / double(durationMs) : 0.0; }
};

class Library {
public:
    explicit Library(JsonStore &store) : store_(store) {}
    void load();
    bool save();

    bool isFavorite(const std::string &id) const;
    void toggleFavorite(const LibraryItem &item, int64_t now);
    const std::vector<LibraryItem> &favorites() const { return favorites_; }

    // Records progress; finished items (>= 92%) leave "continue watching" but stay in history.
    void updateProgress(const LibraryItem &item, int64_t now);
    std::vector<LibraryItem> continueWatching(size_t max = 20) const;
    const std::vector<LibraryItem> &history() const { return history_; }
    const LibraryItem *progressFor(const std::string &id, const std::string &videoId = "") const;

    static constexpr size_t kMaxHistory = 300;

private:
    JsonStore &store_;
    std::vector<LibraryItem> favorites_, history_;
};

}  // namespace ml
