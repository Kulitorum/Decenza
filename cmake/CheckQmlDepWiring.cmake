# Proves the cross-file QML cache dependency wiring in CMakeLists.txt is actually
# attached, by making the build system refuse rather than by reading a count.
#
# WHY THIS EXISTS — and NOT for the reason first claimed here.
#
# The wiring uses `add_custom_command(OUTPUT ... APPEND DEPENDS ...)` to add inputs to
# custom commands that QT created. This file originally justified itself by asserting
# that an APPEND against a non-matching OUTPUT is a SILENT no-op. **That is false.**
# Reproduced with this project's own CMake 3.30.5 + Ninja:
#
#     CMake Error at CMakeLists.txt:12 (add_custom_command):
#       Attempt to APPEND to custom command with output ... which is not already a
#       custom command output.
#     CMake Generate step failed.  Build files cannot be regenerated correctly.
#
# So a drift in Qt's path formula (Qt6QmlMacros.cmake:3841-3847) fails the build loudly
# at configure time. It cannot silently detach. The original rationale was written from
# belief rather than measurement, which is the very habit the rest of this file preaches
# against; it is recorded here rather than quietly deleted.
#
# What is left for this check is the half CMake does NOT cover: that the dependencies,
# having been accepted, actually reach the GENERATOR and make it rebuild. CMake accepting
# an APPEND says the path matched; only asking ninja what it would rebuild says the edge
# has effect. So this does not inspect the wiring, it exercises it — touch a file every
# other QML file resolves types through, and require the whole set to go dirty.
#
# Side effect worth knowing: this leaves Theme.qml's mtime bumped, so the next real build
# regenerates every QML unit. That is the cost of exercising rather than inspecting.
#
# Run:  cmake --build <builddir> --target qml_dep_wiring_check

cmake_minimum_required(VERSION 3.16)

foreach(var QML_DIR BUILD_DIR EXPECTED_UNITS)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "CheckQmlDepWiring: ${var} not set")
    endif()
endforeach()

set(probe "${QML_DIR}/Theme.qml")
if(NOT EXISTS "${probe}")
    message(FATAL_ERROR
        "CheckQmlDepWiring: probe file ${probe} is gone. Point this at another "
        "singleton every other QML file reads, or the check proves nothing.")
endif()

find_program(NINJA_EXE NAMES ninja samu)
if(NOT NINJA_EXE)
    message(FATAL_ERROR
        "CheckQmlDepWiring: needs ninja to ask what is dirty. This check only "
        "supports the Ninja generator.")
endif()

# Touch the probe, then ask ninja what it WOULD build. -n does not run anything,
# so this leaves the tree exactly as it found it apart from one mtime.
file(TOUCH_NOCREATE "${probe}")

execute_process(
    COMMAND ${NINJA_EXE} -n
    WORKING_DIRECTORY "${BUILD_DIR}"
    OUTPUT_VARIABLE dry_run
    ERROR_VARIABLE dry_run_err
    RESULT_VARIABLE dry_run_rc
)
if(NOT dry_run_rc EQUAL 0)
    message(FATAL_ERROR "CheckQmlDepWiring: ninja -n failed:\n${dry_run_err}")
endif()

# Count the qmlcachegen edges ninja intends to re-run.
#
# BOTH suffixes. QML_FILES is 221 entries but only 214 are .qml — the other 7 are .js,
# whose generated units end `_js.cpp`. Matching only `_qml.cpp` made hit_count top out at
# 214 against an EXPECTED_UNITS of 221, so this check could never pass and had in fact
# never been run green. A gate written to make the build refuse, that refused
# unconditionally, is worse than no gate: it teaches people to skip it.
string(REGEX MATCHALL "Generating \\.rcc/qmlcache/[^,\n]*_(qml|js)\\.cpp" hits "${dry_run}")
list(LENGTH hits hit_count)

if(hit_count LESS EXPECTED_UNITS)
    message(FATAL_ERROR
        "CheckQmlDepWiring: FAILED.\n"
        "  Touching ${probe} made ninja want to regenerate ${hit_count} QML units, "
        "expected ${EXPECTED_UNITS}.\n"
        "  The add_custom_command(APPEND DEPENDS) wiring in CMakeLists.txt is not "
        "attached to Qt's cachegen commands — almost certainly because Qt's output "
        "path formula changed. Re-read Qt6QmlMacros.cmake (search for "
        "'INTEGRITY_SYMBOL_UNIQUENESS', the compiled_file is built just above it) "
        "and update the formula in CMakeLists.txt to match.\n"
        "  Until this passes, a cross-file QML edit produces a MIXED cache: the app "
        "compiles and runs but binds against stale type information.")
endif()

message(STATUS
    "QML dep wiring OK — touching Theme.qml dirties ${hit_count} of ${EXPECTED_UNITS} units.")
