// tests/test_recommend.cpp — hybrid recommendation engine unit tests
#include "platform/host/host_platform.hpp"
#include "recommend/recommend.hpp"
#include "storage/store.hpp"
#include "test.hpp"

using namespace ml;
using namespace ml::recommend;

// ---- Profile serialization roundtrip ----------------------------------------
TEST(recommend_profile_roundtrip) {
    Profile p;
    p.kind = ContentKind::Music;
    p.eventCount = 42;
    TagScore t1; t1.tag = "g:pop";  t1.score = 2.5f;
    TagScore t2; t2.tag = "a:test"; t2.score = 1.0f;
    p.tags.push_back(t1);
    p.tags.push_back(t2);

    Json j = saveProfile(p);
    Profile p2 = loadProfile(j);

    CHECK(p2.kind == ContentKind::Music);
    CHECK(p2.eventCount == 42u);
    CHECK(p2.tags.size() == 2u);
    CHECK(p2.tags[0].tag == "g:pop");
    CHECK(p2.tags[0].score > 2.4f && p2.tags[0].score < 2.6f);
    CHECK(p2.tags[1].tag == "a:test");
}

// ---- Engine load/record/score basic flow ------------------------------------
TEST(recommend_engine_record_and_score) {
    host::HostFileSystem fs(t::scratch);
    fs.makeDirs("/rec1");
    JsonStore store(fs, "/rec1");

    Engine eng;
    eng.load(store, ContentKind::Music);

    CHECK(eng.isColdStart());  // no events yet

    Event ev;
    ev.kind   = ContentKind::Music;
    ev.type   = EventKind::PlayFinish;
    ev.id     = "id123";
    ev.genre  = "rock";
    ev.artist = "Acme Band";
    ev.tsMs   = 1700000000000ULL;

    // Record several events to exit cold start (threshold = 10)
    for (int i = 0; i < 11; i++) eng.record(store, ev);

    CHECK(!eng.isColdStart());

    // Score two candidates: rock gets positive score, jazz gets ~0
    std::vector<Candidate> cands;
    Candidate c1; c1.id = "a"; c1.genre = "rock";  c1.subtitle = "Acme Band"; cands.push_back(c1);
    Candidate c2; c2.id = "b"; c2.genre = "jazz";  c2.subtitle = "Other";     cands.push_back(c2);

    eng.score(cands, false);

    // rock/Acme Band should rank higher than jazz/Other
    CHECK(cands[0].genre == "rock");
    CHECK(cands[0].score > cands[1].score);
}

// ---- Negative events reduce score -------------------------------------------
TEST(recommend_negative_events_reduce_score) {
    host::HostFileSystem fs(t::scratch);
    fs.makeDirs("/rec2");
    JsonStore store(fs, "/rec2");

    Engine eng;
    eng.load(store, ContentKind::Music);

    // Build up a positive score for "pop"
    Event pos;
    pos.kind  = ContentKind::Music;
    pos.type  = EventKind::PlayFinish;
    pos.genre = "pop";
    pos.tsMs  = 1700000000000ULL;
    for (int i = 0; i < 5; i++) eng.record(store, pos);

    // Build up a larger negative score for "pop"
    Event neg;
    neg.kind  = ContentKind::Music;
    neg.type  = EventKind::PlaySkip;
    neg.genre = "pop";
    neg.tsMs  = 1700000000000ULL;
    for (int i = 0; i < 20; i++) eng.record(store, neg);

    // Score pop vs jazz (neutral)
    std::vector<Candidate> cands;
    Candidate c1; c1.id = "a"; c1.genre = "pop";  cands.push_back(c1);
    Candidate c2; c2.id = "b"; c2.genre = "jazz"; cands.push_back(c2);
    eng.score(cands, false);

    // pop score must be below jazz score (jazz = 0, pop = negative)
    float popScore = 0.f, jazzScore = 0.f;
    for (const auto &c : cands) {
        if (c.genre == "pop")  popScore  = c.score;
        if (c.genre == "jazz") jazzScore = c.score;
    }
    CHECK(popScore < jazzScore);
}

// ---- Diversity penalty: sorted results are non-increasing in score ----------
TEST(recommend_diversity_penalty_monotone) {
    host::HostFileSystem fs(t::scratch);
    fs.makeDirs("/rec3");
    JsonStore store(fs, "/rec3");

    Engine eng;
    eng.load(store, ContentKind::Music);

    // Record strong preference for "rock"
    Event ev;
    ev.kind  = ContentKind::Music;
    ev.type  = EventKind::Favorite;
    ev.genre = "rock";
    ev.tsMs  = 1700000000000ULL;
    for (int i = 0; i < 10; i++) eng.record(store, ev);

    // 5 rock candidates + 1 jazz
    std::vector<Candidate> cands;
    for (int i = 0; i < 5; i++) {
        Candidate c;
        c.id    = "rock" + std::to_string(i);
        c.genre = "rock";
        cands.push_back(c);
    }
    Candidate j; j.id = "jazz0"; j.genre = "jazz"; cands.push_back(j);

    eng.score(cands, true);

    // After sort, verify scores are non-increasing (output is sorted desc)
    bool monotone = true;
    for (size_t i = 1; i < cands.size(); i++) {
        if (cands[i].score > cands[i-1].score + 0.001f) { monotone = false; break; }
    }
    CHECK(monotone);
}

// ---- ContentKind persisted correctly ----------------------------------------
TEST(recommend_content_kind_persisted) {
    host::HostFileSystem fs(t::scratch);
    fs.makeDirs("/rec4");
    JsonStore store(fs, "/rec4");

    {
        Engine eng;
        eng.load(store, ContentKind::Game);

        Event ev;
        ev.kind  = ContentKind::Game;
        ev.type  = EventKind::PlayStart;
        ev.genre = "action";
        ev.tsMs  = 1700000000000ULL;
        eng.record(store, ev);
    }
    {
        Engine eng2;
        eng2.load(store, ContentKind::Game);
        CHECK(eng2.profile().kind == ContentKind::Game);
        CHECK(eng2.profile().eventCount == 1u);
        CHECK(!eng2.profile().tags.empty());
        CHECK(eng2.profile().tags[0].tag.find("action") != std::string::npos);
    }
}
