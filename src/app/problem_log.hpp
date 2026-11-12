// Playback problems, kept so the owner can photograph them for whoever is developing the app:
// what was being played (name + poster), from which source, why it did not play, and the technical
// details (step, system codes). Shown in Configuracoes > Problemas ao reproduzir. Saved in
// <data>/problems.json, newest first, bounded.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "storage/store.hpp"

namespace ml {

struct Problem {
    int64_t when = 0;        // seconds since 1970 (console clock)
    std::string title;       // what the user was trying to watch
    std::string poster;      // poster URL ("" = none)
    std::string source;      // source / link name
    std::string where;       // host of the link, or "arquivo local"
    std::string reason;      // one sentence, shown in the list
    std::string details;     // technical text (several lines)
};

// What is being played, passed along to the player so a failure can be recorded with its context.
struct PlayContext {
    std::string title, poster, source;
    std::vector<std::pair<std::string, std::string>> requestHeaders;   // source's proxyHeaders.request
};

class ProblemLog {
public:
    explicit ProblemLog(JsonStore &store) : store_(store) {}
    void load();
    // Adds on top; an entry with the same title + reason as the newest one only refreshes it.
    void add(Problem p);
    void clear();
    const std::vector<Problem> &entries() const { return entries_; }

    static constexpr size_t kMax = 40;

private:
    void save();
    JsonStore &store_;
    std::vector<Problem> entries_;
};

// "https://cdn.example.com:8443/a/b.mp4?token=1" -> "cdn.example.com" (never the path or query: tokens).
std::string hostForLog(const std::string &location);

}  // namespace ml
