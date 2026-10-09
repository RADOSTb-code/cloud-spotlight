#pragma once
// Tiny zero-dependency test harness. Tests are compiled on Linux/Windows from portable code only
// (src/core, src/logic). Usage: TEST(Name) { CHECK(cond); CHECK_EQ(a, b); CHECK_NEAR(a, b, eps); }
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "core/Str.h"

namespace test {
struct Case { const char* name; std::function<void()> fn; };
inline std::vector<Case>& Registry() { static std::vector<Case> r; return r; }
inline int& Failures() { static int f = 0; return f; }
struct Reg { Reg(const char* n, std::function<void()> f) { Registry().push_back({n, std::move(f)}); } };
inline std::string Show(const std::wstring& w) { return "\"" + cs::str::WideToUtf8(w) + "\""; }
inline std::string Show(const wchar_t* w) { return Show(std::wstring(w)); }
inline std::string Show(const std::string& s) { return "\"" + s + "\""; }
inline std::string Show(const char* s) { return Show(std::string(s)); }
inline std::string Show(bool b) { return b ? "true" : "false"; }
template <class T> std::string Show(const T& v) { return std::to_string(v); }
}  // namespace test

#define TEST_CAT2(a, b) a##b
#define TEST_CAT(a, b) TEST_CAT2(a, b)
#define TEST(name)                                                   \
  static void name();                                                \
  static ::test::Reg TEST_CAT(reg_, name)(#name, &name);             \
  static void name()
#define CHECK(c)                                                                        \
  do { if (!(c)) { ++::test::Failures(); std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_EQ(a, b)                                                                                    \
  do {                                                                                                    \
    auto&& _a = (a); auto&& _b = (b);                                                                     \
    if (!(_a == _b)) {                                                                                    \
      ++::test::Failures();                                                                               \
      std::printf("  FAIL %s:%d: %s == %s\n    got %s vs %s\n", __FILE__, __LINE__, #a, #b,             \
                  ::test::Show(_a).c_str(), ::test::Show(_b).c_str());                                    \
    }                                                                                                     \
  } while (0)
#define CHECK_NEAR(a, b, eps)                                                                             \
  do {                                                                                                    \
    double _a = (a), _b = (b);                                                                            \
    if (!(std::fabs(_a - _b) <= (eps))) {                                                                 \
      ++::test::Failures();                                                                               \
      std::printf("  FAIL %s:%d: %s ~= %s (got %.10g vs %.10g)\n", __FILE__, __LINE__, #a, #b, _a, _b);   \
    }                                                                                                     \
  } while (0)
