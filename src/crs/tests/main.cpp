#include "crs_tests.hpp"
#include "unity.h"

// Unity's own translation unit is C, so the hooks it resolves at link time
// need C linkage even though this suite compiles as C++.
extern "C" void setUp() {}
extern "C" void tearDown() {}

auto main() -> int {
  UNITY_BEGIN();
  run_homography_tests();
  run_jprcs_tests();
  run_tmerc_tests();
  return UNITY_END();
}
