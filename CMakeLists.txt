cmake_minimum_required(VERSION 3.21)
project(Decenza VERSION 2.0.5 LANGUAGES CXX)

message(STATUS "Developer note: executable target is now 'Decenza'. If Qt Creator shows 'No executable specified', re-run CMake and select the 'Decenza' Run configuration.")

set(CMAKE_CXX_STANDARD 17)

# Version code - incremented by CI on tag push (separate from display version)
set(VERSION_CODE_FILE "${CMAKE_SOURCE_DIR}/versioncode.txt")
set(VERSION_HEADER "${CMAKE_BINARY_DIR}/version.h")
set(VERSION_CODE_CPP "${CMAKE_BINARY_DIR}/version_code.cpp")

# Read version code at configure time (CI bumps versioncode.txt before configure)
file(READ "${VERSION_CODE_FILE}" CURRENT_VERSION_CODE)
string(STRIP "${CURRENT_VERSION_CODE}" CURRENT_VERSION_CODE)

# Configure version.h from template (stable values: VERSION_STRING, VERSION_MAJOR, etc.)
configure_file(
    "${CMAKE_SOURCE_DIR}/src/version.h.in"
    "${VERSION_HEADER}"
    @ONLY
)

# Generate initial version_code.cpp (updated every build by ApplyVersionCode.cmake)
configure_file(
    "${CMAKE_SOURCE_DIR}/src/version_code.cpp.in"
    "${VERSION_CODE_CPP}"
    @ONLY
)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Unit tests — auto-enabled in Debug builds (single-config generators only), opt-in otherwise.
# Multi-config generators (Visual Studio, Xcode) leave CMAKE_BUILD_TYPE empty at configure
# time, so tests default to OFF there — pass -DBUILD_TESTS=ON explicitly.
# Override: cmake -DBUILD_TESTS=ON/OFF ...
# Run with: ctest --output-on-failure
if(NOT DEFINED BUILD_TESTS)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(BUILD_TESTS ON CACHE BOOL "Build unit tests")
    else()
        set(BUILD_TESTS OFF CACHE BOOL "Build unit tests")
    endif()
endif()

# Precompiled headers. Measured on five back-to-back clean rebuilds (this
# machine, Ninja, Debug+ASan): 229.5 s with PCH off, 157.0 s with it on, i.e.
# 31.6%. Honest interval 27-35% — the on-arm is one run and the two off-arms
# spread 7%. Header lists and the per-target detail are at the
# target_precompile_headers() calls in this file and in tests/CMakeLists.txt.
#
# Do NOT compare that against numbers from a different sitting: the same
# configuration measured 169.5 s earlier in the day and 194.6 s later, on the
# same machine. Drift here exceeds the effect, so only same-window arms compare.
#
# OFF on iOS. Its release build fails outright with the PCH enabled:
#
#   error: input is not a PCH file: '…/SharedPrecompiledHeaders/…/cmake_pch.hxx.gch'
#   fatal error: … is not a valid PCH file: file too small to contain
#                precompiled file magic
#
# A COMPILER CACHE IS NOT THE CAUSE, though it looks like one and was chased
# twice. Two things disprove it. First, the rest of the matrix: Android and
# Linux-arm64 build with ccache under Ninja and pass; macOS builds with ccache
# under Xcode, generates objective-c++ PCHs of its own, and passes. Second, a
# wrapper change that routed PCH generation around ccache entirely was tested on
# this leg and it failed identically — see PR #1730 if the reasoning is needed
# again. What is specific to iOS is the build graph: Xcode compiles every source
# there as objective-c++, so ONE cmake_pch.hxx gates ~900 compiles, and the log
# shows it being precompiled twice concurrently into two different
# SharedPrecompiledHeaders hash directories. That is where to start if anyone
# picks this up; it is not a caching problem.
#
# OFF on Windows, untested rather than known-bad. Windows CI wraps the compiler
# in sccache — a different program from ccache with its own PCH handling — and
# the Windows leg of the test run built with this already OFF, so nothing has
# ever exercised the combination. Turning it on is a separate change that must
# be measured on a Windows runner with a warm sccache.
#
# OFF in CI on the four legs that carry a warm ccache (android, macos, linux,
# linux-arm64), passed as -DDECENZA_ENABLE_PCH=OFF by those workflows rather
# than decided here. A PCH and a compiler cache are SUBSTITUTES: ccache will not
# cache a compile that consumes a PCH, since the preprocessed output no longer
# contains the PCH'd headers' text. Enabling the PCH cost those legs ~90% of
# their cache hits and roughly doubled the macOS build; numbers in PR #1738.
#
# ON everywhere else, which includes every local build: Qt Creator and a plain
# CLI configure set no compiler launcher, so there is no cache to lose, and
# local development is the case this exists for.
#
# To force either way: -DDECENZA_ENABLE_PCH=ON/OFF.
if(NOT DEFINED DECENZA_ENABLE_PCH)
    if(WIN32 OR IOS)
        set(DECENZA_ENABLE_PCH OFF CACHE BOOL "Use precompiled headers")
    else()
        set(DECENZA_ENABLE_PCH ON CACHE BOOL "Use precompiled headers")
    endif()
endif()
message(STATUS "Precompiled headers: ${DECENZA_ENABLE_PCH}")

# Address Sanitizer — catches use-after-free, heap overflow, etc.
# Enable in Qt Creator: Projects → Build → CMake → add ENABLE_ASAN=ON
# Or configure from CLI: cmake ... -DENABLE_ASAN=ON
# Applied to the Decenza target only (after qt_add_executable) via the existing
# ASan block further down in this file, which handles MSVC vs GCC/Clang flags.
option(ENABLE_ASAN "Enable Address Sanitizer" OFF)

# Undefined Behavior Sanitizer — catches signed overflow, invalid shifts,
# misaligned loads, out-of-range enum/bool values, etc.
# Enable in Qt Creator: Projects → Build → CMake → add ENABLE_UBSAN=ON
# Or configure from CLI: cmake ... -DENABLE_UBSAN=ON
# nightly-sanitizers.yml builds with this ON (halting mode). Debug builds
# auto-enable it in RECOVERING mode; see the block further down. Not supported
# on MSVC (no UBSan there).
option(ENABLE_UBSAN "Enable Undefined Behavior Sanitizer" OFF)

# Optional features (Quick3D not available on all platforms, e.g. Raspberry Pi)
option(ENABLE_QUICK3D "Enable Qt Quick3D for 3D screensavers" ON)

# Embedded Tailscale (tsnet + Funnel) for the Remote MCP connector Mode A.
# Pulls prebuilt libtailscale artifacts from the fork (no Go toolchain needed).
# Default ON for the platforms with a prebuilt artifact (macOS, iOS, Android) so
# local dev builds match what ships (CI passes -DENABLE_TSNET=ON for those). OFF
# on Windows/Linux — tsnet.cmake has no artifact there and Mode C (BYO URL)
# always works regardless. The download happens at configure time; turn this off
# explicitly for an offline/minimal Apple or Android build.
if(APPLE OR ANDROID)
    set(_tsnet_default ON)
else()
    set(_tsnet_default OFF)
endif()
option(ENABLE_TSNET "Embed Tailscale (tsnet) for the remote MCP connector" ${_tsnet_default})

# Qt 6 modules - core required components
find_package(Qt6 REQUIRED COMPONENTS
    Core
    CorePrivate
    Gui
    GuiPrivate
    Widgets
    Qml
    Quick
    QuickControls2
    Bluetooth
    Graphs
    Svg
    Multimedia
    TextToSpeech
    Network
    Sql
    Positioning
    WebSockets
    WebSocketsPrivate
    CanvasPainter
)

# SerialPort is desktop-only (Android/iOS use JNI/native USB APIs instead)
if(NOT ANDROID AND NOT IOS)
    find_package(Qt6 REQUIRED COMPONENTS SerialPort)
endif()

# Optional: ShaderTools (for GPU CRT/Pip-Boy shader effect)
find_package(Qt6 QUIET COMPONENTS ShaderTools)
if(Qt6ShaderTools_FOUND)
    message(STATUS "Qt6ShaderTools found - GPU shader effects enabled")
else()
    message(STATUS "Qt6ShaderTools not found - GPU shader effects disabled (Canvas fallback)")
endif()

# Optional: Quick3D (only used for screensavers)
if(ENABLE_QUICK3D)
    find_package(Qt6 QUIET COMPONENTS Quick3D)
    if(Qt6Quick3D_FOUND)
        message(STATUS "Qt6Quick3D found - 3D screensavers enabled")
        add_compile_definitions(HAVE_QUICK3D)
    else()
        message(STATUS "Qt6Quick3D not found - 3D screensavers disabled")
        set(ENABLE_QUICK3D OFF)
    endif()
endif()

# Silence Qt policy warnings (qt_policy introduced in Qt 6.5)
if(Qt6_VERSION VERSION_GREATER_EQUAL "6.5.0")
    qt_policy(SET QTP0001 NEW)
    qt_policy(SET QTP0004 NEW)
endif()

# Enable QML debugging in debug builds
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(QT_QML_DEBUG)
endif()

# Platform-specific settings
if(ANDROID)
    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    set(ANDROID_MIN_SDK_VERSION 28)
    set(ANDROID_TARGET_SDK_VERSION 34)

    # Fetch OpenSSL for Android (required for HTTPS)
    # See: https://github.com/KDAB/android_openssl
    include(FetchContent)
    FetchContent_Declare(
        android_openssl
        DOWNLOAD_EXTRACT_TIMESTAMP true
        URL https://github.com/KDAB/android_openssl/archive/refs/heads/master.zip
    )
    FetchContent_MakeAvailable(android_openssl)
    include(${android_openssl_SOURCE_DIR}/android_openssl.cmake)
endif()

if(IOS)
    # Qt 6.11.2 was built for iOS 17.0
    set(CMAKE_OSX_DEPLOYMENT_TARGET "17.0")
    # Fix Qt/Foundation header conflict by compiling all C++ as Objective-C++
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -x objective-c++")

endif()

# OpenSSL for HTTPS/TLS and self-signed certificate generation
# Android uses KDAB android_openssl (fetched above)
# iOS uses Apple Security.framework instead (no OpenSSL needed)
if(NOT ANDROID AND NOT IOS)
    find_package(OpenSSL REQUIRED)
endif()

