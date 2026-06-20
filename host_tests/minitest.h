// Tiny assert-based test framework for host builds. No dependencies.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

struct TestCase {
  const char *name;
  void (*fn)();
};

inline std::vector<TestCase> &all_tests() {
  static std::vector<TestCase> tests;
  return tests;
}

inline int &failure_count() {
  static int failures = 0;
  return failures;
}

struct TestRegistrar {
  TestRegistrar(const char *name, void (*fn)()) { all_tests().push_back({name, fn}); }
};

#define TEST(name)                                  \
  static void test_fn_##name();                     \
  static TestRegistrar reg_##name(#name, test_fn_##name); \
  static void test_fn_##name()

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
      failure_count()++;                                                  \
    }                                                                     \
  } while (0)

inline std::string to_hex(const std::vector<uint8_t> &data) {
  std::string out;
  char buf[3];
  for (uint8_t byte : data) {
    std::snprintf(buf, sizeof(buf), "%02x", byte);
    out += buf;
  }
  return out;
}

#define CHECK_HEX(actual, expected_hex)                                                   \
  do {                                                                                    \
    const std::string actual_hex = to_hex(actual);                                        \
    if (actual_hex != (expected_hex)) {                                                   \
      std::printf("  FAIL %s:%d:\n    actual   %s\n    expected %s\n", __FILE__, __LINE__, \
                  actual_hex.c_str(), (expected_hex));                                    \
      failure_count()++;                                                                  \
    }                                                                                     \
  } while (0)

#define CHECK_STR(actual, expected)                                                       \
  do {                                                                                    \
    const std::string actual_str = (actual);                                              \
    const std::string expected_str = (expected);                                          \
    if (actual_str != expected_str) {                                                     \
      std::printf("  FAIL %s:%d:\n    actual   %s\n    expected %s\n", __FILE__, __LINE__, \
                  actual_str.c_str(), expected_str.c_str());                              \
      failure_count()++;                                                                  \
    }                                                                                     \
  } while (0)
