#pragma once

// Registration contract for the CRS math suite, mirroring the raw C API
// suite's contract (src/c_api/tests/abi_tests.h).
//
// Every test is a `static void <name>()` run through exactly one
// `RUN_TEST(<name>)` in its file's entry point below; `-Werror=unused-function`
// then fails the build on a test nothing runs. Each entry point lives in the
// matching `<name>_tests.cpp`, starts with `UnitySetTestFile(__FILE__)`, and
// must be declared here and called from `main.cpp`.

void run_extent_tests();
void run_homography_tests();
void run_jprcs_tests();
void run_tmerc_tests();
