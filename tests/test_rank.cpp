// Tests for the relevance ranking module (src/music/rank.*).
// Algorithm ported from Rheoson (Apache-2.0).
#include "music/rank.hpp"
#include "test.hpp"

using namespace ml;
using namespace ml::rank;

// --- normalize ---

TEST(rank_normalize_basic)
{
    CHECK_EQ(normalize("Hello World"), std::string("hello world"));
    CHECK_EQ(normalize("  spaces  "), std::string("spaces"));
    CHECK_EQ(normalize("under_score"), std::string("under score"));
    CHECK_EQ(normalize(""), std::string(""));
}

TEST(rank_normalize_brackets)
{
    CHECK_EQ(normalize("Song (Official Video)"), std::string("song"));
    CHECK_EQ(normalize("Track [Remastered 2020]"), std::string("track"));
    CHECK_EQ(normalize("Mix {Extended}"), std::string("mix"));
    // Brackets don't eat trailing content
    CHECK_EQ(normalize("A (feat. B) C"), std::string("a c"));
}

TEST(rank_normalize_accents)
{
    // é → e
    CHECK_EQ(normalize("\xc3\xa9l\xc3\xa8ve"), std::string("eleve"));
    // ñ → n: Ma + U+00F1 (0xC3 0xB1 in UTF-8) + ana = Mañana → manana
    // Note: split literal to avoid \xb1a being parsed as a single hex escape
    CHECK_EQ(normalize("Ma\xc3\xb1" "ana"), std::string("manana"));
    // ü → u
    CHECK_EQ(normalize("M\xc3\xbcnchen"), std::string("munchen"));
}

TEST(rank_normalize_digits)
{
    CHECK_EQ(normalize("Track 2 Remix"), std::string("track 2 remix"));
}

// --- textScore ---

TEST(rank_text_score_exact)
{
    CHECK_EQ(textScore("hello", "hello"), 1.0f);
    CHECK_EQ(textScore("Hello", "hello"), 1.0f);   // case insensitive
}

TEST(rank_text_score_subset)
{
    // "dark side of the moon" contains "dark side"
    float s = textScore("dark side", "dark side of the moon");
    CHECK(s >= 0.89f && s <= 0.91f);
}

TEST(rank_text_score_empty)
{
    CHECK_EQ(textScore("", "hello"), 0.0f);
    CHECK_EQ(textScore("hello", ""), 0.0f);
}

TEST(rank_text_score_different)
{
    // Completely different → low score
    float s = textScore("metallica", "beethoven symphony");
    CHECK(s < 0.4f);
}

TEST(rank_text_score_overlap)
{
    // Some shared tokens: "dark" and "side" are common → Jaccard = 2/5 = 0.4
    float s = textScore("dark side moon", "pink floyd dark side");
    CHECK(s >= 0.4f);
}

// --- score (combined) ---

TEST(rank_score_good_match)
{
    catalog::Song s;
    s.title = "Bohemian Rhapsody";
    s.artist = "Queen";
    float sc = score("Bohemian Rhapsody", "Queen", s);
    CHECK(sc >= 0.95f);
}

TEST(rank_score_cover_guard)
{
    // Right title, clearly wrong artist → capped below floor
    catalog::Song s;
    s.title = "Bohemian Rhapsody";
    s.artist = "Some Random Cover Band";
    float sc = score("Bohemian Rhapsody", "Queen", s);
    CHECK(sc < kFloor);
}

TEST(rank_score_no_artist_query)
{
    // If we only have title, don't penalize for artist
    catalog::Song s;
    s.title = "Stairway to Heaven";
    s.artist = "Led Zeppelin";
    float sc = score("Stairway to Heaven", "", s);
    CHECK(sc >= 0.90f);
}

// --- rankInPlace ---

TEST(rank_rank_in_place_basic)
{
    std::vector<catalog::Song> songs;
    catalog::Song a; a.title = "Bohemian Rhapsody"; a.artist = "Queen";
    catalog::Song b; b.title = "Somebody That I Used to Know"; b.artist = "Gotye";
    catalog::Song c; c.title = "Completely Different Track"; c.artist = "Nobody";
    songs = {c, b, a};

    rankInPlace("Bohemian Rhapsody", "Queen", songs);
    // "Bohemian Rhapsody" by Queen should be first
    CHECK(!songs.empty());
    CHECK_EQ(songs[0].title, std::string("Bohemian Rhapsody"));
}

TEST(rank_rank_in_place_low_scores_removed)
{
    std::vector<catalog::Song> songs;
    catalog::Song a; a.title = "Completely Different"; a.artist = "Nobody";
    catalog::Song b; b.title = "Also Unrelated"; b.artist = "Someone";
    songs = {a, b};

    rankInPlace("Bohemian Rhapsody", "Queen", songs);
    // Both should be below floor and removed
    CHECK(songs.empty());
}

TEST(rank_rank_in_place_empty)
{
    std::vector<catalog::Song> songs;
    rankInPlace("query", "artist", songs);
    CHECK(songs.empty());
}

// --- capPerArtist ---

TEST(rank_cap_per_artist)
{
    std::vector<catalog::Song> songs;
    for (int i = 0; i < 5; i++) {
        catalog::Song s;
        s.title = "Song " + std::to_string(i);
        s.artist = "Same Artist";
        songs.push_back(s);
    }
    catalog::Song other; other.title = "Other"; other.artist = "Other Artist";
    songs.push_back(other);

    capPerArtist(songs, 3);
    // 3 from "Same Artist" + 1 from "Other Artist" = 4
    CHECK_EQ(songs.size(), size_t(4));
}

TEST(rank_cap_per_artist_accent_normalized)
{
    std::vector<catalog::Song> songs;
    catalog::Song a; a.title = "A"; a.artist = "M\xc3\xbcller";
    catalog::Song b; b.title = "B"; b.artist = "Muller";
    catalog::Song c; c.title = "C"; c.artist = "M\xc3\xbcller";
    songs = {a, b, c};

    capPerArtist(songs, 1);
    // "Müller" and "Muller" normalize to "muller" — same bucket → only 1 kept
    CHECK_EQ(songs.size(), size_t(1));
}
