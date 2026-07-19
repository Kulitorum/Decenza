// Deliberate undefined behaviour, so the sanitizer can prove it is armed.
//
// A sanitizer that is silently not applied produces exactly the same green
// suite as a codebase with no undefined behaviour in it. The first UBSan run
// here reported zero findings across the whole suite — a good result and an
// indistinguishable one from `-fsanitize=undefined` having been dropped by a
// flag-ordering mistake, a toolchain that quietly ignored it, or someone
// configuring without -DENABLE_UBSAN=ON. Every later clean run has the same
// ambiguity. This program removes it: it commits real undefined behaviour, so
// if the run is instrumented it must abort with a diagnostic, and the test
// that drives it (run_sanitizer_canary.cmake) fails when it does not.
//
// Built and registered only when ENABLE_UBSAN is on, so it never runs in an
// ordinary build. The volatile reads keep the optimiser from constant-folding
// the overflow away at -O2 and leaving nothing to detect.

#include <cstdio>

int main()
{
    volatile int big = 2147483647;  // INT_MAX
    volatile int one = 1;

    // Signed integer overflow — undefined behaviour, and the check group most
    // relevant to this codebase's fixed-point scale and BLE byte arithmetic.
    const int overflowed = big + one;

    // Only reached when the process was NOT instrumented; the exit code and
    // the absence of a diagnostic are what the driver script keys on.
    std::printf("canary: no sanitizer trap (result %d)\n", overflowed);
    return 0;
}
