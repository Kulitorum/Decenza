## ADDED Requirements

### Requirement: UndefinedBehaviorSanitizer Is Available as a Build Option
`CMakeLists.txt` SHALL provide an `ENABLE_UBSAN` option that instruments the build with UndefinedBehaviorSanitizer, mirroring the structure of the existing `ENABLE_ASAN` block.

UBSan is the highest-value sanitizer for this codebase because it detects a class of defect that neither the compiler nor the existing tests find: signed overflow, invalid shifts, misaligned loads, null-reference binding, and out-of-range enum and bool values. Unlike a warning flag, its coverage does not depend on third-party headers carrying annotations.

#### Scenario: Local instrumented build
- **WHEN** a developer configures with `-DENABLE_UBSAN=ON`
- **THEN** the build compiles and links with `-fsanitize=undefined` and the resulting binaries report undefined behaviour at runtime

#### Scenario: Existing ASan behaviour preserved
- **WHEN** a developer configures a Debug build on a non-Apple platform without naming either option
- **THEN** ASan is still auto-enabled exactly as before, and UBSan is off unless explicitly requested

#### Scenario: Multi-config generators
- **WHEN** CMake is configured with a multi-config generator (Visual Studio, Xcode) that does not set `CMAKE_BUILD_TYPE` at configure time
- **THEN** sanitizer flags are not leaked into Release builds, consistent with the existing ASan guard

### Requirement: The Test Suite Runs Under UndefinedBehaviorSanitizer in CI
The pre-merge workflow SHALL run the `ctest` suite with UBSan instrumentation enabled, and a sanitizer diagnostic SHALL fail the run.

#### Scenario: Test triggers undefined behaviour
- **WHEN** a test exercises code that performs undefined behaviour under UBSan
- **THEN** the run fails and the workflow log contains the UBSan diagnostic naming the source location

#### Scenario: Sanitizer output is not silently swallowed
- **WHEN** UBSan reports a diagnostic
- **THEN** the sanitizer is configured to make the run fail (for example `halt_on_error=1` or `UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1`) rather than printing a warning that a passing exit code hides

### Requirement: Sanitizer Findings Are Distinguishable From Test Failures
A CI failure SHALL make clear whether it was an assertion failure in a test or a sanitizer diagnostic, because the two require different responses.

#### Scenario: Reading a failed pre-merge run
- **WHEN** a maintainer opens a failed pre-merge workflow
- **THEN** the summary distinguishes a failing test assertion from a sanitizer report, without requiring the full log to be read to tell them apart

### Requirement: Existing ASan Configuration Is Exercised
The ASan configuration already present in `CMakeLists.txt` SHALL be run by CI on a defined cadence, rather than existing only as configuration nothing executes.

ASan is currently auto-enabled for local non-Apple Debug builds and is invoked by no workflow, so memory defects it would catch are found only if a developer happens to run a Debug build locally.

#### Scenario: ASan run occurs
- **WHEN** the defined cadence elapses (per-pull-request, nightly, or a documented alternative)
- **THEN** an ASan-instrumented run of the test suite executes and its result is visible without opening the workflow file to check whether it ran
