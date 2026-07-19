# Drives sanitizer_canary and asserts the sanitizer actually caught it.
#
# Deliberately stricter than ctest's WILL_FAIL property, which passes on ANY
# non-zero exit — a canary that failed to link, or crashed for an unrelated
# reason, would satisfy WILL_FAIL and report the sanitizer as armed when it is
# not. This checks both halves of the evidence: the process must fail, AND it
# must have printed a UBSan diagnostic naming the overflow.
#
# Invoked with -DCANARY=<path to the built canary executable>.

if(NOT DEFINED CANARY)
    message(FATAL_ERROR "run_sanitizer_canary.cmake requires -DCANARY=<path>")
endif()

# Run with our OWN UBSAN_OPTIONS rather than inheriting the caller's. The
# question this test answers is "is the build instrumented?", and the answer
# must not change with the ambient environment. It did: a developer (or a
# workflow) running the suite with `log_path=...` set sends every diagnostic to
# a file instead of stderr, and the canary — which had aborted correctly — then
# looked like a sanitizer that killed the process without reporting anything.
# That false positive is worse than no canary, because it discredits a working
# gate. halt_on_error is belt-and-braces here; -fno-sanitize-recover=all in the
# build is what actually forces the abort.
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
            "UBSAN_OPTIONS=halt_on_error=1"
            "${CANARY}"
    RESULT_VARIABLE exit_code
    OUTPUT_VARIABLE stdout_text
    ERROR_VARIABLE stderr_text
)

set(combined "${stdout_text}${stderr_text}")

if(exit_code EQUAL 0)
    message(FATAL_ERROR
        "Sanitizer canary exited 0: deliberate signed overflow was NOT trapped.\n"
        "The UBSan instrumentation is not in effect, so a green test suite is "
        "not evidence of a codebase free of undefined behaviour.\n"
        "Check that -fsanitize=undefined and -fno-sanitize-recover=all reached "
        "the compile and link lines (see the ENABLE_UBSAN block in CMakeLists.txt).\n"
        "Canary output:\n${combined}")
endif()

if(NOT combined MATCHES "runtime error")
    message(FATAL_ERROR
        "Sanitizer canary failed (exit ${exit_code}) but printed no UBSan "
        "diagnostic. It should abort with a 'runtime error: signed integer "
        "overflow' message; a different failure means the canary itself is "
        "broken, not that the sanitizer is working.\n"
        "Canary output:\n${combined}")
endif()

message(STATUS "Sanitizer canary: UBSan trapped the deliberate overflow (exit ${exit_code}).")
