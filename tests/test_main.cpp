// main() of the tests written with tests/Test.h

#include "Test.h"

int main() {
    for (const auto& c : test::registry()) {
        int before = test::failures();
        c.body();
        std::cout << (test::failures() == before ? "PASS " : "FAIL ") << c.name << "\n";
    }
    std::cout << test::failures() << " failure(s)\n";
    return test::failures() == 0 ? 0 : 1;
}
