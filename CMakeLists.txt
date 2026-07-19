cmake_minimum_required(VERSION 3.21)
project(Decenza VERSION 2.0.0 LANGUAGES CXX)

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
# The pre-merge CI job builds with this ON; off by default so routine local
# builds are uninstrumented. Not supported on MSVC (no UBSan there).
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
    # Qt 6.11.1 was built for iOS 17.0
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

# mjansson/mdns: header-only mDNS library for resolving .local hostnames on Android
# (Android's getaddrinfo doesn't reliably handle mDNS)
if(ANDROID)
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
if(ENABLE_ASAN OR (NOT CMAKE_CONFIGURATION_TYPES AND CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT ANDROID AND NOT APPLE))
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
    endif()
endif()

# Undefined Behavior Sanitizer - opt-in only (-DENABLE_UBSAN=ON), never
# auto-enabled, so a routine local build carries no instrumentation. The
# pre-merge CI job turns it on to run the test suite instrumented.
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
# Note the existing ASan condition above excludes APPLE, which meant that on
# macOS — the platform this is developed on — a Debug build carried NO
# sanitizer at all. UBSan is verified working on macOS (the full 83-test suite
# passes instrumented), so there is no reason to exclude Apple here.
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
# here; flags that fired mostly on idiom are not, and are listed at the bottom
# with the counts, so nobody re-proposes them without new evidence.
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
    # Chosen by MEASUREMENT, not taste — every first-party TU was compiled with
    # a wide candidate set and the hits counted (scripts/measure_warnings.py).
    #
    #   -Wshorten-64-to-32   30 sites. CLAUDE.md already documents this hazard
    #                        (qsizetype -> int truncates on 64-bit) and the
    #                        convention did not hold on its own.
    #   -Wshadow              8 sites, two of which hid genuine confusion
    #                        between an inner and outer variable of different
    #                        types.
    #   the rest             scored ZERO across 568 TUs: free today, and a
    #                        first occurrence now fails the build rather than
    #                        drifting in unnoticed.
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

    # MEASURED AND DELIBERATELY NOT ENABLED. Re-proposing any of these needs
    # evidence that the instances are defects, not that the flag exists:
    #
    #   -Wrange-loop-bind-reference  147 hits / 50 files. Almost entirely
    #       `for (const QJsonValue& v : array)` — Qt 6 returns a proxy from
    #       QJsonArray's iterator, so the reference binds to a temporary. A
    #       per-iteration copy, not a bug; 147 edits would buy no correctness.
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
    src/core/settings.cpp
    src/core/settings_mqtt.cpp
    src/core/settings_autowake.cpp
    src/core/settings_hardware.cpp
    src/core/settings_ai.cpp
    src/core/emojiassets.cpp
    src/core/markdownrenderer.cpp
    src/core/settings_theme.cpp
    src/core/settings_visualizer.cpp
    src/core/settings_mcp.cpp
    src/core/settings_brew.cpp
    src/core/settings_dye.cpp
    src/core/settings_network.cpp
    src/core/settings_app.cpp
    src/core/settings_calibration.cpp
    src/core/widgetlibrary.cpp
    src/widget/machinestatussnapshot.cpp
    src/core/batterymanager.cpp
    src/core/memorymonitor.cpp
    src/core/accessibilitymanager.cpp
    src/core/autowakemanager.cpp
    src/core/crashhandler.cpp
    src/core/profilestorage.cpp
    src/core/translationmanager.cpp
    src/core/updatechecker.cpp
    src/core/firmwareassetcache.cpp
    src/ble/protocol/binarycodec.cpp
    src/ble/blecapability.cpp
    src/ble/blemanager.cpp
    src/ble/de1device.cpp
    src/ble/bletransport.cpp
    src/ble/scaledevice.cpp
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
    src/ble/refractometers/difluidr2.cpp
    src/ble/refractometers/difluidr1.cpp
    src/ble/refractometers/aes128.cpp
    src/ble/scales/eurekaprecisascale.cpp
    src/ble/scales/solobaristascale.cpp
    src/ble/scales/atomhearteclairscale.cpp
    src/ble/scales/variaakuscale.cpp
    src/ble/scales/timemorescale.cpp
    src/ble/scales/flowscale.cpp
    src/ble/transport/qtscalebletransport.cpp
    src/core/asynclogger.cpp
    src/core/btlogfilter.cpp
    src/machine/machinestate.cpp
    src/machine/weightprocessor.cpp
    src/machine/stepexitarbiter.cpp
    src/profile/profile.cpp
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
    src/controllers/shottimingcontroller.cpp
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

# Simulator engine - always included for multi-config generators (Xcode, Visual Studio)
# CMAKE_BUILD_TYPE is empty at configure time, so conditional inclusion won't work.
# The simulator code is small and has no extra dependencies, so it's safe to always link.
list(APPEND SOURCES
    src/simulator/de1simulator.cpp
    src/simulator/simulatedscale.cpp
)

# GHC simulator window - desktop only (uses QWidget window)
if(WIN32 OR APPLE)
    list(APPEND SOURCES
        src/simulator/ghcsimulator.cpp
    )
