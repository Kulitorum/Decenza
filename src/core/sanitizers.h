#pragma once

// Whether this build is instrumented with a sanitizer, decided in one place.
//
// This is harder than it looks, and getting it wrong is quiet rather than
// loud — the build still works, it just stops reporting what it is doing.
// Three detection routes are needed because no single one covers the field:
//
//   DECENZA_SANITIZERS_ACTIVE   defined by CMakeLists.txt, which knows which
//                               sanitizers it asked for. The only reliable
//                               route for UBSan under GCC (see below).
//   __has_feature(...)          clang. Covers ASan/UBSan/TSan/MSan.
//   __SANITIZE_ADDRESS__ etc.   GCC. Covers ASan and TSan — and NOTHING for
//                               UBSan; GCC defines no macro for it at all.
//
// That last gap is not hypothetical. The nightly UBSan leg builds with GCC, so
// a macro-only guard left the sanitizer hooks in main.cpp uncompiled and the
// startup banner announcing "Sanitizers: none" on a fully instrumented binary
// — the build reporting the opposite of the truth, which is exactly the class
// of silent failure the canaries exist to catch.
//
// The compiler macros are kept alongside the CMake define as a secondary
// signal, so a build that passes -fsanitize by hand without the CMake options
// is still recognised.
//
// Two copies of this logic previously existed (main.cpp and memorymonitor.cpp)
// and had already drifted — one checked TSan and MSan, the other did not.

#if defined(DECENZA_SANITIZERS_ACTIVE)
#  define DECENZA_SANITIZERS_PRESENT 1
#endif

#if defined(__has_feature)
#  if __has_feature(address_sanitizer) || __has_feature(undefined_behavior_sanitizer) \
   || __has_feature(thread_sanitizer) || __has_feature(memory_sanitizer)
#    define DECENZA_SANITIZERS_PRESENT 1
#  endif
#endif

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#  define DECENZA_SANITIZERS_PRESENT 1
#endif
