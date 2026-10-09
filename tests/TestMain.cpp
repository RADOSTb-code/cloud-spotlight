#include "Test.h"

int main() {
  int failedCases = 0;
  for (auto& c : test::Registry()) {
    int before = test::Failures();
    c.fn();
    bool ok = test::Failures() == before;
    if (!ok) ++failedCases;
    std::printf("[%s] %s\n", ok ? " OK " : "FAIL", c.name);
  }
  std::printf("\n%zu tests, %d failed\n", test::Registry().size(), failedCases);
  return failedCases ? 1 : 0;
}
