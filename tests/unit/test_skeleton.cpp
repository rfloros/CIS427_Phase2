#include "minitest.h"

TEST_CASE("Slice0 runner reports a passing check") {
    CHECK(1 + 1 == 2);
    CHECK_EQ(std::string("a") + "b", std::string("ab"));
}