endif()

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
    src/core/settings.h
    src/core/settings_mqtt.h
    src/core/settings_autowake.h
    src/core/settings_hardware.h
    src/core/settings_ai.h
    src/core/settings_theme.h
    src/core/settings_visualizer.h
    src/core/settings_mcp.h
    src/core/settings_brew.h
    src/core/settings_dye.h
    src/core/settings_network.h
    src/core/settings_app.h
    src/core/settings_calibration.h
    src/core/grinderaliases.h
    src/core/basketaliases.h
    src/core/widgetlibrary.h
    src/core/batterymanager.h
    src/core/memorymonitor.h
    src/core/accessibilitymanager.h
    src/core/autowakemanager.h
    src/core/crashhandler.h
    src/core/profilestorage.h
    src/core/translationmanager.h
    src/core/updatechecker.h
    src/ble/protocol/binarycodec.h
    src/ble/protocol/de1characteristics.h
    src/ble/protocol/decentscaleprotocol.h
    src/ble/blecapability.h
    src/ble/blemanager.h
    src/ble/de1transport.h
    src/ble/de1device.h
    src/ble/bletransport.h
    src/ble/scaledevice.h
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
    src/ai/aiprovider.h
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
    src/history/shotprojection.h
    src/history/shotdebuglogger.h
    src/history/shotfileparser.h
    src/history/shotimporter.h
    src/history/shothistoryexporter.h
    src/models/shotcomparisonmodel.h
    src/models/flowcalibrationmodel.h
    src/network/shotserver.h
    src/network/mqttclient.h
    src/network/mdnsresolver.h
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
    src/core/settingsserializer.h
    src/core/datamigrationclient.h
    src/core/databasebackupmanager.h
    src/core/documentformatter.h
    src/weather/weathermanager.h
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
)

# GHC simulator window header - desktop only
if(WIN32 OR APPLE)
    list(APPEND HEADERS
        src/simulator/ghcsimulator.h
    )
endif()

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
)
if(ANDROID)
    target_include_directories(Decenza PRIVATE ${mdns_lib_SOURCE_DIR})
endif()
if(IOS)
    target_include_directories(Decenza PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ios)
endif()

# QML module - mark singletons
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(qml/components/SettingsTabs.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(qml/components/layout/WidgetColor.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(qml/components/DrinkType.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

# Define QML files list for dependency tracking
set(QML_FILES
    qml/main.qml
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
    qml/pages/DialingAssistantPage.qml
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
    qml/components/PresetPillRow.qml
    qml/components/ValueInput.qml
    qml/components/RatingInput.qml
    qml/components/TastePicker.qml
    qml/components/ShotGraph.qml
    qml/components/SteamGraph.qml
    qml/components/StatusBar.qml
    qml/components/RatioPresetDialog.qml
    qml/components/GrindPickerDialog.qml
    qml/components/StableWeightCapture.qml
    qml/components/ShotPlanText.qml
    qml/components/SteamPlanText.qml
    qml/components/ProfileGraph.qml
    qml/components/ProfilePreviewPopup.qml
    qml/components/ProfileInfoButton.qml
    qml/components/ProfileDisplayButton.qml
    qml/components/BottomBar.qml
    qml/components/ThemedIcon.qml
    qml/components/TranslationErrorToast.qml
    qml/components/AccessibleMouseArea.qml
    qml/components/AccessibleTapHandler.qml
    qml/components/AccessibleButton.qml
    qml/components/HideKeyboardButton.qml
    qml/components/ExpandableTextArea.qml
    qml/components/FocusIndicator.qml
    qml/components/ColorWheel.qml
    qml/components/ColorEditor.qml
    qml/components/ColorSwatch.qml
    qml/components/StyledTextField.qml
    qml/components/SuggestionField.qml
    qml/components/BeanBaseSearchBar.qml
    qml/components/BeanBaseDetailsPopup.qml
    qml/components/BeanBaseDetailsRow.qml
    qml/components/BeanSummary.qml
    qml/components/LinkBeanBaseDialog.qml
    qml/components/ChangeBeansDialog.qml
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
    qml/components/ProfileKnowledgeDialog.qml
    qml/components/BeanThumbnail.qml
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
    qml/components/CrashReportDialog.qml
    qml/components/McpConfirmDialog.qml
    qml/components/De1CommunicationErrorDialog.qml
    qml/components/ColoredIcon.qml
    qml/components/CrtOverlay.qml
    qml/components/CrtShaderEffect.qml
    qml/components/QrCode.qml
    qml/components/DateUtils.js
    qml/components/qrcode.js
    qml/components/SettingsSearchIndex.js
    qml/components/SettingsTabs.qml

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
    qml/components/layout/items/ShotPlanItem.qml
    qml/components/layout/items/SpacerItem.qml
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

# Simulator QML - desktop only (GHC window uses QWidget)
if(WIN32 OR APPLE)
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

qt_add_qml_module(Decenza
    URI Decenza
    VERSION 1.1
    QML_FILES ${QML_FILES}
    NO_GENERATE_EXTRA_QMLDIRS
)

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
    set_target_properties(Decenza PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/macos/Info.plist"
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
        src/core/settings.cpp
        src/core/settings_mqtt.cpp
        src/core/settings_autowake.cpp
        src/core/settings_hardware.cpp
        src/core/settings_ai.cpp
        src/core/emojiassets.cpp
        src/core/markdownrenderer.cpp
        src/core/settings_theme.cpp
        src/core/settings_visualizer.cpp
        src/core/settings_mcp.cpp
        src/core/settings_brew.cpp
        src/core/settings_dye.cpp
        src/history/coffeebagstorage.cpp
        src/history/equipmentstorage.cpp
        src/core/settings_network.cpp
        src/core/settings_app.cpp
        src/core/settings_calibration.cpp
        src/ble/scales/scaletypeids.cpp
    )
    target_link_libraries(saw_parity PRIVATE Qt6::Core Qt6::Sql Qt6::Network Qt6::Gui Qt6::Bluetooth)
    target_include_directories(saw_parity PRIVATE ${CMAKE_SOURCE_DIR}/src)
endif()
