// Anchor TU for decenza_test_pch. Its only job is to give the PCH donor target
// a source file to exist around — every real test target reuses the precompiled
// header this target builds, via REUSE_FROM in add_decenza_test().
//
// The header list lives in tests/CMakeLists.txt next to the target, not here.
// This file should stay empty; anything added to it is compiled into nothing
// and linked by nobody.
namespace {
// A TU with no external linkage at all draws -Wempty-translation-unit on some
// toolchains, and this tree builds with -Werror.
[[maybe_unused]] constexpr int kDecenzaTestPchAnchor = 0;
}  // namespace
