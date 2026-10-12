// Shared main for the unit and integration test binaries.
// An optional argument runs only the tests whose name contains it.
#include "minitest.h"

int main(int argc, char** argv) {
    const std::string filter = argc > 1 ? argv[1] : "";
    int ran = 0;
    for (const auto& test : minitest::registry()) {
        if (test.name.find(filter) == std::string::npos) continue;
        const int failures_before = minitest::failure_count();
        test.body();
        ++ran;
        const bool passed = minitest::failure_count() == failures_before;
        std::cout << (passed ? "PASS " : "FAIL ") << test.name << std::endl;
    }
    std::cout << ran << " tests run, " << minitest::failure_count()
              << " check failures\n";
    return minitest::failure_count() == 0 ? 0 : 1;
}
