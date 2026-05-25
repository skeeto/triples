// Minimal test harness: each test file registers test functions into a list,
// main() runs them all, prints pass/fail, exits with status reflecting failures.
//
// Usage in a test file:
//   TEST(name) { CHECK(condition); CHECK_EQ(a, b); ... }
//
// No external dependencies. Failures don't abort, so all tests run.

#include "test_main.hpp"

#include <cstdio>
#include <cstdlib>

namespace triples::tests {

std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}

bool TestRecord::ok = true;
int  TestRecord::checks = 0;
int  TestRecord::fails = 0;
const char* TestRecord::current_test = "";

void TestRecord::fail(const char* file, int line, const char* expr) {
    ok = false;
    ++fails;
    std::fprintf(stderr, "  FAIL  %s  %s:%d  %s\n", current_test, file, line, expr);
}

}  // namespace triples::tests

int main() {
    using namespace triples::tests;
    int total = 0, passed = 0;
    for (const auto& tc : registry()) {
        TestRecord::ok = true;
        TestRecord::current_test = tc.name;
        ++total;
        tc.run();
        if (TestRecord::ok) {
            ++passed;
        } else {
            std::fprintf(stderr, "FAIL: %s\n", tc.name);
        }
    }
    std::printf("%d/%d tests passed (%d checks, %d failures)\n",
                passed, total, TestRecord::checks, TestRecord::fails);
    return passed == total ? 0 : 1;
}
