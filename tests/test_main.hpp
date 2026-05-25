#pragma once
#include <vector>

namespace triples::tests {

struct TestCase {
    const char* name;
    void (*run)();
};

std::vector<TestCase>& registry();

struct TestRecord {
    static bool        ok;
    static int         checks;
    static int         fails;
    static const char* current_test;
    static void        fail(const char* file, int line, const char* expr);
};

struct Registrar {
    Registrar(const char* name, void (*fn)()) {
        registry().push_back({name, fn});
    }
};

}  // namespace triples::tests

#define TEST_CONCAT_(a, b) a##b
#define TEST_CONCAT(a, b)  TEST_CONCAT_(a, b)

#define TEST(NAME)                                                                       \
    static void TEST_CONCAT(test_, NAME)();                                              \
    static ::triples::tests::Registrar TEST_CONCAT(reg_, NAME){#NAME,                    \
                                                              TEST_CONCAT(test_, NAME)}; \
    static void TEST_CONCAT(test_, NAME)()

#define CHECK(expr)                                                            \
    do {                                                                       \
        ++::triples::tests::TestRecord::checks;                                \
        if (!(expr)) ::triples::tests::TestRecord::fail(__FILE__, __LINE__, #expr); \
    } while (0)

#define CHECK_EQ(a, b)                                                                       \
    do {                                                                                     \
        ++::triples::tests::TestRecord::checks;                                              \
        auto _x = (a);                                                                       \
        auto _y = (b);                                                                       \
        if (!(_x == _y))                                                                     \
            ::triples::tests::TestRecord::fail(__FILE__, __LINE__, #a " == " #b);            \
    } while (0)
