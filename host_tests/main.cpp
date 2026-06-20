#include "minitest.h"

int main() {
  int ran = 0;
  for (const TestCase &test : all_tests()) {
    const int before = failure_count();
    test.fn();
    ran++;
    if (failure_count() != before) {
      std::printf("FAILED %s\n", test.name);
    }
  }
  std::printf("%d tests, %d failures\n", ran, failure_count());
  return failure_count() == 0 ? 0 : 1;
}
