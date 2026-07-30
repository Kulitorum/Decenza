# Proves the cross-file QML cache dependency wiring in CMakeLists.txt is actually
# attached, by making the build system refuse rather than by reading a count.
#
# WHY THIS EXISTS. The wiring uses `add_custom_command(OUTPUT ... APPEND DEPENDS ...)`
# to add dependencies to custom commands that QT created. That only works if the
# OUTPUT path matches Qt's byte for byte; against anything else CMake silently
# ignores the APPEND. So a change to Qt's path formula (Qt6QmlMacros.cmake:3841-3847)
# would disable the whole mechanism while CMake still cheerfully printed
# "QML cross-file cache deps wired for 214 units" — a plausible number describing
# nothing. That is precisely the failure mode this repo keeps re-learning: the real
# defects all printed healthy numbers, and were caught only when a tool refused.
#
# So this does not inspect the wiring. It exercises it: touch a file that every
# other QML file resolves types through, ask the generator what is now dirty, and
# require that to be all of them.
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
string(REGEX MATCHALL "Generating \\.rcc/qmlcache/[^,\n]*_qml\\.cpp" hits "${dry_run}")
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
