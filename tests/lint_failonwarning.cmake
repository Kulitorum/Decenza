# Guard: every test executable must arm QTest::failOnWarning() so an unexpected
# qWarning/qCritical fails the test (see docs/CLAUDE_MD/TESTING.md). A new
# tst_*.cpp that forgets it would silently opt out of the discipline — this lint
# catches that. Run as a CTest entry (cmake -P), so it re-globs on every run and
# needs no reconfigure when a test file is added.
#
# Invoked with -DLINT_DIR=<tests source dir>.

file(GLOB test_files "${LINT_DIR}/tst_*.cpp")
set(offenders "")
foreach(f ${test_files})
    file(READ "${f}" content)
    # Only files that actually declare a test executable need the guard.
    if(content MATCHES "QTEST_[A-Z_]*MAIN" AND NOT content MATCHES "failOnWarning")
        get_filename_component(name "${f}" NAME)
        list(APPEND offenders "${name}")
    endif()
endforeach()

if(offenders)
    string(REPLACE ";" "\n  " offlist "${offenders}")
    message(FATAL_ERROR
        "These test files declare a QTEST_*MAIN executable but do not call "
        "QTest::failOnWarning():\n  ${offlist}\n"
        "Add `void init() { QTest::failOnWarning(); }` to the test class (or "
        "prepend the call to an existing init()). See the Handling Warnings "
        "section of docs/CLAUDE_MD/TESTING.md.")
endif()

message(STATUS "failOnWarning lint: all test executables guarded")
