// A tiny test runner: TEST_CASE registers a test, CHECK records a failure
// without stopping, so one test can report several problems at once.
#ifndef MINITEST_H
#define MINITEST_H

#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace minitest {

struct TestEntry {
    std::string name;
    std::function<void()> body;
};

inline std::vector<TestEntry>& registry() {
    static std::vector<TestEntry> tests;
    return tests;
}

inline int& failure_count() {
    static int failures = 0;
    return failures;
}

struct Registrar {
    Registrar(const std::string& name, std::function<void()> body) {
        registry().push_back({name, std::move(body)});
    }
};

inline void report_failure(const char* file, int line, const std::string& what) {
    ++failure_count();
    std::cerr << "  FAIL " << file << ":" << line << ": " << what << "\n";
}

}  // namespace minitest

#define MT_CONCAT_INNER(a, b) a##b
#define MT_CONCAT(a, b) MT_CONCAT_INNER(a, b)

#define TEST_CASE(name)                                                        \
    static void MT_CONCAT(test_body_, __LINE__)();                             \
    static minitest::Registrar MT_CONCAT(test_reg_, __LINE__)(                 \
        name, MT_CONCAT(test_body_, __LINE__));                                \
    static void MT_CONCAT(test_body_, __LINE__)()

#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) minitest::report_failure(__FILE__, __LINE__, #expr);      \
    } while (0)

// Compares two values and prints both when they differ.
#define CHECK_EQ(actual, expected)                                             \
    do {                                                                       \
        auto mt_actual = (actual);                                             \
        auto mt_expected = (expected);                                         \
        if (!(mt_actual == mt_expected)) {                                     \
            std::ostringstream mt_out;                                         \
            mt_out << #actual << " == " << #expected << " (got <" << mt_actual \
                   << ">, wanted <" << mt_expected << ">)";                    \
            minitest::report_failure(__FILE__, __LINE__, mt_out.str());        \
        }                                                                      \
    } while (0)

#endif
