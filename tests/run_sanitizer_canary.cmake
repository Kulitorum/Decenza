# Drives a sanitizer canary and asserts the sanitizer actually caught it.
#
# Deliberately stricter than ctest's WILL_FAIL property, which passes on ANY
# non-zero exit — a canary that failed to link, or crashed for an unrelated
# reason, would satisfy WILL_FAIL and report the sanitizer as armed when it is
# not. This checks both halves of the evidence: the process must behave as the
# mode requires, AND it must have printed a diagnostic naming the defect.
#
# Invoked with:
#   -DCANARY=<path to the built canary executable>
#   -DSANITIZER=ubsan|asan
#   -DEXPECT_ABORT=ON|OFF   OFF only for UBSan's recovering mode, where the
#                           sanitizer reports and continues by design.

if(NOT DEFINED CANARY)
    message(FATAL_ERROR "run_sanitizer_canary.cmake requires -DCANARY=<path>")
endif()
if(NOT DEFINED SANITIZER)
    message(FATAL_ERROR "run_sanitizer_canary.cmake requires -DSANITIZER=ubsan|asan")
endif()
if(NOT DEFINED EXPECT_ABORT)
    set(EXPECT_ABORT ON)
endif()

# What each sanitizer's report looks like, and the env var that could hide it.
if(SANITIZER STREQUAL "ubsan")
    set(expected_text "runtime error")
    set(what "signed integer overflow")
    set(options_var "UBSAN_OPTIONS=halt_on_error=1")
    set(build_flags "-fsanitize=undefined and -fno-sanitize-recover=all")
    set(cmake_option "ENABLE_UBSAN")
elseif(SANITIZER STREQUAL "asan")
    set(expected_text "heap-buffer-overflow")
    set(what "heap read past the end of an allocation")
    set(options_var "ASAN_OPTIONS=halt_on_error=1")
    set(build_flags "-fsanitize=address")
    set(cmake_option "ENABLE_ASAN")
else()
    message(FATAL_ERROR "Unknown -DSANITIZER=${SANITIZER} (expected ubsan or asan)")
endif()

# Run with our OWN sanitizer options rather than inheriting the caller's. The
# question this test answers is "is the build instrumented?", and the answer
# must not change with the ambient environment. It did: a developer (or a
# workflow) running the suite with `log_path=...` set sends every diagnostic to
# a file instead of stderr, and the canary — which had aborted correctly — then
# looked like a sanitizer that killed the process without reporting anything.
# That false positive is worse than no canary, because it discredits a working
# gate. src/main.cpp sets exactly that log_path for the app, so this is the
# normal case here, not a hypothetical.
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "${options_var}" "${CANARY}"
    RESULT_VARIABLE exit_code
    OUTPUT_VARIABLE stdout_text
    ERROR_VARIABLE stderr_text
)

set(combined "${stdout_text}${stderr_text}")

# A malloc failure in the ASan canary means nothing was exercised; it must not
# read as either success or a sanitizer finding.
if(exit_code EQUAL 2 AND combined MATCHES "malloc failed")
    message(FATAL_ERROR
        "Sanitizer canary could not allocate; the check did not run at all.\n"
        "Canary output:\n${combined}")
endif()

if(EXPECT_ABORT AND exit_code EQUAL 0)
    message(FATAL_ERROR
        "Sanitizer canary exited 0: deliberate ${what} was NOT trapped.\n"
        "The ${SANITIZER} instrumentation is not in effect, so a green test "
        "suite is not evidence of a codebase free of these defects.\n"
        "Check that ${build_flags} reached the compile AND link lines "
        "(see the ${cmake_option} block in CMakeLists.txt).\n"
        "Canary output:\n${combined}")
endif()

# The diagnostic is the load-bearing half of the evidence, and it is the ONLY
# half in recovering mode — there the process is supposed to survive, so the
# exit code says nothing and this text is the whole proof.
if(NOT combined MATCHES "${expected_text}")
    if(EXPECT_ABORT)
        message(FATAL_ERROR
            "Sanitizer canary failed (exit ${exit_code}) but printed no "
            "${SANITIZER} diagnostic. It should report '${expected_text}'; a "
            "different failure means the canary itself is broken, not that the "
            "sanitizer is working.\n"
            "Canary output:\n${combined}")
    else()
        message(FATAL_ERROR
            "Sanitizer canary ran to completion without a ${SANITIZER} "
            "diagnostic. In recovering mode the process is expected to survive "
            "the deliberate ${what}, but it must still REPORT it — and nothing "
            "was reported, so the instrumentation is not in effect.\n"
            "Check that ${build_flags} reached the compile AND link lines "
            "(see the ${cmake_option} block in CMakeLists.txt).\n"
            "Canary output:\n${combined}")
    endif()
endif()

if(EXPECT_ABORT)
    message(STATUS "Sanitizer canary: ${SANITIZER} trapped the deliberate "
                   "${what} (exit ${exit_code}).")
else()
    message(STATUS "Sanitizer canary: ${SANITIZER} reported the deliberate "
                   "${what} and continued, as recovering mode requires.")
endif()