# Fetch Eclipse Paho MQTT C library (industry-standard, works with FetchContent)
# OpenSSL must be found BEFORE this block so Paho can link against it.
# PAHO_WITH_SSL ON makes Paho use OpenSSL's SHA1 instead of bundling its own,
# which avoids duplicate SHA1_Init/Final/Update symbols on macOS when both
# Paho and OpenSSL are linked as static libraries.
include(FetchContent)
FetchContent_Declare(
    paho_mqtt_c
    GIT_REPOSITORY https://github.com/eclipse/paho.mqtt.c.git
    GIT_TAG        v1.3.16
)
set(PAHO_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(PAHO_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(PAHO_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
if(NOT ANDROID AND NOT IOS)
    # Desktop: use OpenSSL's SHA1 to avoid duplicate symbols with libcrypto.a
    set(PAHO_WITH_SSL ON CACHE BOOL "" FORCE)
else()
    # Android/iOS: no OpenSSL linked with Paho (Android uses KDAB separately,
    # iOS uses Apple Security.framework for TLS cert generation)
    set(PAHO_WITH_SSL OFF CACHE BOOL "" FORCE)
endif()
set(PAHO_HIGH_PERFORMANCE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(paho_mqtt_c)

# mjansson/mdns: header-only mDNS library. Two jobs:
#  - resolving .local hostnames on Android (Android's getaddrinfo doesn't
#    reliably handle mDNS), which is what it was originally pulled in for;
#  - the _decentscale._tcp DNS-SD service browse, which QHostInfo cannot do at
#    all (it resolves names, it does not enumerate services).
#
# Everywhere EXCEPT iOS.
#
# iOS is excluded because a raw multicast socket to 224.0.0.251 there requires
# the com.apple.developer.networking.multicast entitlement, which Apple grants
# only by per-app application; iOS browses through mDNSResponder instead and
# must not even carry this code.
#
# macOS DOES build it, despite shipping the Bonjour backend by default. macOS is
# the only desktop that can be tested against real scales here, and without this
# the mjansson browse — the backend Android and Windows/Linux actually use —
# could not be exercised anywhere except by deploying to a tablet. The backend
# is selectable at runtime (MdnsResolver::setBrowseBackend), so a macOS build
# can A/B the two against the same LAN and confirm they agree.
if(NOT IOS)
    FetchContent_Declare(
        mdns_lib
        GIT_REPOSITORY https://github.com/mjansson/mdns.git
        GIT_TAG        1.4.3
    )
    FetchContent_Populate(mdns_lib)
endif()

# Address Sanitizer - auto-enabled for single-config Debug builds (Makefiles, Ninja),
# or manually via -DENABLE_ASAN=ON. Multi-config generators (Visual Studio, Xcode)
# don't set CMAKE_BUILD_TYPE at configure time, so auto-enable is skipped to avoid
# leaking flags into Release builds. Android requires special ASan packaging
# (wrap.sh + shared lib in APK) that isn't handled here.
#
# APPLE was excluded here for a long time because the instrumented app would not
# START — the test suite was fine, so the fault was in launching the bundle, not
# in the instrumentation. macOS is back in, because a Debug build is exactly
# where memory errors should surface and excluding the development platform made
# ASan dead configuration for the people most able to act on it.
#
# If the app fails to launch under ASan, the two causes worth checking first:
#
#  1. dyld cannot find the runtime. clang links it as
#     @rpath/libclang_rt.asan_osx_dynamic.dylib and adds an rpath pointing INTO
#     the Xcode toolchain (…/clang/<major>/lib/darwin). That path contains an
#     Xcode version, so a major Xcode upgrade can strand every previously-linked
#     binary. Symptom is a dyld error naming the dylib, before any app code runs.
#     `otool -l <binary> | grep -A2 LC_RPATH` shows what it is actually looking for.
#  2. ASan found a real defect during startup and aborted. This looks like "the
#     app won't start" but is the tool working: the console carries an
#     "ERROR: AddressSanitizer: …" block naming the location. That is a bug to
#     fix, not a reason to turn the sanitizer off.
if(ENABLE_ASAN OR (NOT CMAKE_CONFIGURATION_TYPES AND CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT ANDROID))
    # Reflect the auto-enable back into the option, as the UBSan block does.
    # Without this, ASan is ACTIVE in every local Debug build while ENABLE_ASAN
    # stays OFF — and tests/CMakeLists.txt registers asan_canary under
    # `if(ENABLE_ASAN)`, so the canary silently did not exist in exactly the
    # configuration developers run all day. ASan instrumented, nothing proving
    # it: the precise hole the canary was written to close, reintroduced by
    # copying the UBSan block's shape without noticing UBSan sets its flag here
    # and ASan did not.
    #
    # Caught by a test count of 84 that contained sanitizer_canary and no
    # asan_canary.
    set(ENABLE_ASAN ON)
    if(MSVC)
        message(STATUS ">>> ASan ENABLED for debug build (MSVC) <<<")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /fsanitize=address")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /fsanitize=address")
    else()
        message(STATUS ">>> ASan ENABLED for debug build <<<")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=address")

        # GCC + ASan only: -Wmaybe-uninitialized becomes unreliable and must
        # not be an error in this one configuration.
        #
        # Found by the first nightly ASan run, which failed to BUILD while the
        # UBSan leg — same compiler, same flags, same commit — succeeded. The
        # diagnostic fires on `s.lastShot = WidgetLastShot{...}` where
        # `lastShot` is a default-constructed (disengaged) std::optional:
        #
        #   error: '((char16_t**)((char*)&s + offsetof(WidgetSnapshot,
        #   ...std::_Optional_payload_base<WidgetLastShot>::_M_payload)))[3]'
        #   may be used uninitialized [-Werror=maybe-uninitialized]
        #
        # A disengaged optional's payload bytes ARE uninitialized — that is the
        # design — and optional's assignment operator constructs into them
        # without reading them. The claim is false. What makes it appear is
        # ASan's effect on inlining: GCC's dataflow analysis loses the
        # engaged-ness guard across the changed inline boundary. Every member of
        # WidgetLastShot and WidgetSnapshot carries an initializer; there is
        # nothing to fix in the code.
        #
        # Scoped to exactly the configuration where the analysis is wrong. The
        # plain Linux builds keep -Wmaybe-uninitialized as an error, and they
        # are the ones that run constantly (37-48 times per fortnight per
        # platform), so real instances are still caught. Do NOT widen this to
        # all GCC builds — the warning does find genuine bugs uninstrumented.
        #
        # Recorded as a variable rather than applied here: the flag has to land
        # AFTER the bare -Werror in the warning-policy block below, because GCC
        # resolves these left-to-right and a later -Werror re-promotes what an
        # earlier -Wno-error= exempted. See where this is consumed.
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            set(_gcc_asan_maybe_uninit_exempt TRUE)
        endif()
    endif()
endif()

# Undefined Behavior Sanitizer. Auto-enabled for Debug builds (see the block
# below) and available anywhere via -DENABLE_UBSAN=ON; a Release build carries
# no instrumentation unless asked. The nightly job turns it on explicitly to run
# the suite in HALTING mode — see the recover/halt split further down.
#
# -fno-sanitize-recover=all is what makes a finding FAIL rather than warn.
# UBSan's default is report-and-continue: a run can print undefined behaviour
# and still exit 0 (verified — a deliberate overflow exits 0 by default and
# 134 with this on). UBSAN_OPTIONS=halt_on_error=1 does the same job, but it
# lives in the CI workflow where it can be dropped by an edit that looks
# unrelated, and it is absent from a developer's local run entirely. Baking it
# into the build makes "instrumented" and "fails on a finding" the same switch.
#
# Beyond the default `undefined` group:
#   local-bounds        - array index out of bounds on locals; real UB that
#                         plain -fsanitize=undefined does not include.
#   float-divide-by-zero- defined as inf by IEEE 754, but in flow/pressure/ratio
#                         arithmetic it is a defect every time.
# Deliberately NOT enabled: the `integer` group (unsigned-integer-overflow,
# implicit conversions). That is legal, well-defined C++ that CRC and hashing
# code wraps on purpose, so it reports intent as if it were a defect. A
# detector that cries wolf gets switched off, which costs more than it finds.
# UBSan auto-enables for single-config Debug builds, on Apple too. This is the
# main place undefined behaviour actually gets caught, because the full test
# suite is run locally before every pull request and there is no CI gate — so
# instrumenting the Debug build instruments the run that already happens.
#
# The ASan condition above used to exclude APPLE, so a macOS Debug build once
# carried no sanitizer at all. This branch removed that exclusion (see the
# comment on the ASan block), so macOS Debug now gets BOTH. UBSan is verified
# working there — the full suite passes instrumented.
#
# Android is excluded for the same reason ASan excludes it: the runtime needs
# packaging into the APK (wrap.sh + shared lib) that this file does not do.
#
# An auto-enabled Debug build gets RECOVERING UBSan — it prints a diagnostic
# and keeps going. An explicit -DENABLE_UBSAN=ON gets the halting kind. The
# distinction matters: a developer running the app should be told about
# undefined behaviour, not have the app killed under them mid-session, whereas
# a test run wants a hard failure it cannot ignore.
if(NOT ENABLE_UBSAN AND NOT CMAKE_CONFIGURATION_TYPES
   AND CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT ANDROID AND NOT MSVC)
    set(ENABLE_UBSAN ON)
    set(_ubsan_recover ON)
    message(STATUS ">>> UBSan auto-enabled for Debug build (reports, does not abort) <<<")
endif()

if(ENABLE_UBSAN)
    if(MSVC)
        message(FATAL_ERROR "ENABLE_UBSAN is not supported with MSVC (it has no UBSan). Use clang or GCC.")
    else()
        # -fno-sanitize=vptr is required, not preference. The vptr check
        # instruments downcasts through a polymorphic hierarchy and needs the
        # target type's typeinfo at link time. decentscalewifi.cpp downcasts
        # QObjectPrivate* to QWebSocketPrivate* (to reach the inner QTcpSocket
        # and set DSCP/TCP_NODELAY), and Qt does not export typeinfo for its
        # private classes on Linux, so the link fails with "undefined reference
        # to `typeinfo for QWebSocketPrivate'".
        #
        # Found by this very gate: it links on macOS, where Apple's toolchain
        # emits that typeinfo, and fails on Linux/GCC — the same shape as the
        # iOS break that motivated the six-platform rule, caught this time
        # before merge rather than at a release tag.
        #
        # What this costs, stated rather than buried: bad-downcast detection is
        # off everywhere, including for that Qt-private downcast, whose validity
        # rests on a convention the code's own comment says to re-verify on every
        # Qt upgrade. That check is exactly what vptr would have enforced.
        # The optional checks are PROBED, not assumed. `local-bounds` is
        # clang-only — GCC fails the whole compile with "unrecognized argument
        # to '-fsanitize=' option", which is how this was found: verified on
        # Apple clang, broken on Linux/GCC, exactly the mistake the
        # six-platform rule exists to prevent. Probing rather than branching on
        # compiler id keeps that true for the platforms not built here (iOS,
        # Android, Windows) without anyone having to predict their toolchains.
        set(_ubsan_extra "")
        include(CheckCXXCompilerFlag)
        foreach(_check local-bounds float-divide-by-zero)
            set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=undefined")
            check_cxx_compiler_flag("-fsanitize=${_check}" _ubsan_has_${_check})
            unset(CMAKE_REQUIRED_LINK_OPTIONS)
            if(_ubsan_has_${_check})
                string(APPEND _ubsan_extra ",${_check}")
            else()
                message(STATUS "UBSan: ${_check} unsupported by this compiler, skipping")
            endif()
        endforeach()
        # Recovering in an auto-enabled Debug build, halting when asked for
        # explicitly — see the auto-enable block above for why.
        if(_ubsan_recover)
            set(_ubsan_halt "")
            set(_ubsan_mode "reports")
        else()
            set(_ubsan_halt " -fno-sanitize-recover=all")
            set(_ubsan_mode "aborts on finding")
        endif()
        message(STATUS ">>> UBSan ENABLED (undefined${_ubsan_extra}, no vptr, ${_ubsan_mode}) <<<")
        set(_ubsan_checks "-fsanitize=undefined${_ubsan_extra} -fno-sanitize=vptr${_ubsan_halt} -fno-omit-frame-pointer")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${_ubsan_checks}")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${_ubsan_checks}")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=undefined")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=undefined")
    endif()
endif()

# Hardened standard library + live Qt assertions, tied to whichever sanitizer
# build is running. These catch a defect class BOTH sanitizers miss: an
# out-of-bounds std::vector/QVector operator[] that lands inside the
# allocation. ASan sees nothing (the memory is validly owned), UBSan sees
# nothing (no UB at the language level), and the read silently returns garbage
# that propagates into a shot. Verified: `v[argc+10]` on a 3-element vector
# prints garbage and exits 0 unhardened, and traps with this on.
#
# QT_FORCE_ASSERTS keeps Q_ASSERT live in a Release-configured sanitizer build.
# Without it every Q_ASSERT in Qt's own code and in ours compiles to nothing,
# so the invariants they document go unchecked in exactly the run built to
# check invariants.
if(ENABLE_ASAN OR ENABLE_UBSAN)
    # Tell the C++ side which sanitizers are actually on. This has to come from
    # CMake, not from compiler macros: clang exposes __has_feature(...), and GCC
    # defines __SANITIZE_ADDRESS__ for ASan — but GCC defines NOTHING for UBSan.
    # The nightly job's ubsan leg is GCC on ubuntu-24.04, so a C++-side guess
    # left the report hooks uncompiled and the startup banner announcing
    # "Sanitizers: none" on an instrumented binary — the precise failure the
    # banner exists to rule out.
    if(ENABLE_ASAN)
        add_compile_definitions(DECENZA_ASAN_ACTIVE)
    endif()
    if(ENABLE_UBSAN)
        add_compile_definitions(DECENZA_UBSAN_ACTIVE)
    endif()
    add_compile_definitions(DECENZA_SANITIZERS_ACTIVE)
    add_compile_definitions(QT_FORCE_ASSERTS)
    if(NOT MSVC)
        # libc++ (Apple/clang) and libstdc++ (GCC) spell this differently and
        # ignore each other's macro, so both are set rather than branched on
        # the compiler — the wrong one is inert, not an error.
        add_compile_definitions(
            _LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE
            _GLIBCXX_ASSERTIONS
        )
    endif()
endif()

# A discarded [[nodiscard]] verdict is a build ERROR, not a warning. Seven bugs on one branch
# had the identical shape — a bool-returning save whose refusal a caller silently dropped, then
# reported success over — and the last two were written while fixing the previous ones, so a
# convention demonstrably did not hold. Scoped to exactly this diagnostic (not a general
# -Werror), and placed AFTER the third-party FetchContent blocks so Paho does not inherit it;
# it applies to the app target and tests, which are created below. A deliberate discard is
# written `(void)call();` with a comment saying why the failure is tolerable.
if(MSVC)
    add_compile_options(/we4834)
else()
    add_compile_options(-Werror=unused-result)
endif()

# Warnings are errors. This is where defects are cheapest to catch — the
# developer's own build, before anything is committed — and it is deliberately
# the ONLY place diagnostics are enforced. There is no CI warning job: a count
# in a log has no forcing function, and a red build does.
#
# Placed after the FetchContent blocks so third-party code (Paho, mdns, tsnet)
# never inherits it; it applies to the app target and tests, created below.
#
# The extra flags beyond -Wall -Wextra were chosen by MEASUREMENT, not taste:
# every first-party TU was compiled with a wide candidate set and the hits
# counted (scripts/measure_warnings.py). Flags that fired on real defects are
# enabled below; flags that fired mostly on idiom are not, and are listed after
# them with their counts, so nobody re-proposes them without new evidence.
if(MSVC)
    # NOT verified. /W4 is a different diagnostic set from -Wall -Wextra with no
    # measurement behind it here, and turning /WX on unmeasured is the same
    # mistake as the one recorded below. Left as a plain warning level until
    # someone builds Windows and counts what it produces.
    add_compile_options(/W4)
else()
    # -Wall -Wextra -Werror are universally supported; everything else is
    # PROBED. This is the second time this lesson was learned in one sitting:
    # `-fsanitize=local-bounds` was hardcoded, GCC rejected it, the probe below
    # was written to fix it — and then these warning flags were hardcoded too,
    # and GCC rejected -Wshorten-64-to-32, -Wconditional-uninitialized,
    # -Wcomma and -Wself-assign in exactly the same way. A flag that exists in
    # clang is not a flag that exists.
    add_compile_options(-Wall -Wextra -Werror)

    include(CheckCXXCompilerFlag)
    # What the measurement said about the flags that ARE enabled:
    #
    #   -Wshorten-64-to-32   30 sites. CLAUDE.md already documents this hazard
    #                        (qsizetype -> int truncates on 64-bit) and the
    #                        convention did not hold on its own.
    #   -Wshadow              8 sites, two of which hid genuine confusion
    #                        between an inner and outer variable of different
    #                        types.
    #   the rest             scored ZERO in the sweep: free today, and a
    #                        first occurrence now fails the build rather than
    #                        drifting in unnoticed.
    #
    # Those two counts are from BEFORE the sites were fixed — fixing them is
    # what let the flags be enabled. Re-running the sweep today reports 0 for
    # both, which is the point, not a contradiction. The counts below, for the
    # flags that were NOT enabled, do still reproduce as written.
    foreach(_w
            shorten-64-to-32
            shadow
            non-virtual-dtor
            conditional-uninitialized
            implicit-fallthrough
            self-assign
            pessimizing-move
            redundant-move
            comma
            suggest-override
            extra-semi
            format=2
            cast-align)
        string(MAKE_C_IDENTIFIER "${_w}" _w_id)
        check_cxx_compiler_flag("-W${_w}" _have_w_${_w_id})
        if(_have_w_${_w_id})
            add_compile_options("-W${_w}")
        endif()
    endforeach()

    # Must be the LAST warning option added: GCC resolves -Werror and
    # -Wno-error=<name> left-to-right, so a bare -Werror appearing after this
    # would re-promote the very diagnostic being exempted. Set by the ASan block
    # above, and only for GCC+ASan — see the long note there for why the
    # diagnostic is wrong in that one configuration and correct everywhere else.
    #
    # -Wno-error=, not -Wno-: the warning is still printed. It is demoted from
    # fatal, not hidden, so a real instance is still visible in the log.
    if(_gcc_asan_maybe_uninit_exempt)
        message(STATUS "GCC+ASan: -Wmaybe-uninitialized demoted to a warning "
                       "(known std::optional false positive under instrumentation)")
        add_compile_options(-Wno-error=maybe-uninitialized)
    endif()

    # MEASURED AND DELIBERATELY NOT ENABLED. Re-proposing any of these needs
    # evidence that the instances are defects, not that the flag exists:
    #
    #   -Wrange-loop-bind-reference  147 hits / 50 files, and all 147 name a
    #       QJsonArray range — `for (const QJsonValue& v : array)`, where Qt 6
    #       returns a proxy from the iterator so the reference binds to a
    #       temporary. A per-iteration copy, not a bug; 147 edits would buy no
    #       correctness. (Verified, not assumed: every line of the sweep output
    #       for this class matches "range of type ... QJsonArray".)
    #
    #       Do NOT read this as "range-loop warnings find nothing here."
    #       GCC's sibling -Wrange-loop-construct — which is in -Wextra on GCC,
    #       so it is already enabled and already enforced — found five REAL
    #       sites on this branch (7137c9cf): `for (const QString& key : {"a",
    #       "b", ...})` built a throwaway QString per element per call. Those
    #       were worth fixing. The two flags share a name and nothing else.
    #   -Wfloat-equal                 74 hits / 28 files. Sampled sites are
    #       `a != 0` sentinel checks ("was this ever set?"), a legitimate idiom
    #       here. Real float-comparison bugs would be lost in the noise.
    #   -Wswitch-enum                 46 hits / 31 files. Tempting, because
    #       "new machine phase added, switch silently falls to default:" is a
    #       real class here — but it fires on every switch with a deliberate
    #       `default:`, which is most of them. -Wswitch (in -Wall) already
    #       covers the dangerous case: a switch with NO default that misses an
    #       enumerator.
    #   -Wdouble-promotion            14 hits / 3 files. float->double in
    #       arithmetic that is double anyway. Harmless.
    #   -Wshadow-all                  clang-only superset of -Wshadow. Fires on
    #       `post([..., work = std::move(work), done = std::move(done)] ...)` in
    #       dbutils.h — the canonical C++14 move-into-lambda idiom, where
    #       reusing the name is the entire point. Reporting intent as a defect,
    #       same reason -Wfloat-equal is out.
    #
    #       Worth recording HOW this was nearly added by mistake: a sweep script
    #       reported zero hits, and the real build immediately
    #       produced three. The script counted lines matching "warning:" and had
    #       no way to tell "compiled clean" from "failed to compile" — so a
    #       broken run and a clean one looked identical. That is the same
    #       silent-failure shape the sanitizer canary exists to prevent, in the
    #       measurement tooling itself. Trust a real build over a sweep.
    #
    #       That hole is now closed: measure_warnings.py probes each flag for
    #       support before sweeping, counts TUs that failed to compile, and
    #       exits non-zero when any did. Reproduce the counts above with:
    #
    #         ninja -C <build-dir> -t compdb > /tmp/cdb.json
    #         python3 scripts/measure_warnings.py /tmp/cdb.json 10 /tmp/w.txt \
    #             -Wrange-loop-bind-reference -Wfloat-equal -Wswitch-enum \
    #             -Wdouble-promotion
endif()

# Enable parallel builds
if(NOT DEFINED CMAKE_BUILD_PARALLEL_LEVEL)
    include(ProcessorCount)
    ProcessorCount(N)
    if(N GREATER 0)
        set(CMAKE_BUILD_PARALLEL_LEVEL ${N})
    endif()
endif()

# Source files
set(SOURCES
    src/main.cpp
    src/core/appsettings.cpp
    src/core/settingsstoremigration.cpp
    src/core/settings.cpp
    src/core/settings_mqtt.cpp
    src/core/settings_autowake.cpp
    src/core/settings_hardware.cpp
    src/core/settings_ai.cpp
    src/core/emojiassets.cpp
    src/core/markdownrenderer.cpp
    src/core/settings_theme.cpp
    src/core/backgroundpresets.cpp
    src/core/settings_visualizer.cpp
    src/core/settings_mcp.cpp
    src/core/settings_brew.cpp
    src/core/settings_dye.cpp
    src/core/settings_network.cpp
    src/core/settings_app.cpp
    src/core/settings_calibration.cpp
    src/core/settings_graph.cpp
    src/core/widgetlibrary.cpp
    src/widget/machinestatussnapshot.cpp
    src/core/batterymanager.cpp
    src/core/memorymonitor.cpp
    src/core/fddiagnostics.cpp
    src/core/accessibilitymanager.cpp
    src/core/autowakemanager.cpp
    src/core/crashhandler.cpp
    src/core/profilestorage.cpp
    src/core/translationmanager.cpp
    src/core/updatechecker.cpp
    src/core/githubreleaseclient.cpp
    src/core/hdsfirmwarecatalog.cpp
    src/core/hdsfirmwareupdatecontroller.cpp
    src/core/firmwareassetcache.cpp
    src/core/fileshare.cpp
    src/ble/protocol/binarycodec.cpp
    src/ble/blecapability.cpp
    src/ble/blegattqueue.cpp
    src/ble/blemanager.cpp
    src/ble/de1device.cpp
    src/ble/bletransport.cpp
    src/ble/scaledevice.cpp
    src/ble/scaledeviceproxy.cpp
    src/ble/scales/scalefactory.cpp
    src/ble/scales/scaletypeids.cpp
    src/ble/scales/decentscale.cpp
    src/ble/scales/decentscalewifi.cpp
    src/ble/scales/acaiascale.cpp
    src/ble/scales/felicitascale.cpp
    src/ble/scales/skalescale.cpp
    src/ble/scales/hiroiascale.cpp
    src/ble/scales/bookooscale.cpp
    src/ble/scales/smartchefscale.cpp
    src/ble/scales/difluidscale.cpp
    src/ble/refractometers/refractometerdevice.cpp
    src/ble/refractometers/refractometerproxy.cpp
    src/ble/refractometers/difluidr2.cpp
    src/ble/refractometers/difluidr1.cpp
    src/ble/refractometers/aes128.cpp
    src/ble/scales/eurekaprecisascale.cpp
    src/ble/scales/solobaristascale.cpp
    src/ble/scales/atomhearteclairscale.cpp
    src/ble/scales/variaakuscale.cpp
    src/ble/scales/timemorescale.cpp
    src/ble/scales/flowscale.cpp
    src/ble/transport/scalebletransport.cpp
    src/ble/transport/qtscalebletransport.cpp
    src/core/asynclogger.cpp
    src/core/btlogfilter.cpp
    src/machine/machinestate.cpp
    src/machine/weightprocessor.cpp
    src/machine/stepexitarbiter.cpp
    src/profile/profile.cpp
    src/profile/de1apptclfields.cpp
    src/profile/profileframe.cpp
    src/profile/profileconverter.cpp
    src/profile/profileimporter.cpp
    src/profile/profilesavehelper.cpp
    src/profile/recipeparams.cpp
    src/profile/recipegenerator.cpp
    src/profile/recipeanalyzer.cpp
    src/profile/temperaturedisplay.cpp
    src/models/shotdatamodel.cpp
    src/models/steamdatamodel.cpp
    src/machine/steamhealthtracker.cpp
    src/controllers/maincontroller.cpp
    src/controllers/autoflowcalclassifier.cpp
    src/controllers/profilemanager.cpp
    src/controllers/steamheaterpolicy.cpp
    src/controllers/shottimingcontroller.cpp
    src/controllers/sensorcalibrationcontroller.cpp
    src/controllers/firmwareupdater.cpp
    src/screensaver/screensavervideomanager.cpp
    src/screensaver/strangeattractorrenderer.cpp
    src/rendering/fastlinerenderer.cpp
    src/ui/jscanvaspainteritem.cpp
    src/ui/jscanvascontext.cpp
    src/network/visualizeruploader.cpp
    src/network/visualizerimporter.cpp
    src/network/beanbaseclient.cpp
    src/ai/aimanager.cpp
    src/ai/aiprovider.cpp
    src/ai/aiconversation.cpp
    src/ai/conductance.cpp
    src/ai/livesteamcoach.cpp
    src/ai/shotanalysis.cpp
    src/ai/shotsummarizer.cpp
    src/ai/shotsummarizer_kb.cpp
    src/ai/profileshapeindex.cpp
    src/history/shothistorystorage.cpp
    src/history/shothistorystorage_internal.cpp
    src/history/shothistorystorage_serialize.cpp
    src/history/shothistorystorage_queries.cpp
    src/history/coffeebagstorage.cpp
    src/history/equipmentstorage.cpp
    src/history/recipestorage.cpp
    src/history/recipepromotion.cpp
    src/history/unifiedbeansearchmodel.cpp
    src/history/shotprojection.cpp
    src/history/shotdebuglogger.cpp
    src/history/shotfileparser.cpp
    src/history/shotimporter.cpp
    src/history/shothistoryexporter.cpp
    src/models/shotcomparisonmodel.cpp
    src/models/flowcalibrationmodel.cpp
    src/network/shotserver.cpp
    src/network/shotserver_recipes.cpp
    src/network/shotserver_bags.cpp
    src/network/shotserver_equipment.cpp
    src/network/shotserver_shots.cpp
    src/network/shotserver_upload.cpp
    src/network/shotserver_backup.cpp
    src/network/shotserver_layout.cpp
    src/network/shotserver_theme.cpp
    src/network/shotserver_settings.cpp
    src/network/shotserver_auth.cpp
    src/network/shotserver_ai.cpp
    src/mcp/mcpserver.cpp
    src/mcp/mcpremoteaccess.cpp
    src/mcp/mcptunnel_tsnet.cpp
    src/mcp/mcptools_machine.cpp
    src/mcp/mcptools_shots.cpp
    src/mcp/mcptools_profiles.cpp
    src/mcp/mcptools_settings.cpp
    src/mcp/mcptools_presets.cpp
    src/mcp/mcptools_recipes.cpp
    src/mcp/mcptools_beansearch.cpp
    src/mcp/mcptools_dialing.cpp
    src/ai/dialing_blocks.cpp
    src/mcp/mcptools_control.cpp
    src/mcp/mcpresources.cpp
    src/mcp/mcptools_scale.cpp
    src/mcp/mcptools_devices.cpp
    src/mcp/mcptools_write.cpp
    src/mcp/mcptools_agent.cpp
    src/mcp/mcptools_ai.cpp
    src/mcp/mcptools_ai_conversations.cpp
    src/network/mqttclient.cpp
    src/network/mdnsresolver.cpp
    src/network/multicastlock.cpp
    src/network/wifiscaleresult.cpp
    src/network/webdebuglogger.cpp
    src/network/locationprovider.cpp
    src/network/shotreporter.cpp
    src/network/crashreporter.cpp
    src/network/librarysharing.cpp
    src/network/relayclient.cpp
    src/network/screencaptureservice.cpp
    src/network/wifiscalediscovery.cpp
    src/core/settingsserializer.cpp
    src/core/datamigrationclient.cpp
    src/core/databasebackupmanager.cpp
    src/core/documentformatter.cpp
    src/core/keyboard.cpp
    src/weather/weathermanager.cpp
)

# USB serial files — not available on iOS (no QSerialPort, no Android JNI)
if(NOT IOS)
    list(APPEND SOURCES
        src/usb/serialtransport.cpp
        src/usb/usbmanager.cpp
        src/usb/androidusbhelper.cpp
        src/usb/androidusbscalehelper.cpp
        src/usb/usbdecentscale.cpp
        src/usb/usbscalemanager.cpp
    )
endif()

# Simulator engine - listed unconditionally so AUTOMOC sees the same files in
# every configuration. Whether any of it actually compiles is decided by
# DECENZA_SIMULATOR (see the target_compile_definitions block below), which the
# files themselves guard on.
#
# ghcsimulator is here rather than behind a platform test because what it actually
# depends on is DE1Simulator, not a platform: it drives that class's signals and
# cannot compile without it. Tying it to the same condition says so directly, and
# means it is available in every build where the simulator is — all desktop
# configurations, and Debug on Android and iOS.
#
# It used to be `if(WIN32 OR (APPLE AND NOT IOS))`, which had a second cost beyond
# being the wrong condition: GHCSimulator is a registered QML singleton, and a
# registered type only reaches Decenza.qmltypes on a platform that COMPILES it. The
# qmllint baseline is generated on macOS and the gate runs on Linux, so a
# macOS-and-Windows-only type produced a ceiling CI could not achieve — the exact
# shape #1684 added detection for.
list(APPEND SOURCES
    src/simulator/de1simulator.cpp
    src/simulator/simulatedscale.cpp
    src/simulator/ghcsimulator.cpp
)


# Native CoreBluetooth transport and iOS brightness helpers
if(APPLE)
    list(APPEND SOURCES
        src/ble/transport/corebluetooth/corebluetoothscalebletransport.mm
        src/ble/applebtstate.mm
        src/screensaver/iosbrightness.mm
    )
endif()

# iOS-only: SFSafariViewController bridge for opening URLs without universal-link interception
if(IOS)
    list(APPEND SOURCES ios/SafariViewHelper.mm)
    list(APPEND SOURCES ios/MachineStatusWidgetBridge.mm)
endif()

set(HEADERS
    src/widget/machinestatussnapshot.h
    src/widget/widgetsharedkeys.h
    src/core/appsettings.h
    src/core/settingsstoremigration.h
    src/core/settings.h
    # QML_FOREIGN registration for Settings. Listed here so AUTOMOC processes it and
    # qmltyperegistrar sees the type; it is compiled by no other target, which is the point
    # (settings.h stays free of QtQml — see the file's own comment).
    src/core/settings_qml.h
    # Same reason, for the objects main() owns and used to publish with setContextProperty():
    # their class headers are compiled by test and tool targets that link no Qt6::Qml, so the
    # registration lives in a file only this target compiles.
    src/core/contextsingletons_qml.h
    src/core/settings_mqtt.h
    src/core/settings_autowake.h
    src/core/settings_hardware.h
    src/core/settings_ai.h
    src/core/settings_theme.h
    src/core/backgroundpresets.h
    src/core/settings_visualizer.h
    src/core/settings_mcp.h
    src/core/settings_brew.h
    src/core/settings_dye.h
    src/core/settings_network.h
    src/core/settings_app.h
    src/core/settings_calibration.h
    src/core/settings_graph.h
    src/core/grinderaliases.h
    src/core/basketaliases.h
    src/core/widgetlibrary.h
    src/core/batterymanager.h
    src/core/memorymonitor.h
    src/core/fddiagnostics.h
    src/core/logcollapse.h
    src/core/logpaths.h
    src/core/sanitizers.h
    src/core/accessibilitymanager.h
    src/core/autowakemanager.h
    src/core/crashhandler.h
    src/core/profilestorage.h
    src/core/translationmanager.h
    src/core/updatechecker.h
    src/core/updatelogging.h
    src/core/githubreleaseclient.h
    src/core/hdsfirmwarecatalog.h
    src/core/hdsfirmwareupdatecontroller.h
    src/ble/protocol/binarycodec.h
    src/ble/protocol/de1characteristics.h
    src/ble/protocol/decentscaleprotocol.h
    src/ble/blecapability.h
    src/ble/blecontrollererror.h
    src/ble/bleserviceerror.h
    src/ble/blemanager.h
    src/ble/de1transport.h
    src/ble/de1device.h
    src/ble/bletransport.h
    src/ble/scaledevice.h
    src/ble/scaledeviceproxy.h
    src/ble/scales/scalefactory.h
    src/ble/scales/scaletypeids.h
    src/ble/scales/decentscale.h
    src/ble/scales/acaiascale.h
    src/ble/scales/felicitascale.h
    src/ble/scales/skalescale.h
    src/ble/scales/hiroiascale.h
    src/ble/scales/bookooscale.h
    src/ble/scales/smartchefscale.h
    src/ble/scales/difluidscale.h
    src/ble/scales/eurekaprecisascale.h
    src/ble/scales/solobaristascale.h
    src/ble/scales/atomhearteclairscale.h
    src/ble/scales/variaakuscale.h
    src/ble/scales/timemorescale.h
    src/ble/scales/flowscale.h
    src/ble/transport/scalebletransport.h
    src/ble/transport/qtscalebletransport.h
    src/core/asynclogger.h
    src/core/btlogfilter.h
    src/machine/machinestate.h
    src/machine/frameexitreason.h
    src/machine/weightprocessor.h
    src/machine/stepexitarbiter.h
    src/profile/profile.h
    src/profile/profileframe.h
    src/profile/profileconverter.h
    src/profile/profileimporter.h
    src/profile/profilesavehelper.h
    src/profile/recipeparams.h
    src/profile/recipegenerator.h
    src/profile/recipeanalyzer.h
    src/models/shotdatamodel.h
    src/models/steamdatamodel.h
    src/machine/steamhealthtracker.h
    src/controllers/maincontroller.h
    src/controllers/profilemanager.h
    src/controllers/steamheaterpolicy.h
    src/controllers/shottimingcontroller.h
    src/screensaver/screensavervideomanager.h
    src/screensaver/iosbrightness.h
    src/screensaver/strangeattractorrenderer.h
    src/rendering/fastlinerenderer.h
    src/ui/jscanvaspainteritem.h
    src/ui/jscanvascontext.h
    src/network/visualizeruploader.h
    src/network/visualizerimporter.h
    src/network/visualizershotlist.h
    src/network/beanbaseclient.h
    src/network/beanbase_blob.h
    src/ai/aimanager.h
    src/ai/conversationkey.h
    src/ai/aiprovider.h
    src/ai/airequestshape.h
    src/ai/conductance.h
    src/ai/livesteamcoach.h
    src/ai/shotanalysis.h
    src/ai/shotsummarizer.h
    src/history/shothistory_types.h
    src/history/shothistorystorage.h
    src/history/shothistorystorage_internal.h
    src/history/coffeebagstorage.h
    src/history/equipmentstorage.h
    src/history/recipestorage.h
    src/history/recipepromotion.h
    src/history/unifiedbeansearchmodel.h
    src/history/equipmentjoin.h
    src/history/shotprojection.h
    src/history/shotscope.h
    src/history/shotdebuglogger.h
    src/history/shotfileparser.h
    src/history/shotimporter.h
    src/history/shothistoryexporter.h
    src/models/shotcomparisonmodel.h
    src/models/flowcalibrationmodel.h
    src/network/shotserver.h
    src/network/mqttclient.h
    src/network/mdnsresolver.h
    src/network/wifiscaleresult.h
    src/network/webdebuglogger.h
    src/network/locationprovider.h
    src/network/shotreporter.h
    src/network/crashreporter.h
    src/network/librarysharing.h
    src/network/relayclient.h
    src/network/screencaptureservice.h
    src/mcp/mcpserver.h
    src/mcp/mcpsession.h
    src/mcp/mcptoolregistry.h
    src/mcp/mcpresourceregistry.h
    src/mcp/mcpremoteaccess.h
    src/core/settingsserializer.h
    src/core/datamigrationclient.h
    src/core/databasebackupmanager.h
    src/core/documentformatter.h
    src/weather/weathermanager.h
    src/weather/weatherlogging.h
)

# USB serial headers — not available on iOS
if(NOT IOS)
    list(APPEND HEADERS
        src/usb/serialtransport.h
        src/usb/usbmanager.h
        src/usb/androidusbhelper.h
        src/usb/androidusbscalehelper.h
        src/usb/usbdecentscale.h
        src/usb/usbscalemanager.h
    )
endif()

# Simulator engine headers - always included (see source section comment)
list(APPEND HEADERS
    src/simulator/de1simulator.h
    src/simulator/simulatedscale.h
    src/simulator/ghcsimulator.h
)


# Native CoreBluetooth transport header (iOS and macOS)
if(APPLE)
    list(APPEND HEADERS
        src/ble/transport/corebluetooth/corebluetoothscalebletransport.h
        src/ble/applebtstate.h
    )
endif()

# Resources
qt_add_resources(RESOURCES resources/resources.qrc)
qt_add_resources(RESOURCES resources/emoji.qrc)
qt_add_resources(RESOURCES resources/ai.qrc)

# Main executable
qt_add_executable(Decenza
    ${SOURCES}
    ${HEADERS}
    ${RESOURCES}
)

# Link libraries
target_link_libraries(Decenza PRIVATE
    Qt6::Core
    Qt6::CorePrivate
    Qt6::Gui
    Qt6::GuiPrivate
    Qt6::Widgets
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Bluetooth
    Qt6::Graphs
    Qt6::Svg
    Qt6::Multimedia
    Qt6::TextToSpeech
    Qt6::Network
    Qt6::Sql
    Qt6::Positioning
    Qt6::WebSockets
    Qt6::WebSocketsPrivate
    Qt6::CanvasPainter
)

# Embedded Tailscale (Remote MCP Mode A). mcptunnel_tsnet.cpp is always compiled
# (as a stub when disabled) so McpRemoteAccess can reference it unconditionally;
# only when ENABLE_TSNET is on do we pull the prebuilt lib, define the feature
# macro, and link it.
if(ENABLE_TSNET)
    include(${CMAKE_SOURCE_DIR}/cmake/tsnet.cmake)
    target_compile_definitions(Decenza PRIVATE DECENZA_ENABLE_TSNET)
    decenza_link_tsnet(Decenza)
endif()

# Simulation Mode availability.
#
# The simulator is a development aid on a tablet — the machine is right there —
# so tablet RELEASE builds ship without it: no engine, none of DE1Device's
# simulator branches, no main.cpp wiring, and no Settings card. (DE1Device's
# setSimulator() seam itself stays compiled in every configuration — see the
# comment on it for why.) Debug builds keep it on every platform, and
# desktop keeps it in every configuration (running the app on a laptop with no
# DE1 attached is a legitimate way to use it).
#
# DECENZA_SIMULATOR is the single source of truth. src/simulator/*.{h,cpp} guard
# their whole contents on it, so when it is off those translation units compile
# to nothing and no simulator code reaches the binary. The sources stay listed in
# the target either way, which keeps AUTOMOC's view of them stable.
#
# This MUST be a generator expression, not `if(CMAKE_BUILD_TYPE STREQUAL Debug)`.
# The iOS release build uses the Xcode generator, where CMAKE_BUILD_TYPE is empty
# at configure time — a configure-time test silently reads as "not Debug" for
# every config, including Debug. That is exactly what broke the old conditional
# and led #487 to link the engine into every build instead. Android builds
# single-config, where a plain if() would work, but it uses the same genex so
# there is one spelling to reason about rather than two that can diverge.
#
# DECENZA_SIMULATOR_OVERRIDE exists so the simulator-absent shape can be built
# without an Android or iOS toolchain: configure with -DDECENZA_SIMULATOR_OVERRIDE=0
# and a desktop build compiles exactly what ships to tablets. Worth having,
# because otherwise no routine CI job compiles that arm at all — the nightly is
# Linux (always on) and the mobile workflows run only on tag push or manual
# dispatch, so a break of #1629's shape would first surface at a release tag.
# Left empty by default; not cached as a computed default, which would go stale
# if the same build directory were reconfigured for another platform.
set(DECENZA_SIMULATOR_OVERRIDE "" CACHE STRING
    "Force the DECENZA_SIMULATOR condition (0 = build the simulator-absent shape). Empty = per-platform default.")
if(DECENZA_SIMULATOR_OVERRIDE STREQUAL "")
    if(ANDROID OR IOS)
        set(DECENZA_SIMULATOR_CONDITION "$<CONFIG:Debug>")
    else()
        set(DECENZA_SIMULATOR_CONDITION "1")
    endif()
else()
    set(DECENZA_SIMULATOR_CONDITION "${DECENZA_SIMULATOR_OVERRIDE}")
endif()
target_compile_definitions(Decenza PRIVATE
    "$<${DECENZA_SIMULATOR_CONDITION}:DECENZA_SIMULATOR>")

# Link SerialPort only on desktop (not available on Android/iOS)
if(NOT ANDROID AND NOT IOS)
    target_link_libraries(Decenza PRIVATE Qt6::SerialPort)
endif()

# Link Paho MQTT - use SSL variant (paho-mqtt3as) on desktop where OpenSSL is
# statically linked (avoids duplicate SHA1 symbols), non-SSL on Android/iOS
if(NOT ANDROID AND NOT IOS)
    target_link_libraries(Decenza PRIVATE paho-mqtt3as-static)
else()
    target_link_libraries(Decenza PRIVATE paho-mqtt3a-static)
endif()

# Link OpenSSL on desktop only (Android uses KDAB add_android_openssl_libraries,
# iOS uses Apple Security.framework for cert generation)
if(NOT ANDROID AND NOT IOS)
    target_link_libraries(Decenza PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()

# Link psapi on Windows (for GetProcessMemoryInfo in MemoryMonitor)
if(WIN32)
    target_link_libraries(Decenza PRIVATE psapi)
endif()

# Set Windows executable icon
if(WIN32)
    set(app_icon_resource_windows "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/decenza.ico")
    set_property(TARGET Decenza PROPERTY QT_WIN_RC_ICONS "${app_icon_resource_windows}")
endif()

# Qt 6.11 added a new required target property QT_ANDROID_COMPILE_SDK_VERSION
# (the SDK platform used at BUILD time, separate from target SDK at runtime).
# Without it, Qt Creator fails the APK packaging step with "Android build SDK
# version is not defined". Compile SDK >= target SDK conventionally; use
# android-35 here (Android 15) — installed by default by setup-android in CI
# and on most local dev machines. Target SDK is the runtime-targeted API
# level the AndroidManifest declares, controlled by ANDROID_TARGET_SDK_VERSION.
if(ANDROID)
    set_property(TARGET Decenza PROPERTY QT_ANDROID_MIN_SDK_VERSION ${ANDROID_MIN_SDK_VERSION})
    set_property(TARGET Decenza PROPERTY QT_ANDROID_TARGET_SDK_VERSION ${ANDROID_TARGET_SDK_VERSION})
    set_property(TARGET Decenza PROPERTY QT_ANDROID_COMPILE_SDK_VERSION "android-35")
endif()

# Optionally link Quick3D and add 3D screensaver sources
if(ENABLE_QUICK3D AND Qt6Quick3D_FOUND)
    target_link_libraries(Decenza PRIVATE Qt6::Quick3D)
    target_compile_definitions(Decenza PRIVATE ENABLE_QUICK3D)
    target_sources(Decenza PRIVATE
        src/screensaver/pipegeometry.cpp
        src/screensaver/pipegeometry.h
    )
endif()

# iOS/macOS: Use native AVFoundation backend only (no FFmpeg).
# The FFmpeg/VideoToolbox backend leaks Metal/IOSurface memory on macOS
# (~10 MB per video transition, causing SIGBUS after ~15 screensaver videos).
# Excluding the FFmpeg plugin avoids linking libavcodec/libavformat entirely.
if(IOS OR APPLE)
    qt_import_plugins(Decenza
        INCLUDE_BY_TYPE multimedia Qt6::QDarwinMediaPlugin
        INCLUDE_BY_TYPE permissions Qt6::QDarwinLocationPermissionPlugin
    )
endif()

# Include directories
target_include_directories(Decenza PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}  # For version.h
    # Needed by QML_ELEMENT types, whose headers must be reachable by BARE BASENAME.
    # qmltyperegistrar generates decenza_qmltyperegistrations.cpp from moc's JSON and emits
    # `#if __has_include(<translationmanager.h>)` around the include — moc records `inputFile`
    # as the basename alone, and no path prefix changes that (AUTOMOC_PATH_PREFIX does prefix
    # the include moc writes into moc_*.cpp, but not the JSON field this is generated from).
    # When the basename does not resolve, the guarded include expands to nothing and the build
    # fails on `use of undeclared identifier` in generated code — three tools away from the
    # cause. Qt's own modules do not hit this because their headers sit flat in one include dir.
    #
    # Add the directory of each new QML_ELEMENT header here. There are currently no duplicate
    # header basenames across these directories, so this cannot make an include ambiguous —
    # re-check that when adding one (`find <dirs> -maxdepth 1 -name '*.h' -exec basename {} \; |
    # sort | uniq -d`), because an ambiguous basename would silently pick the wrong header.
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/controllers
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ai
    ${CMAKE_CURRENT_SOURCE_DIR}/src/history
    ${CMAKE_CURRENT_SOURCE_DIR}/src/machine
    ${CMAKE_CURRENT_SOURCE_DIR}/src/models
    ${CMAKE_CURRENT_SOURCE_DIR}/src/network
    ${CMAKE_CURRENT_SOURCE_DIR}/src/profile
    # Added when the last runtime qmlRegisterType<> calls became QML_ELEMENT: fastlinerenderer.h,
    # jscanvaspainteritem.h, strangeattractorrenderer.h and pipegeometry.h. Verified non-ambiguous
    # by the command above before adding.
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rendering
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/src/screensaver
)
if(NOT IOS)
    # SYSTEM: mjansson/mdns is vendored third-party code, and it is header-only,
    # so without this it compiles INSIDE our translation units and inherits
    # -Werror. Android caught -Wcast-align on its sockaddr -> sockaddr_in casts,
    # which is idiomatic BSD-socket code we are in no position to "fix".
    # SYSTEM keeps our diagnostics on our code, matching the reason the warning
    # options sit after the FetchContent blocks.
    #
    # Must stay SYSTEM on every platform this is enabled for, not just Android:
    # the header is equally -Wcast-align-dirty under MSVC and desktop GCC/Clang,
    # and those builds are -Werror too. Keep this guard in step with the
    # FetchContent block above.
    target_include_directories(Decenza SYSTEM PRIVATE ${mdns_lib_SOURCE_DIR})
endif()
if(IOS)
    target_include_directories(Decenza PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ios)
endif()

# QML module - mark singletons
set_source_files_properties(qml/AppShell.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(qml/components/SettingsTabs.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(qml/components/layout/WidgetColor.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(qml/components/layout/LayoutActions.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(qml/components/DrinkType.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(qml/components/SteamLabels.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(qml/components/LastShotChartSource.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(qml/components/GraphSeries.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

# Define QML files list for dependency tracking
set(QML_FILES
    qml/main.qml
    qml/AppShell.qml
    qml/Theme.qml
    qml/pages/IdlePage.qml
    qml/pages/EspressoPage.qml
    qml/pages/SteamPage.qml
    qml/pages/HotWaterPage.qml
    qml/pages/SettingsPage.qml
    qml/pages/ProfileEditorPage.qml
    qml/pages/RecipeEditorPage.qml
    qml/pages/SimpleProfileEditorPage.qml
    qml/pages/PressureEditorPage.qml
    qml/pages/FlowEditorPage.qml
    qml/pages/ProfileSelectorPage.qml
    qml/pages/DescalingPage.qml
    qml/pages/TransportPage.qml
    qml/pages/SensorCalibrationPage.qml
    qml/pages/ScreensaverPage.qml
    qml/pages/FlushPage.qml
    qml/pages/VisualizerBrowserPage.qml
    qml/pages/ProfileImportPage.qml
    qml/pages/VisualizerMultiImportPage.qml
    qml/pages/BeanInfoPage.qml
    qml/pages/EquipmentPage.qml
    qml/pages/RecipesPage.qml
    qml/pages/RecipeWizardPage.qml
    qml/pages/PostShotReviewPage.qml
    qml/pages/AISettingsPage.qml
    qml/pages/ShotHistoryPage.qml
    qml/pages/AutoFavoritesPage.qml
    qml/pages/AutoFavoriteInfoPage.qml
    qml/pages/ShotDetailPage.qml
    qml/pages/ShotComparisonPage.qml
    qml/pages/FlowCalibrationPage.qml
    qml/pages/ProfileInfoPage.qml
    qml/pages/settings/SettingsConnectionsTab.qml
    qml/pages/settings/SettingsMachineTab.qml
    qml/pages/settings/SettingsCalibrationTab.qml
    qml/pages/settings/SettingsScreensaverTab.qml
    qml/pages/settings/SettingsVisualizerTab.qml
    qml/pages/settings/SettingsAITab.qml
    qml/pages/settings/SettingsThemesTab.qml
    qml/pages/settings/SettingsLanguageTab.qml
    qml/pages/settings/SettingsHistoryDataTab.qml
    qml/pages/settings/SettingsHomeAutomationTab.qml
    qml/pages/settings/SettingsUpdateTab.qml
    qml/pages/settings/SettingsFirmwareTab.qml
    qml/pages/settings/SettingsDebugTab.qml
    qml/pages/settings/SettingsSearchDialog.qml
    qml/pages/settings/DeviceMigrationDialog.qml
    qml/pages/settings/SettingsLayoutTab.qml
    qml/pages/settings/LayoutEditorZone.qml
    qml/pages/settings/AddLanguagePage.qml
    qml/pages/settings/StringBrowserPage.qml
    qml/components/graphs/AutoRangingAxis.qml
    qml/components/graphs/CustomLegend.qml
    qml/components/graphs/DashedLineSeries.qml
    qml/components/graphs/DecenzaGraphsTheme.qml
    qml/components/ActionButton.qml
	qml/components/BrewDialog.qml
    qml/components/CircularGauge.qml
    qml/components/ConnectionIndicator.qml
    qml/components/LiveCoachingBanner.qml
    qml/components/FavoritesListView.qml
    qml/components/FavoritesRowAction.qml
    qml/components/RepeaterDelegateItem.qml
    qml/components/StatusToast.qml
    qml/components/layout/LayoutWidgetItem.qml
    qml/components/PresetPillRow.qml
    qml/components/ValueInput.qml
    qml/components/RatingInput.qml
    qml/components/TastePicker.qml
    qml/components/ShotGraph.qml
    qml/components/SteamGraph.qml
    qml/components/StatusBar.qml
    qml/components/RatioPresetDialog.qml
    qml/components/TempPickerDialog.qml
    qml/components/GrindField.qml
    qml/components/GrindPickerDialog.qml
    qml/components/GrindRowSource.qml
    qml/components/StableWeightCapture.qml
    qml/components/ShotPlanText.qml
    qml/components/SteamPlanText.qml
    qml/components/ProfileGraph.qml
    qml/components/ProfilePreviewPopup.qml
    qml/components/ProfileDialInDiffBlock.qml
    qml/components/ProfileInfoButton.qml
    qml/components/BottomBar.qml
    qml/components/BottomBarSubtitle.qml
    qml/components/ThemedIcon.qml
    qml/components/TranslationErrorToast.qml
    qml/components/AccessibleMouseArea.qml
    qml/components/AccessibleTapHandler.qml
    qml/components/AccessibleButton.qml
    qml/components/HideKeyboardButton.qml
    qml/components/ExpandableTextArea.qml
    qml/components/FocusIndicator.qml
    qml/components/ColorEditor.qml
    qml/components/ColorSwatch.qml
    qml/components/StyledTextField.qml
    qml/components/SubsystemLogView.qml
    qml/components/SuggestionField.qml
    qml/components/BeanBaseSearchBar.qml
    qml/components/BeanBaseDetailsPopup.qml
    qml/components/BeanBaseDetailsRow.qml
    qml/components/BeanSummary.qml
    qml/components/LinkBeanBaseDialog.qml
    qml/components/ChangeBeansDialog.qml
    qml/components/CardTapArea.qml
    qml/components/BagCard.qml
    qml/components/SwitchEquipmentDialog.qml
    qml/components/EquipmentCard.qml
    qml/components/EquipmentSummary.qml
    qml/components/EquipmentInfoDialog.qml
    qml/components/KeyboardAwareContainer.qml
    qml/components/AIConversationPanel.qml
    qml/components/ConversationOverlay.qml
    qml/components/Tr.qml
    qml/components/RecipeSection.qml
    qml/components/RecipeRow.qml
    qml/components/RecipeDrinkCard.qml
    qml/components/RecipeField.qml
    qml/components/RecipeResolver.qml
    qml/components/DrinkType.qml
    qml/components/SteamLabels.qml
    qml/components/ProfileKnowledgeDialog.qml
    qml/components/KbDerivedFromLabel.qml
    qml/components/BeanThumbnail.qml
    qml/components/BackgroundSurface.qml
    qml/components/LastShotChartRenderer.qml
    qml/components/LastShotChartSource.qml
    qml/components/ThemedPageBackground.qml
    qml/components/BackgroundPickerDialog.qml
    qml/components/PresetButton.qml
    qml/components/AccessibleLabel.qml
    qml/components/ComparisonGraph.qml
    qml/components/HistoryShotGraph.qml
    qml/components/GraphInspectBar.qml
    qml/components/ComparisonInspectBar.qml
    qml/components/ComparisonDataTable.qml
    qml/components/ComparisonShotTable.qml
    qml/components/GraphLegend.qml
    qml/components/GraphFlowScaleCard.qml
    qml/components/GraphOptionsButton.qml
    qml/components/GraphOptionsDialog.qml
    qml/components/GraphOptionToggleCard.qml
    qml/components/GraphRightAxisLabels.qml
    qml/components/GraphSeries.qml
    qml/components/QualityBadges.qml
    qml/components/PhaseSummaryPanel.qml
    qml/components/ShotAnalysisDialog.qml
    qml/components/CupFillView.qml
    qml/components/PhaseTimelineView.qml
    qml/components/ExtractionViewSelector.qml
    qml/components/StyledSwitch.qml
    qml/components/StyledComboBox.qml
    qml/components/SelectionDialog.qml
    qml/components/StyledIconButton.qml
    qml/components/StyledTabButton.qml
    qml/components/FlipClockScreensaver.qml
    qml/components/StrangeAttractorScreensaver.qml
    qml/components/ShotMapScreensaver.qml
    qml/components/UnsavedChangesDialog.qml
    qml/components/DatePickerDialog.qml
    qml/components/SwipeableArea.qml
    qml/components/DecenzaDialog.qml
    qml/components/CrashReportDialog.qml
    qml/components/McpConfirmDialog.qml
    qml/components/De1CommunicationErrorDialog.qml
    qml/components/ProfileRefusedDialog.qml
    qml/components/ColoredIcon.qml
    qml/components/SearchKeywordChip.qml
    qml/components/CrtOverlay.qml
    qml/components/CrtShaderEffect.qml
    qml/components/QrCode.qml
    qml/components/DateUtils.js
    qml/components/GraphUtils.js
    qml/components/qrcode.js
    qml/components/RecipeSearch.js
    qml/components/SettingsSearchIndex.js
    qml/components/SettingsTabs.qml
    qml/components/SettingsActionRow.qml

    qml/components/layout/LayoutItemDelegate.qml
    qml/components/layout/LayoutBarZone.qml
    qml/components/layout/LayoutCenterZone.qml
    qml/components/layout/LayoutPreview.qml
    qml/components/layout/items/EspressoItem.qml
    qml/components/layout/items/SteamItem.qml
    qml/components/layout/items/HotWaterItem.qml
    qml/components/layout/items/FlushItem.qml
    qml/components/layout/items/BeansItem.qml
    qml/components/layout/items/RecipesItem.qml
    qml/components/layout/items/EquipmentItem.qml
    qml/components/layout/items/HistoryItem.qml
    qml/components/layout/items/DiscussItem.qml
    qml/components/layout/items/AutoFavoritesItem.qml
    qml/components/layout/items/SleepItem.qml
    qml/components/layout/items/SettingsItem.qml
    qml/components/layout/items/TemperatureItem.qml
    qml/components/layout/items/WaterLevelItem.qml
    qml/components/layout/items/ScaleWeightItem.qml
    qml/components/layout/items/ProfileNameItem.qml
    qml/components/layout/items/DoseWeightItem.qml
    qml/components/layout/items/MilkWeightItem.qml
    qml/components/layout/items/RatioQuickSelectItem.qml
    qml/components/layout/items/GrindQuickSelectItem.qml
    qml/components/layout/items/TempQuickSelectItem.qml
    qml/components/layout/items/ShotPlanItem.qml
    qml/components/layout/items/SpacerItem.qml
    qml/components/layout/LayoutActions.qml
    qml/components/layout/GestureActionRow.qml
    qml/components/layout/items/CustomItem.qml
    qml/components/layout/items/WeatherItem.qml
    qml/components/layout/items/PageTitleItem.qml
    qml/components/layout/items/SteamTemperatureItem.qml
    qml/components/layout/items/SeparatorItem.qml
    qml/components/layout/items/QuitItem.qml
    qml/components/layout/items/ScreensaverItem.qml
    qml/components/layout/items/LastShotItem.qml
    qml/components/layout/items/MachineStatusItem.qml
    qml/components/layout/items/BatteryLevelItem.qml
    qml/components/layout/items/ScaleBatteryItem.qml
    qml/components/layout/items/MiniGHCItem.qml
    qml/components/layout/items/ClockItem.qml
    qml/components/layout/CustomEditorPopup.qml
    qml/components/layout/ZoneOptionsPopup.qml
    qml/components/layout/ReadoutOptionsPopup.qml
    qml/components/layout/WidgetColor.qml
    qml/components/layout/WidgetColorPicker.qml
    qml/components/layout/SleepEditorPopup.qml
    qml/components/layout/ScreensaverEditorPopup.qml
    qml/components/layout/ShotPlanConfig.js
    qml/components/layout/PillFit.js
    qml/components/layout/EmojiPicker.qml
    qml/components/layout/EmojiData.js
    qml/components/library/LibraryPanel.qml
    qml/components/library/LibraryItemCard.qml
    qml/pages/CommunityBrowserPage.qml
)

# Simulator QML - desktop only (second top-level window; see source section).
# `WIN32 OR APPLE` would include iOS, which shipped this window's compiled QML
# in every IPA — dead weight for a window iOS never loads, and whose
# GHCSimulator context property does not exist there at all.
#
# Linux is in the list even though main.cpp never loads this window there — its load site is
# `#if (Q_OS_WIN || Q_OS_MACOS) && QT_DEBUG`. It is here so the module's file set does not vary
# by desktop platform, because the qmllint gate's file set is derived from it. Nightly CI is the
# only automated lint run and it is Linux: with the file excluded there, CI linted 216 files
# against a 217-file baseline, and this file's 57 diagnostics were checked by nobody. The gate
# caught that on its first CI run. Cost of carrying it is a few KB of compiled QML in a Linux
# desktop build; the alternative — NOT_IN_MODULE_BY_DESIGN in scripts/qmllint_report.py — buys
# the same green at the price of never linting the file again.
if(NOT ANDROID AND NOT IOS)
    list(APPEND QML_FILES
        qml/simulator/GHCSimulatorWindow.qml
    )
endif()

# Quick3D screensavers - only when Quick3D is available
if(ENABLE_QUICK3D AND Qt6Quick3D_FOUND)
    list(APPEND QML_FILES
        qml/components/PipesScreensaver.qml
        qml/components/ShotMapGlobe.qml
    )
endif()

# DEPENDENCIES is what lets qmllint follow a Decenza type's PROTOTYPE into another module.
# Without it, PipeCylinderGeometry's base QQuick3DGeometry is a name the linter cannot link, and
# every use of those types reports "used but it is not resolved" — even though Qt ships
# Quick3D.qmltypes and its qml directory is already on the import path. The import path resolves
# what QML *imports*; this resolves what our own registered types *inherit*.
# Conditional for the same reason the sources are: pipegeometry.* is not compiled without Quick3D,
# so naming the dependency there would declare one the module does not have.
set(DECENZA_QML_DEPENDENCIES "")
if(ENABLE_QUICK3D AND Qt6Quick3D_FOUND)
    list(APPEND DECENZA_QML_DEPENDENCIES QtQuick3D)
endif()

qt_add_qml_module(Decenza
    URI Decenza
    VERSION 1.1
    QML_FILES ${QML_FILES}
    DEPENDENCIES ${DECENZA_QML_DEPENDENCIES}
    NO_GENERATE_EXTRA_QMLDIRS
)

# ---------------------------------------------------------------------------
# Precompiled header for the Decenza target.
#
# Placed after qt_add_qml_module ON PURPOSE: the property applies to every
# source of the target, and the largest beneficiary is not hand-written code at
# all — it is the ~226 generated .cpp files qmlcachegen emits under
# .rcc/qmlcache/, which are compiled into THIS target and which all carry the
# same ~20 QtQml/QtCore includes (71 of them share a byte-identical include
# block; 62 more share a second). Those edges are 32% of a clean build, larger
# than every hand-written src/*.cpp put together (27.8%).
#
# Measured on this machine, isolated single-TU compiles with the real flags:
#
#   qmlcache generated (12 sampled)   13.35s -> 6.46s   52%
#   src/*.cpp          (15 sampled)   29.74s -> 21.56s  28%
#
# The percentages differ but the SECONDS do not: the saving is ~0.5 s per TU
# almost regardless of file size, because a PCH removes one fixed header parse
# and nothing else. stepexitarbiter.cpp saves 0.42 s (56%); shotserver.cpp saves
# 0.48 s (14%). Do not read the low percentage on a big file as "PCH does not
# help there" — it helped by the same amount, the file just does more work.
#
# QT AND STDLIB HEADERS ONLY. A project header in here rebuilds the PCH and then
# every TU in the target on every edit to it, which is strictly worse than no
# PCH at all. That specifically rules out settings.h, which is otherwise the
# most-included project header in the tree.
#
# Verified green on FIVE of the six release workflows: Linux, Linux-arm64 and
# Android (Ninja), macOS (Xcode), and Windows (Ninja, but with this switch OFF,
# so that leg proves only that the guard works). iOS is NOT in that list — it
# fails, and is disabled at the switch above, where the diagnosis lives.
#
# Do not restore the sentence this replaced. It read "Verified to work under
# every generator the six release workflows use ... and Xcode for macOS/iOS",
# which was true of a local probe and false of iOS in CI, and it sat here
# contradicting the iOS failure recorded seventy lines above for as long as it
# took a reviewer to notice. The probe it described was real — `-G Xcode` with
# -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64" does emit one cmake_pch.hxx.gch per
# slice and link a correct fat binary — but a passing probe is not a passing
# platform, and writing it as though it were is how a stale claim outlives the
# thing it claimed.
if(DECENZA_ENABLE_PCH)
target_precompile_headers(Decenza PRIVATE
    <QtCore/qobject.h>
    <QtCore/qstring.h>
    <QtCore/qstringlist.h>
    <QtCore/qvariant.h>
    <QtCore/qdatetime.h>
    <QtCore/qurl.h>
    <QtCore/qdebug.h>
    <QtCore/qjsonobject.h>
    <QtCore/qjsonarray.h>
    <QtCore/qjsondocument.h>
    # The QtQml set below is what qmlcachegen emits into every generated file.
    <QtQml/qqmlprivate.h>
    <QtQml/qjsengine.h>
    <QtQml/qjsvalue.h>
    <QtQml/qjsprimitivevalue.h>
    <QtQml/qqmlcomponent.h>
    <QtQml/qqmlcontext.h>
    <QtQml/qqmlengine.h>
    <QtQml/qqmllist.h>
    # …and so are these six, which the first version of this list MISSED because
    # it was built by reading one generated file's include block and stopping at
    # the QtQml lines. type_traits and qtimezone.h appear in 216 of the 226
    # generated TUs — 95.6%, the identical share as every QtQml header above —
    # so leaving them out meant re-parsing a heavy template header 216 times.
    # The other four sit at 45.1%.
    #
    # Do not extend this list by eye. Regenerate the evidence:
    #   grep -h '^#include <' build/<dir>/.rcc/qmlcache/**/*.cpp | sort | uniq -c | sort -rn
    # and for the hand-written half, the same over src/**/*.cpp. A header below
    # roughly 40% of the TUs in a target is not obviously worth its place — every
    # TU maps it whether or not it uses it — and that threshold is a guess that
    # has never been measured, so treat additions as an experiment, not an edit.
    <type_traits>
    <QtCore/qtimezone.h>
    <cmath>
    <limits>
    <QtCore/qalgorithms.h>
    <QtCore/qrandom.h>
)
endif()

# The Objective-C++ sources must opt OUT of the PCH entirely.
#
# This was first written as a $<COMPILE_LANGUAGE:CXX> generator expression on
# the header list above, on the assumption that it would confine the PCH to C++.
# It does not. That genex controls the CONTENTS of the generated PCH per
# language, and CMake still passes `-include-pch` to the .mm compile lines —
# with the C++ PCH, because the OBJCXX one it would otherwise build was made
# empty by the very genex meant to protect it. clang then refuses outright:
#
#   error: Objective-C was disabled in precompiled file 'cmake_pch.hxx.pch'
#          but is currently enabled
#
# SKIP_PRECOMPILE_HEADERS is the property that actually means "do not apply a
# PCH to this source". Five files (three APPLE, two IOS) are not worth a second
# ~28 MB ObjC++ artefact, so they simply go without.
#
# Derived from SOURCES rather than hand-listed: the .mm files are added inside
# if(APPLE)/if(IOS) blocks, so a hand-list would compile clean on Linux and
# Windows and break only the two Apple workflows — the same shape as the iOS
# link bug that got through review in #1728.
foreach(_decenza_src IN LISTS SOURCES)
    if(_decenza_src MATCHES "\\.mm$")
        set_source_files_properties(${_decenza_src}
            PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
    endif()
endforeach()
unset(_decenza_src)

# Compile CRT shader for GPU effect (requires ShaderTools)
if(Qt6ShaderTools_FOUND)
    qt_add_shaders(Decenza "crt_shaders"
        BATCHABLE
        PREFIX "/"
        FILES
            shaders/crt.frag
    )
    qt_add_shaders(Decenza "cup_shaders"
        BATCHABLE
        PREFIX "/"
        FILES
            shaders/cup_mask.frag
            shaders/cup_lighten.frag
    )
endif()

# Apply version code on every build (reads versioncode.txt as-is, no increment).
# BYPRODUCTS tells Ninja that this target produces version_code.cpp, so dependents
# (the compilation of version_code.cpp into an object file) are scheduled after this
# target and properly detect the file change. This fixes the stale build number issue
# where Ninja's pre-computed dirty state missed header-only changes.
add_custom_target(apply_version_code ALL
    COMMAND ${CMAKE_COMMAND}
        -DVERSION_CODE_FILE=${VERSION_CODE_FILE}
        -DMANIFEST_FILE=${CMAKE_SOURCE_DIR}/android/AndroidManifest.xml
        -DMANIFEST_TEMPLATE=${CMAKE_SOURCE_DIR}/android/AndroidManifest.xml.in
        -DVERSION_STRING=${PROJECT_VERSION}
        -DCMAKE_SOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DVERSION_CODE_CPP=${VERSION_CODE_CPP}
        -DVERSION_CODE_CPP_TEMPLATE=${CMAKE_SOURCE_DIR}/src/version_code.cpp.in
        -DPROJECT_VERSION=${PROJECT_VERSION}
        -DPROJECT_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        -DPROJECT_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        -DPROJECT_VERSION_PATCH=${PROJECT_VERSION_PATCH}
        -DISS_TEMPLATE=${CMAKE_SOURCE_DIR}/installer/version.iss.in
        -DISS_OUTPUT=${CMAKE_SOURCE_DIR}/installer/version.iss
        -P ${CMAKE_SOURCE_DIR}/cmake/ApplyVersionCode.cmake
    BYPRODUCTS ${VERSION_CODE_CPP}
    COMMENT "Applying version code..."
)
add_dependencies(Decenza apply_version_code)

# Add generated version_code.cpp to the build (recompiled every build, triggers relink)
target_sources(Decenza PRIVATE ${VERSION_CODE_CPP})
set_source_files_properties(${VERSION_CODE_CPP} PROPERTIES GENERATED TRUE)

# Build-time KB validator (change: restructure-kb-as-validated-json, D5/2.4).
# HARD GATE: a schema / typo'd-key / out-of-range / provenance↔src /
# alias-integrity violation in resources/ai/profile_knowledge.json fails the
# build. Pass/fail check ONLY — no generated artifact enters the build graph,
# and the target is standalone (not a custom command shared between targets),
# deliberately avoiding the cross-platform-codegen failure class (2b6e0965).
# validate_kb.py is stdlib-only Python 3. A missing interpreter is a HARD
# FAILURE, not a soft skip: nothing else actually performs schema validation
# (no test runs validate_kb.py; CI builds on tag, not PR), so degrading to a
# warning would let malformed/typo'd/colliding KB data ship behind an unread
# build log. Python 3 is a de-facto build dependency on all supported
# platforms; if it is genuinely unavailable, fail loudly so it gets installed.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(NOT Python3_Interpreter_FOUND)
    message(FATAL_ERROR
        "Python3 not found — required for the build-time KB validator gate "
        "(tools/validate_kb.py). It is the ONLY schema/integrity check for "
        "resources/ai/profile_knowledge.json; skipping it would let invalid "
        "KB data ship. Install Python 3.")
endif()
add_custom_target(validate_kb ALL
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/tools/validate_kb.py
        ${CMAKE_SOURCE_DIR}/resources/ai/profile_knowledge.json
    COMMENT "Validating profile_knowledge.json (schema + integrity)..."
    VERBATIM
)
add_dependencies(Decenza validate_kb)

# ---------------------------------------------------------------------------
# QML diagnostics gate (qmllint)
#
# Part of the default build (the qmllint_gate target below), plus two explicit ones:
#
#     cmake --build <dir> --target qmllint_check     # the gate, on demand
#     cmake --build <dir> --target qmllint_report    # what is actually in there
#
# This block used to say the opposite — "deliberately NOT part of the default
# build ... a full-tree qmllint run takes minutes". That was never measured. It is
# ~11 s for the whole tree on a 2023 MacBook Pro and ~14 s on a CI runner. At that
# price there is no reason a developer should have to remember to ask: the whole
# point of a gate at zero is that a regression is visible the moment it is written,
# not the next night on main.
#
# One mode, one number: every file in the module, on every platform, with the qmllint
# that ships beside the pinned Qt. Until Qt 6.11.2 this was not possible —
# QQmlJSRegisterContent::merge() was tree-recursive and one layout item drove the
# released linter to a 313 GB footprint and an OOM kill, so the project carried a
# patched binary on macOS and skipped that file everywhere else. 6.11.2 memoizes the
# merge (Gerrit 757430), so the stock tool finishes it. If a future Qt ever
# reintroduces such a file, state the gap and FAIL — do not add a skip flag. A file
# the linter never reached emits no warnings and therefore reads as clean, which is
# the one failure mode this gate cannot survive.
#
# It does NOT re-run on every build. The work is a custom command producing a
# stamp, so ninja/make skip it entirely when nothing it reads has changed — a
# C++-only edit pays nothing. Without that, every 2 s incremental relink would
# cost 13 s and the first thing anyone would do is switch it off.
#
# Why it exists: an undeclared QML identifier compiles clean and throws only
# when its binding is first evaluated — possibly on a screen nobody opened
# during review. That is not hypothetical here. It shipped in 2.0.1 (#1661),
# where Theme.qml referenced a singleton living in another directory, so
# hasBackgroundImage stayed false forever and every user who chose the
# shot-chart background silently lost its scrim.
#
# The import path is the BUILD directory, not the source tree, because that is
# where the generated Decenza/qmldir lives and it points at the build's own
# copies of the QML. Lint against a stale build and the numbers describe
# whatever was last compiled rather than what is on disk — the script hard-errors
# on that rather than reporting a plausible wrong answer.
#
# Policy (per-file clean list, category ceilings) lives in the script, next to
# the parser that produces the numbers it enforces. This target only wires the
# two together.
# ---------------------------------------------------------------------------
get_target_property(_qt_qmake_location Qt6::qmake IMPORTED_LOCATION)
get_filename_component(_qt_bin_dir "${_qt_qmake_location}" DIRECTORY)
find_program(QMLLINT_EXECUTABLE
    NAMES qmllint
    HINTS "${_qt_bin_dir}"
    DOC "qmllint from the Qt build this project targets"
)

if(QMLLINT_EXECUTABLE)
    add_custom_target(qmllint_check
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/scripts/qmllint_report.py
            --check
            --qmllint ${QMLLINT_EXECUTABLE}
            --import-path ${CMAKE_BINARY_DIR}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Checking QML diagnostics against the baseline..."
        VERBATIM
    )
    add_custom_target(qmllint_report
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/scripts/qmllint_report.py
            --report
            --qmllint ${QMLLINT_EXECUTABLE}
            --import-path ${CMAKE_BINARY_DIR}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Summarising QML diagnostics by category and file..."
        VERBATIM
    )
    # Both targets need the module built: qmllint resolves Decenza types through the generated
    # qmldir, which points at the build's own copies of the QML, and Qt's generated argument
    # file (.rcc/qmllint/Decenza.rsp) does not exist until then. The script refuses to run
    # against a stale build rather than report numbers for whatever was last compiled — this
    # dependency turns that refusal into a rebuild instead of an error message.
    add_dependencies(qmllint_check Decenza)
    add_dependencies(qmllint_report Decenza)

    # The same check, in the default build, so a developer sees a regression when they
    # write it rather than reading about it in the next morning's nightly.
    #
    # A custom command with a real OUTPUT, not a second add_custom_target: a target with
    # no outputs is always considered dirty and would re-lint on every single build,
    # including a one-line C++ edit that cannot change a QML diagnostic. The stamp lets
    # the generator skip the whole 10.8 s when nothing below has changed.
    #
    # KNOWN GAP: the DEPENDS list is the QML sources, the script and the baseline — not
    # the generated Decenza.qmltypes. So a pure-C++ change to a QML-exposed type (dropping
    # a Q_PROPERTY, say) can invalidate a QML binding without re-triggering this. Such a
    # change almost always touches QML in the same commit, and the nightly on main catches
    # what slips; depending on the qmltypes path directly was not done because it is a
    # Qt-internal output location and naming it wrong breaks the build for everyone.
    # `--target qmllint_check` always re-runs unconditionally, which is the escape hatch.
    set(_qmllint_stamp "${CMAKE_BINARY_DIR}/.qmllint_check.stamp")
    add_custom_command(
        OUTPUT "${_qmllint_stamp}"
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/scripts/qmllint_report.py
            --check
            --qmllint ${QMLLINT_EXECUTABLE}
            --import-path ${CMAKE_BINARY_DIR}
        COMMAND ${CMAKE_COMMAND} -E touch "${_qmllint_stamp}"
        DEPENDS
            ${QML_FILES}
            ${CMAKE_SOURCE_DIR}/scripts/qmllint_report.py
            ${CMAKE_SOURCE_DIR}/qml-diagnostics-baseline.json
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Checking QML diagnostics against the baseline..."
        VERBATIM
    )
    # Desktop only, and not because mobile deserves less checking — the QML is identical,
    # so a desktop run has already checked it. What differs is the cost of being wrong:
    # ANDROID/IOS builds are the tag-push release workflows, and a gate that can fail is a
    # new way for a release to fail. `qmllint_check` still exists there on demand.
    if(ANDROID OR IOS)
        add_custom_target(qmllint_gate DEPENDS "${_qmllint_stamp}")
    else()
        add_custom_target(qmllint_gate ALL DEPENDS "${_qmllint_stamp}")
    endif()
    # Ordering, not content: the script hard-errors on a stale build dir, so the module
    # has to be built before it runs. (See the import-path note above.)
    add_dependencies(qmllint_gate Decenza)
else()
    # Not fatal: qmllint ships with Qt's QML tooling, which a runtime-only Qt
    # install can lack.
    message(WARNING
        "qmllint not found next to ${_qt_bin_dir} — the qmllint_check/qmllint_report "
        "targets and the qmllint_gate step of the default build are all unavailable, so "
        "THIS build is not checking QML diagnostics at all. The nightly on main still is "
        "(.github/workflows/nightly-sanitizers.yml), and it fails loudly rather than "
        "silently skipping, so a regression is caught — just a day later than here.")
endif()

# Force QML resource rebuild when source files change
# NOTE: Disabled - defeats QML caching which slows page loads
# Re-enable temporarily if QML changes aren't being picked up
# set(QML_RCC_CPP "${CMAKE_CURRENT_BINARY_DIR}/.qt/rcc/qrc_Decenza_raw_qml_0.cpp")
# add_custom_target(qml_force_rebuild ALL
#     COMMAND ${CMAKE_COMMAND} -E rm -f ${QML_RCC_CPP}
#     COMMENT "Invalidating QML resources for rebuild..."
# )
# add_dependencies(Decenza qml_force_rebuild)

# Also add QML files as sources so IDE can find them
set_property(TARGET Decenza APPEND PROPERTY SOURCES ${QML_FILES})

# ---------------------------------------------------------------------------
# Cross-file QML cache dependencies
#
# qmlcachegen resolves QML-DECLARED types (singletons like Theme, and every
# component used by name) by READING those files through `-I <builddir>`. It does
# not declare them as inputs, and it has no --depfile, so it cannot report them
# either: Qt's own add_custom_command (Qt6QmlMacros.cmake:3864) lists only the
# file's own source, the .qrc files, Decenza.qmltypes and qmldir.
#
# The result is a build that is quietly WRONG rather than merely stale. Editing
# Theme.qml changes how all ~214 other units should compile and rebuilds none of
# them; the app then runs on a mixed cache, which is not cosmetic — it produced
# `Unable to assign [undefined] to QString` at runtime, the exact failure that
# once got type annotations banned from Theme's temperature wrappers for a
# release. Until now the only defence was remembering to run
# `find qml -name '*.qml' -exec touch {} +`, which is not a defence.
#
# So: every unit depends on every QML source. Coarse on purpose — the precise
# graph would mean reimplementing QML type resolution, and getting THAT subtly
# wrong reintroduces silent staleness, which is the thing being fixed.
#
# It is affordable because of `restat = 1` on the cachegen edges. Measured from
# this project's .ninja_log: qmlcachegen is ~205 ms median per file, while
# compiling the .cpp it emits is ~3387 ms — 16.5x more. A one-file QML edit now
# re-runs cachegen on all of them (a few seconds wall, in parallel) and ninja
# then prunes every unit whose generated .cpp came out byte-identical, so the
# expensive compile still only happens where the output actually changed.
#
# The real fix is `qmlcachegen --depfile` upstream. This is the local stand-in.
set(_decenza_qml_abs "")
foreach(_qml_file IN LISTS QML_FILES)
    get_filename_component(_abs "${_qml_file}" ABSOLUTE)
    list(APPEND _decenza_qml_abs "${_abs}")
endforeach()

# Path formula mirrored from Qt6QmlMacros.cmake — keep in step with it. In Qt 6.11.2 the
# three substitutions are at :3864, :3867 and :3870 (they were around :3841-3847 in 6.11.1;
# cite the version you actually read, because this formula MOVES).
#
# 6.11.2 CHANGED IT, and that is not cosmetic: the character class gained `/` and a space,
#
#     6.11.1:  string(REGEX REPLACE "[$#?]+"    "_" ...)
#     6.11.2:  string(REGEX REPLACE "[$#?/ ]+"  "_" ...)
#
# so directory separators are now flattened into the file name. `qml/AppShell.qml` compiles
# to `Decenza_qml_AppShell_qml.cpp` in one flat directory, where it used to be
# `Decenza_qml/AppShell_qml.cpp` in a nested one. Getting this wrong is not silent: the
# APPEND below fails configure for every unit at once (240 of them on the 6.11.2 bump), which
# is exactly what the gate under this loop promises and the reason it is written this way.
set(_decenza_qml_units_wired 0)
foreach(_qml_file IN LISTS QML_FILES)
    get_filename_component(_abs "${_qml_file}" ABSOLUTE)
    file(RELATIVE_PATH _rel "${CMAKE_CURRENT_SOURCE_DIR}" "${_abs}")
    # Qt replaces a leading `../` run first, so a source outside the tree cannot escape the
    # cache directory. No QML_FILES entry does that today; carried anyway to stay a faithful
    # mirror rather than a formula that happens to agree on the current inputs.
    string(REGEX REPLACE "\\.\\.[/\\\\]" "_" _compiled "${_rel}")
    string(REGEX REPLACE "\\.(js|mjs|qml)$" "_\\1" _compiled "${_compiled}")
    string(REGEX REPLACE "[$#?/ ]+" "_" _compiled "${_compiled}")
    set(_compiled "${CMAKE_CURRENT_BINARY_DIR}/.rcc/qmlcache/Decenza_${_compiled}.cpp")
    add_custom_command(OUTPUT "${_compiled}" APPEND DEPENDS ${_decenza_qml_abs})
    math(EXPR _decenza_qml_units_wired "${_decenza_qml_units_wired} + 1")
endforeach()
message(STATUS "QML cross-file cache deps wired for ${_decenza_qml_units_wired} units")

# The gate. NOTE: `add_custom_command(APPEND)` against an OUTPUT that does not match an
# existing command is a hard configure-time error, NOT a silent no-op — verified by
# reproduction with this project's CMake 3.30.5. So path drift fails the build by itself,
# and this target covers the other half: that the accepted dependencies actually reach the
# generator and cause a rebuild. Exercise it rather than reading the count above:
#
#   cmake --build <builddir> --target qml_dep_wiring_check
#
# It touches Theme.qml and asserts the generator re-runs cachegen on every unit.
add_custom_target(qml_dep_wiring_check
    COMMAND ${CMAKE_COMMAND}
            -DQML_DIR=${CMAKE_CURRENT_SOURCE_DIR}/qml
            -DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DEXPECTED_UNITS=${_decenza_qml_units_wired}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckQmlDepWiring.cmake
    VERBATIM
    USES_TERMINAL
)

# Android deployment
if(ANDROID)
    set_target_properties(Decenza PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android"
        # Note: versionCode and versionName are managed by ApplyVersionCode.cmake
        # at build time (updates both AndroidManifest.xml and version_code.cpp in sync).
        # Do NOT set QT_ANDROID_VERSION_CODE here — it freezes at configure time
        # and diverges from version_code.cpp on rebuilds without reconfigure.
    )
    # Add OpenSSL libraries for HTTPS support
    add_android_openssl_libraries(Decenza)
endif()

# iOS deployment
if(IOS)
    # Bundle ID and team can be overridden for local dev builds:
    #   cmake ... -DIOS_BUNDLE_ID=com.yourname.decenza -DIOS_DEVELOPMENT_TEAM=YOURTEAMID
    if(NOT IOS_BUNDLE_ID)
        set(IOS_BUNDLE_ID "io.github.kulitorum.decenza")
    endif()
    set_target_properties(Decenza PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/ios/Info.plist"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${CMAKE_PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${CURRENT_VERSION_CODE}"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${IOS_BUNDLE_ID}"
        XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"
        XCODE_ATTRIBUTE_MARKETING_VERSION "${CMAKE_PROJECT_VERSION}"
        XCODE_ATTRIBUTE_CURRENT_PROJECT_VERSION "${CURRENT_VERSION_CODE}"
        XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT "dwarf-with-dsym"
        XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT[variant=Release] "dwarf-with-dsym"
        XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT[variant=Debug] "dwarf"
        XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT[variant=MinSizeRel] "dwarf-with-dsym"
        XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT[variant=RelWithDebInfo] "dwarf-with-dsym"
        XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS "${CMAKE_CURRENT_SOURCE_DIR}/ios/Decenza.entitlements"
    )
    # Only override Xcode's signing team when explicitly provided
    if(IOS_DEVELOPMENT_TEAM)
        set_target_properties(Decenza PROPERTIES
            XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${IOS_DEVELOPMENT_TEAM}"
        )
    endif()
    # Add iOS asset catalog for app icon
    target_sources(Decenza PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/ios/Assets.xcassets"
    )
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/ios/Assets.xcassets"
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources
    )
    # Add privacy manifest (required for App Store since spring 2024)
    target_sources(Decenza PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/ios/PrivacyInfo.xcprivacy"
    )
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/ios/PrivacyInfo.xcprivacy"
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources
    )
    # Link iOS frameworks required for multimedia (AVFoundation backend)
    target_link_libraries(Decenza PRIVATE
        "-framework AVFoundation"
        "-framework CoreMedia"
        "-framework CoreVideo"
        "-framework CoreAudio"
        "-framework AudioToolbox"
    )
    # Link CoreBluetooth for native scale BLE transport
    target_link_libraries(Decenza PRIVATE
        "-framework CoreBluetooth"
    )
    # Link Security.framework for self-signed TLS certificate generation
    target_link_libraries(Decenza PRIVATE
        "-framework Security"
    )
    # SFSafariViewController for opening URLs without universal-link interception
    target_link_libraries(Decenza PRIVATE
        "-framework SafariServices"
    )
    # NOTE: the app target does NOT link WidgetKit. The widget-snapshot
    # bridge only writes App Group UserDefaults; WidgetKit (WidgetCenter) is
    # Swift-only and unreachable from the modules-disabled Obj-C++ build.
    # The widget extension target links WidgetKit itself (see
    # ios/inject_widget_extension.rb).
endif()

# macOS deployment
if(APPLE AND NOT IOS)
    # Link CoreBluetooth for native scale BLE transport on macOS
    target_link_libraries(Decenza PRIVATE
        "-framework CoreBluetooth"
    )
    # Bundle ID, overridable for local dev builds — mirrors IOS_BUNDLE_ID above:
    #   cmake ... -DMACOS_BUNDLE_ID=io.github.kulitorum.decenza.jefftest
    #
    # Why you would want to: macOS keys the Local Network privacy grant to the
    # app's identity, and a dev build that shares the shipping bundle id also
    # shares (and fights over) that one grant. On macOS 15+ the permission prompt
    # frequently never appears, and there is no way to reset the grant —
    # `tccutil` does not manage Local Network — so a wedged dev build ends up
    # needing a reboot. Giving dev builds their own id makes them a separate
    # subject, which re-triggers the prompt and leaves the shipping app's grant
    # untouched.
    #
    # Safe for user data: settings and app storage are addressed by
    # organization/application name ("DecentEspresso"/"Decenza", see
    # appsettings.cpp) and QStandardPaths, never by bundle id, so changing this
    # does not orphan an existing install's shots, profiles or preferences.
    # A CACHE variable rather than the plain `if(NOT ...)` form used for
    # IOS_BUNDLE_ID: this one needs to be settable on an EXISTING build
    # directory. Qt Creator's "Initial Configuration" is only applied to a fresh
    # cache, so a plain reconfigure silently ignores anything added there — which
    # is exactly how a first attempt at this failed. As a cache entry it appears
    # in Qt Creator's CMake settings and can be edited in place.
    set(MACOS_BUNDLE_ID "io.github.kulitorum.decenza" CACHE STRING
        "macOS bundle identifier. Override for local dev builds to get a separate Local Network permission subject.")
    set_target_properties(Decenza PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/macos/Info.plist"
        MACOSX_BUNDLE_GUI_IDENTIFIER "${MACOS_BUNDLE_ID}"
        MACOSX_BUNDLE_ICON_FILE "AppIcon.icns"
    )
    # Add macOS app icon to bundle
    set(MACOS_ICON "${CMAKE_CURRENT_SOURCE_DIR}/macos/AppIcon.icns")
    target_sources(Decenza PRIVATE ${MACOS_ICON})
    set_source_files_properties(${MACOS_ICON} PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources
    )
    # Force macOS to re-read the app icon from the bundle after build.
    # Without this, Launch Services caches the old icon indefinitely.
    add_custom_command(TARGET Decenza POST_BUILD
        COMMAND touch "$<TARGET_BUNDLE_DIR:Decenza>"
        COMMAND /System/Library/Frameworks/CoreServices.framework/Versions/A/Frameworks/LaunchServices.framework/Versions/A/Support/lsregister -f "$<TARGET_BUNDLE_DIR:Decenza>"
        COMMENT "Re-registering app bundle with Launch Services (icon cache refresh)"
    )

    # Optional stable code-signing identity for LOCAL macOS dev builds.
    #
    # Empty by default → the linker's ad-hoc signature stands, which is the
    # existing behavior; CI (which signs release builds itself on tag push) and
    # every other developer are completely unaffected.
    #
    # Why it exists: an ad-hoc / linker-signed bundle has NO stable code
    # identity, so macOS falls back to identifying it by its CDHash — a hash of
    # the binary that changes on every rebuild. TCC (Camera, Bluetooth, and in
    # particular Local Network) keys its per-app grant to that identity, so each
    # rebuild reads as a brand-new app: the previous Local Network approval no
    # longer applies, outbound LAN connections are silently dropped (a WiFi
    # scale fails with "Host unreachable" while the internet still works), and a
    # fresh, stale "Decenza" row piles up in System Settings that no UI can
    # remove. On this project's dev cadence that means a permission re-approval —
    # in practice a full reboot, since toggling the Local Network row does not
    # reliably re-arm the grant — after every single rebuild.
    #
    # Re-signing the finished bundle with a stable keychain identity gives it a
    # fixed designated requirement (the bundle id plus the signing cert), which
    # does not change between rebuilds. macOS then recognizes each rebuild as the
    # same app and the Local Network grant sticks. One-time setup on the
    # developer's machine:
    #
    #   1. Get a code-signing identity. If you already sign iOS builds you
    #      HAVE one — reuse it, no new cert needed:
    #        security find-identity -v -p codesigning
    #      An "Apple Development: …" identity works for macOS too and carries a
    #      real Team ID, so its designated requirement is maximally stable. With
    #      no Apple identity, make a self-signed one instead: Keychain Access →
    #      Certificate Assistant → Create a Certificate… (Self Signed Root,
    #      Certificate Type: Code Signing).
    #   2. Add to the Qt Creator kit's Initial CMake parameters (once). The
    #      value is the identity's SHA-1 from step 1 (robust — no spaces/parens
    #      to quote) or any unique substring of its name:
    #        -DDECENZA_MACOS_CODESIGN_IDENTITY=DFA23C5D…the SHA-1…
    #
    # CAVEAT, learned 2026-07-28: a stable signature is necessary but may not be
    # sufficient. Local Network identity also involves the UUID of the main
    # executable, which the linker regenerates on every relink, and on macOS 15+
    # the permission prompt often never appears at all — so a rebuild can still
    # read as an unapproved app and have its LAN traffic dropped silently
    # (instant EHOSTUNREACH, no local port bound, while the internet works).
    # Apple confirms the immediate-failure behaviour is by design and that there
    # is no way to reset or force the prompt. When that happens, MACOS_BUNDLE_ID
    # above is the lever that does not require a reboot: a different bundle id is
    # unambiguously a different subject, so the prompt fires again.
    #
    # Non-deep on purpose: only the top-level bundle is re-signed, which is what
    # carries the identity TCC keys on. The linker's ad-hoc signatures on the
    # nested Qt frameworks are left intact — re-signing them adds launch-failure
    # risk for no TCC benefit. If a future macOS ever keys Local Network on the
    # full nested signature, adding --deep is the escalation.
    set(DECENZA_MACOS_CODESIGN_IDENTITY "" CACHE STRING
        "Self-signed keychain identity to re-sign the macOS dev build with for a stable TCC/Local Network grant (empty = ad-hoc, the default)")
    if(DECENZA_MACOS_CODESIGN_IDENTITY)
        add_custom_command(TARGET Decenza POST_BUILD
            COMMAND codesign --force
                    --sign "${DECENZA_MACOS_CODESIGN_IDENTITY}"
                    "$<TARGET_BUNDLE_DIR:Decenza>"
            COMMENT "Re-signing with stable dev identity '${DECENZA_MACOS_CODESIGN_IDENTITY}' — stable identity for the TCC/Local Network grant (see CAVEAT in CMakeLists.txt)"
            VERBATIM
        )
    endif()
endif()

# Linux: executable output collides with QML module directory (both named "Decenza")
# when building in-tree with Ninja. Redirect executable to bin/ subdirectory.
if(UNIX AND NOT APPLE AND NOT ANDROID)
    set_target_properties(Decenza PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
endif()

# Windows installer - generate version.iss for Inno Setup
if(WIN32)
    configure_file(
        "${CMAKE_SOURCE_DIR}/installer/version.iss.in"
        "${CMAKE_SOURCE_DIR}/installer/version.iss"
        @ONLY
    )
endif()

# Unit tests (separate executables, not linked to main app)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Developer tools (desktop only — CLI executables not deployable on iOS/Android)
if(NOT ANDROID AND NOT IOS)
    add_executable(profile_sync
        tools/profile_sync.cpp
        src/profile/profile.cpp
        src/profile/de1apptclfields.cpp
        src/profile/profileframe.cpp
        src/profile/recipegenerator.cpp
        src/profile/recipeanalyzer.cpp
        src/profile/recipeparams.cpp
        src/ble/protocol/binarycodec.cpp
    )
    target_link_libraries(profile_sync PRIVATE Qt6::Core Qt6::Sql Qt6::Network Qt6::Gui Qt6::Bluetooth)
    target_include_directories(profile_sync PRIVATE ${CMAKE_SOURCE_DIR}/src)

    # shot_eval: offline evaluation harness for ShotAnalysis. Links the real
    # production sources (shotanalysis.cpp + conductance.cpp) so results
    # match live-shot behavior exactly. Usage:
    #   curl https://visualizer.coffee/api/shots/<uuid>/download > shot.json
    #   ./shot_eval shot.json [more.json ...]
    # The AI KB resource (:/ai/profile_knowledge.json) is embedded below via the
    # target-scoped qt_add_resources signature (NOT the classic variable form
    # over resources/ai.qrc). Both forms map the files to the same :/ai/...
    # paths loadProfileKnowledge reads, but the classic form would derive the
    # generated source name from the .qrc basename — colliding with the
    # Decenza target's own qrc_ai.cpp (same directory scope). One custom
    # command attached to two unrelated targets is rejected by the macOS CI
    # Xcode "new build system" generator; the local/Linux/Windows/iOS/Android
    # Ninja generator did not flag it. The target-scoped form emits a
    # uniquely-named source bound to shot_eval only, so there is no shared
    # custom command.
    add_executable(shot_eval
        tools/shot_eval/main.cpp
        src/ai/shotanalysis.cpp
        src/ai/conductance.cpp
        # Lean KB-resolution cluster (split out of the monolith). shot_eval
        # resolves analysisFlags + expertBand via the SAME ShotSummarizer
        # functions prepareAnalysisInputs calls — identical inputs FOR THE
        # CURRENT CORPUS (title-matches a KB key directly, no profile JSON)
        # WITHOUT dragging Profile (→ Qt Bluetooth via profileframe) into a
        # curve-eval CLI. NOT a general app-parity guarantee: editorType is
        # empty here vs the app's real value, so the editor-type fallback
        # path is not exercised — see the scoped note in main.cpp's
        # "App parity (scoped)" block.
        src/ai/shotsummarizer_kb.cpp
    )
    # Qt6::Sql is linked for the QSqlDatabase include pulled in via
    # history/shothistorystorage.h (where HistoryPhaseMarker lives). The
    # tool itself doesn't open any databases.
    target_link_libraries(shot_eval PRIVATE Qt6::Core Qt6::Sql)
    target_include_directories(shot_eval PRIVATE ${CMAKE_SOURCE_DIR}/src)
    # Target-scoped resource: BASE=resources keeps the files at :/ai/<name>
    # (identical to resources/ai.qrc's prefix="/" mapping), and the unique
    # resource name "shot_eval_ai" produces a shot_eval-only generated source
    # instead of the shared qrc_ai.cpp. NOTE: this list is maintained
    # independently of resources/ai.qrc (still consumed by the Decenza target
    # above) — there is no enforced coupling, so adding a KB file requires
    # editing BOTH this list and resources/ai.qrc or shot_eval silently lacks
    # the new resource.
    qt_add_resources(shot_eval "shot_eval_ai"
        PREFIX "/"
        BASE "${CMAKE_SOURCE_DIR}/resources"
        FILES
            "${CMAKE_SOURCE_DIR}/resources/ai/profile_knowledge.json"
            "${CMAKE_SOURCE_DIR}/resources/ai/espresso_dial_in_reference.md"
            "${CMAKE_SOURCE_DIR}/resources/ai/claude_agent.md"
    )

    # saw_replay: Phase 0 simulator for SAW prediction model proposals.
    # Replays a corpus of historical shots through OLD (weighted-avg) and
    # candidate replacement models (linear, MAD-trimmed, LOWESS) and
    # reports per-bucket MAE. The math is a stand-alone port — not linked
    # against settings.cpp — so the simulator builds without QSettings,
    # BLE, QML, or any of the live app's I/O surface. See
    # openspec/changes/ for current SAW proposals.
    add_executable(saw_replay tools/saw_replay/main.cpp)
    target_link_libraries(saw_replay PRIVATE Qt6::Core)

    # saw_parity: validates saw_replay's standalone math port against the
    # real production code path in src/core/settings.cpp. Links the full
    # Settings translation unit (and its 11 settings_* sub-domain TUs).
    # A passing run lets us trust simulator-driven sweeps as predictive of
    # what production would do under the same change.
    add_executable(saw_parity
        tools/saw_parity/main.cpp
        src/core/appsettings.cpp
        src/core/settings.cpp
        src/core/settings_mqtt.cpp
        src/core/settings_autowake.cpp
        src/core/settings_hardware.cpp
        src/core/settings_ai.cpp
        src/core/emojiassets.cpp
        src/core/markdownrenderer.cpp
        src/core/settings_theme.cpp
        src/core/backgroundpresets.cpp
        src/core/settings_visualizer.cpp
        src/core/settings_mcp.cpp
        src/core/settings_brew.cpp
        src/core/settings_dye.cpp
        src/history/coffeebagstorage.cpp
        src/history/equipmentstorage.cpp
        src/core/settings_network.cpp
        src/core/settings_app.cpp
        src/core/settings_calibration.cpp
        src/core/settings_graph.cpp
        src/ble/scales/scaletypeids.cpp
    )
    target_link_libraries(saw_parity PRIVATE Qt6::Core Qt6::Sql Qt6::Network Qt6::Gui Qt6::Bluetooth)
    target_include_directories(saw_parity PRIVATE ${CMAKE_SOURCE_DIR}/src)

    # PCH for the dev tools.
    #
    # saw_parity is worth this on its own: 22 TUs and 139 s of compile (that 139
    # is contended wall clock from a parallel build, not isolated cost), the
    # SECOND most expensive target in the whole tree after decenza_testlib. It is
    # 20 production sources plus its own main.cpp and moc — the same shape that
    # measured ~0.5 s saved per TU, so expect it to recover on the order of 10 s
    # for one ~2 s PCH build.
    #
    # An earlier version of this comment said 35-40 s, reached by applying the
    # 28% figure to the 139 s total. That is the wrong model and contradicts the
    # per-TU one stated at the Decenza PCH block above: the saving is roughly
    # CONSTANT per TU regardless of file size, so 22 TUs buys ~11 s, not a
    # percentage of a contended total. Both numbers cannot be right, and the
    # per-TU one is the measured one.
    #
    # Each tool gets its OWN PCH rather than reusing one, because they do not
    # share a flag set: saw_parity and profile_sync link Sql/Network/Gui/
    # Bluetooth, shot_eval links only Core, and none of them define the
    # DECENZA_TESTING / DECENZA_SIMULATOR pair that the tests/ targets carry.
    # REUSE_FROM across those would fail on the macro mismatch, which is
    # precisely the check that makes reuse safe inside tests/ and unsafe here.
    #
    # saw_replay is deliberately absent: ONE TU (a single main.cpp, no Q_OBJECT
    # so AUTOMOC adds nothing), so a ~2 s PCH build to save ~0.5 s is a plain
    # loss. The bar for a private PCH is roughly 4 TUs; below that, reuse or
    # nothing.
    if(DECENZA_ENABLE_PCH)
        target_precompile_headers(saw_parity PRIVATE
            <QtCore/qobject.h>
            <QtCore/qstring.h>
            <QtCore/qvariant.h>
            <QtCore/qdatetime.h>
            <QtCore/qdebug.h>
            <QtCore/qsettings.h>
        )
        target_precompile_headers(profile_sync PRIVATE
            <QtCore/qobject.h>
            <QtCore/qstring.h>
            <QtCore/qvariant.h>
            <QtCore/qjsonobject.h>
            <QtCore/qjsonarray.h>
            <QtCore/qjsondocument.h>
        )
        target_precompile_headers(shot_eval PRIVATE
            <QtCore/qobject.h>
            <QtCore/qstring.h>
            <QtCore/qvariant.h>
            <QtCore/qjsonobject.h>
            <QtCore/qjsondocument.h>
        )
    endif()
endif()
