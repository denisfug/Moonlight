// Relevance ranking for multi-source music search results.
// Algorithm ported from Rheoson (Apache-2.0, github.com/morluto/Rheoson):
//   score = 0.72 * title_score + 0.28 * artist_score
// Normalization: lowercase, NFKD accent stripping, remove bracketed tags,
// collapse to ASCII alpha-num tokens, skip stopwords.
// Jaccard overlap for partial matches.
// No static constructors; no exceptions.
#pragma once
#include <string>
#include <vector>

#include "music/catalog.hpp"

namespace ml::rank {

// Rank songs by relevance to (query_title, query_artist). Songs with score
// below kFloor are removed. The result is sorted best-first.
// If query_artist is empty, only title scoring is used.
constexpr float kFloor = 0.35f;

struct Scored {
    catalog::Song song;
    float score;
};

// Normalize a string: lowercase, strip accents, remove bracketed annotations,
// collapse non-alpha-num runs to spaces. Returns ASCII tokens joined by space.
std::string normalize(const std::string &s);

// Score two strings against each other (0.0–1.0):
//   1.0  exact (normalized)
//   0.9  one is a subset of the other's tokens
//   Jaccard otherwise, 0.0 for empty
float textScore(const std::string &want, const std::string &got);

// Combined score: 0.72*title + 0.28*artist. If artist < 0.4 the total is
// capped at kFloor-0.01 (cover/remix guard).
float score(const std::string &qTitle, const std::string &qArtist,
            const catalog::Song &s);

// Filter + sort a result list. Modifies in-place and removes low-scoring items.
void rankInPlace(const std::string &qTitle, const std::string &qArtist,
                 std::vector<catalog::Song> &songs);

// Diversity cap: at most maxPerArtist songs with the same normalized artist
// name are kept (useful for trending/recommendation shelves). Call after
// rankInPlace if needed.
void capPerArtist(std::vector<catalog::Song> &songs, int maxPerArtist = 3);

}  // namespace ml::rank
