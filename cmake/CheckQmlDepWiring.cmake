# Exercises a QML module's cache wiring (native app or Wasm client) instead of inspecting it.
#
# 1. Touching Theme.qml must dirty every cachegen edge. A drifted Qt path formula
#    already fails configure (add_custom_command(APPEND) on an unknown OUTPUT is an
#    error with CMake 3.30.5), so this covers the other half: that the accepted
#    dependencies actually reach the generator.
# 2. After regenerating those outputs, no QML cache object may recompile: a touch
#    changes no generated code, so decenza_use_stable_qmlcachegen() must have left
#    every output's timestamp alone for Ninja's restat to prune.
#
# Run:  cmake --build <builddir> --target qml_dep_wiring_check

cmake_minimum_required(VERSION 3.16)

foreach(var QML_DIR BUILD_DIR MODULE_TARGET)
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

# The target passes the build's own CMAKE_MAKE_PROGRAM; Qt's Ninja is often not on PATH.
if(DEFINED MAKE_PROGRAM AND EXISTS "${MAKE_PROGRAM}")
    set(NINJA_EXE "${MAKE_PROGRAM}")
else()
    find_program(NINJA_EXE NAMES ninja samu)
endif()
if(NOT NINJA_EXE)
    message(FATAL_ERROR "CheckQmlDepWiring: needs ninja; only the Ninja generator is supported.")
endif()

# Real builds, not `ninja -n`: CMake's glob verification is an always-dirty phony
# edge, so a dry run stops at "Re-running CMake" and never shows the real work.
file(STRINGS "${BUILD_DIR}/build.ninja" unit_lines
    REGEX "^build [^ :|]*\\.rcc/qmlcache/${MODULE_TARGET}_[^ :|]+_(qml|js|mjs)\\.cpp[ :]")
file(STRINGS "${BUILD_DIR}/build.ninja" object_lines
    REGEX "^build [^ :|]*CMakeFiles/${MODULE_TARGET}\\.dir/[^ :|]*\\.rcc/qmlcache/[^ :|]+\\.cpp\\.o[ :]")
foreach(kind unit object)
    set(${kind}s "")
    foreach(line IN LISTS ${kind}_lines)
        string(REGEX MATCH "^build ([^ :|]+)" _ "${line}")
        list(APPEND ${kind}s "${CMAKE_MATCH_1}")
    endforeach()
endforeach()
list(LENGTH units unit_count)
if(NOT DEFINED EXPECTED_UNITS)
    set(EXPECTED_UNITS ${unit_count})
endif()
if(unit_count EQUAL 0 OR unit_count LESS EXPECTED_UNITS)
    message(FATAL_ERROR "CheckQmlDepWiring: ${BUILD_DIR}/build.ninja lists ${unit_count} "
        "${MODULE_TARGET} QML units, expected ${EXPECTED_UNITS}.")
endif()

function(_build out_var)
    execute_process(
        COMMAND ${NINJA_EXE} ${ARGN}
        WORKING_DIRECTORY "${BUILD_DIR}"
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err
        RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "CheckQmlDepWiring: ninja failed:\n${out}\n${err}")
    endif()
    set(${out_var} "${out}" PARENT_SCOPE)
endfunction()

_build(settle ${units} ${objects})
file(TOUCH_NOCREATE "${probe}")

# Both suffixes: .js sources compile to `_js.cpp` units.
_build(regen ${units})
string(REGEX MATCHALL "Generating [^ ,\n]*\\.rcc/qmlcache/${MODULE_TARGET}_[^,\n]*_(qml|js|mjs)\\.cpp" hits "${regen}")
list(LENGTH hits hit_count)
if(hit_count LESS EXPECTED_UNITS)
    message(FATAL_ERROR
        "CheckQmlDepWiring: FAILED.\n"
        "  Touching ${probe} made ninja regenerate ${hit_count} QML units, "
        "expected ${EXPECTED_UNITS}.\n"
        "  The add_custom_command(APPEND DEPENDS) wiring in CMakeLists.txt does not reach "
        "Qt's cachegen commands; check the output-path helper against Qt6QmlMacros.cmake.\n"
        "  Until this passes, a cross-file QML edit leaves stale units behind.")
endif()

_build(compile ${objects})
string(REGEX MATCHALL "Building CXX object [^\n]*\\.rcc/qmlcache/[^\n]*\\.cpp\\.o" compiled "${compile}")
list(LENGTH compiled object_count)
if(object_count GREATER 0)
    message(FATAL_ERROR
        "CheckQmlDepWiring: FAILED.\n"
        "  Regenerating units for an unchanged Theme.qml recompiled ${object_count} QML cache objects.\n"
        "  qmlcachegen rewrote identical outputs, so restat could not prune them: check that "
        "decenza_use_stable_qmlcachegen() still sets QT_QMLCACHEGEN_EXECUTABLE on the module.")
endif()

message(STATUS
    "QML dep wiring OK (${MODULE_TARGET}) - touching Theme.qml dirties ${hit_count} of "
    "${EXPECTED_UNITS} units, and regenerating them recompiles no QML object.")
