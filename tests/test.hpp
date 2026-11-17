// Minimal test harness (no exceptions, no dependencies).
#pragma once
#include <cstdio>
#include <string>
#include <vector>

namespace t {

struct Case {
    const char *name;
    void (*fn)();
};

std::vector<Case> &registry();
extern int failures;
extern std::string fixtures;   // path to tests/fixtures
extern std::string scratch;    // writable temp dir for this run

struct Reg {
    Reg(const char *n, void (*f)()) { registry().push_back({n, f}); }
};

}  // namespace t

#define TEST(name)                                   \
    static void name();                              \
    static t::Reg reg_##name(#name, &name);          \
    static void name()

#define CHECK(cond)                                                                     \
    do {                                                                                \
        if (!(cond)) {                                                                  \
            t::failures++;                                                              \
            std::printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
        }                                                                               \
    } while (0)

#define CHECK_EQ(a, b)                                                                  \
    do {                                                                                \
        auto va_ = (a);                                                                 \
        auto vb_ = (b);                                                                 \
        if (!(va_ == vb_)) {                                                            \
            t::failures++;                                                              \
            std::printf("    FAIL %s:%d: %s == %s\n", __FILE__, __LINE__, #a, #b);      \
        }                                                                               \
    } while (0)
