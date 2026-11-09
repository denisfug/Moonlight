#include <cstdlib>
#include <cstring>

#include "test.hpp"

namespace t {
std::vector<Case> &registry()
{
    static std::vector<Case> r;
    return r;
}
int failures = 0;
std::string fixtures;
std::string scratch;
}  // namespace t

int main(int argc, char **argv)
{
    t::fixtures = std::getenv("ML_FIXTURES") ? std::getenv("ML_FIXTURES") : "tests/fixtures";
    char tmpl[] = "/tmp/mltest-XXXXXX";
    const char *d = mkdtemp(tmpl);
    t::scratch = d ? d : "/tmp";
    const char *only = argc > 1 ? argv[1] : nullptr;
    int run = 0;
    for (const auto &c : t::registry()) {
        if (only && !std::strstr(c.name, only)) continue;
        int before = t::failures;
        std::printf("%-40s", c.name);
        std::fflush(stdout);
        c.fn();
        std::printf("%s\n", t::failures == before ? "ok" : "FAILED");
        run++;
    }
    std::printf("\n%d tests, %d failed checks\n", run, t::failures);
    return t::failures ? 1 : 0;
}
