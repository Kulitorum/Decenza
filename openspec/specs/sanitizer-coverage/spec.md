# sanitizer-coverage Specification

## Purpose
Governs the runtime instrumentation the project relies on to find defects the compiler and the ordinary test suite cannot: UndefinedBehaviorSanitizer and AddressSanitizer, the nightly workflow that runs the suite under each, and the check selection that keeps them credible. Two themes run through it. First, an instrumented run must prove it is armed — a canary commits deliberate undefined behaviour, because a sanitizer silently not applied produces exactly the same green suite as a clean codebase. Second, a finding must fail by construction (`-fno-sanitize-recover=all`) rather than by an environment variable a workflow edit can drop.

## Requirements
### Requirement: UndefinedBehaviorSanitizer Is Available as a Build Option

`CMakeLists.txt` SHALL provide an `ENABLE_UBSAN` option that instruments the
build with UndefinedBehaviorSanitizer, mirroring the structure of the existing
`ENABLE_ASAN` block.

#### Scenario: Local instrumented build
- **WHEN** a developer configures with `-DENABLE_UBSAN=ON`
- **THEN** the build compiles and links with `-fsanitize=undefined` and the resulting binaries report undefined behaviour at runtime

#### Scenario: Existing ASan behaviour preserved
- **WHEN** a developer configures a Debug build on a non-Apple platform without naming either option
- **THEN** ASan is still auto-enabled exactly as before, and UBSan is off unless explicitly requested

#### Scenario: Multi-config generators
- **WHEN** CMake is configured with a multi-config generator (Visual Studio, Xcode) that does not set `CMAKE_BUILD_TYPE` at configure time
- **THEN** sanitizer flags are not leaked into Release builds, consistent with the existing ASan guard

#### Scenario: UBSan reports defects the other checks miss

- **WHEN** the instrumented suite executes signed overflow, an invalid shift, a misaligned load, a null-reference binding or an out-of-range enum or bool value
- **THEN** the run reports the undefined behaviour at runtime


### Requirement: The Test Suite Runs Under Sanitizers in CI
A scheduled (nightly) workflow SHALL run the `ctest` suite with UBSan instrumentation enabled, and separately with ASan, as two independent builds. A sanitizer diagnostic SHALL fail the run.

The two are separate jobs because their object files never hash-match and so cannot share a compiler cache, and because a finding from one says nothing about the other — cancelling the survivor would discard half the night's signal.

#### Scenario: Test triggers undefined behaviour
- **WHEN** a test exercises code that performs undefined behaviour under UBSan
- **THEN** the run fails and the workflow log contains the UBSan diagnostic naming the source location

#### Scenario: Sanitizer output is not silently swallowed
- **WHEN** UBSan reports a diagnostic
- **THEN** the sanitizer is configured to make the run fail (for example `halt_on_error=1` or `UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1`) rather than printing a warning that a passing exit code hides

### Requirement: Sanitizer Findings Are Distinguishable From Test Failures
A CI failure SHALL make clear whether it was an assertion failure in a test or a sanitizer diagnostic, because the two require different responses.

#### Scenario: Reading a failed nightly run
- **WHEN** a maintainer opens a failed sanitizer workflow
- **THEN** the summary distinguishes a failing test assertion from a sanitizer report, without requiring the full log to be read to tell them apart

### Requirement: The Sanitizer Proves It Is Armed

The instrumented suite SHALL include a canary that commits deliberate undefined
behaviour and SHALL fail if the sanitizer does not trap it. The canary SHALL
verify both that the process failed and that it failed by printing a sanitizer
diagnostic; exit status alone is insufficient.

#### Scenario: Sanitizer is active
- **WHEN** the suite runs with instrumentation correctly applied
- **THEN** the canary aborts with a sanitizer diagnostic and its test passes

#### Scenario: Instrumentation silently absent
- **WHEN** the sanitizer flags do not reach the compile or link line
- **THEN** the canary runs to completion and exits zero, and its test **fails**, naming the instrumentation as the problem rather than reporting a clean suite

#### Scenario: Ordinary build
- **WHEN** a developer builds without `-DENABLE_UBSAN=ON`
- **THEN** the canary is neither compiled nor registered as a test

### Requirement: A Finding Fails the Run By Construction, Not By Environment

The build SHALL compile with `-fno-sanitize-recover=all`, so that a sanitizer
finding aborts the process independently of any environment variable set by a CI
workflow. `UBSAN_OPTIONS` MAY still be set as belt-and-braces, but it is not the
mechanism.

#### Scenario: Local instrumented run without CI environment
- **WHEN** a developer runs the instrumented suite without setting `UBSAN_OPTIONS`
- **THEN** a finding still aborts the test and fails the run

### Requirement: Check Selection Excludes Well-Defined Behaviour

The enabled sanitizer checks SHALL be the default `undefined` group plus `local-
bounds` and `float-divide-by-zero`. The `integer` group SHALL NOT be enabled,
because it flags well-defined unsigned wrapping and implicit conversions that
CRC and hashing code uses on purpose.

#### Scenario: Proposal to enable the integer group
- **WHEN** someone proposes adding `-fsanitize=integer` or `unsigned-integer-overflow` to the gating build
- **THEN** it is rejected for the gate unless each flagged site is shown to be an actual defect, because wrapping unsigned arithmetic is legal and used deliberately here

### Requirement: Detection Extends Past Language-Level Undefined Behaviour

The sanitizer build SHALL additionally enable hardened standard-library
assertions (`_LIBCPP_HARDENING_MODE` on libc++, `_GLIBCXX_ASSERTIONS` on
libstdc++) and SHALL set `QT_FORCE_ASSERTS`, keeping `Q_ASSERT` live in a
Release-type build. Hardened mode is needed because an out-of-range `operator[]`
that stays inside its allocation is missed by both sanitizers.

#### Scenario: Out-of-bounds container access inside the allocation
- **WHEN** a test executes an out-of-range `operator[]` whose index still falls within the allocated block
- **THEN** the hardened build traps it, rather than returning garbage that no sanitizer reports

#### Scenario: Release-configured sanitizer build
- **WHEN** the instrumented build is configured as a Release-type build
- **THEN** `Q_ASSERT` remains active, so documented invariants are still checked

### Requirement: Existing ASan Configuration Is Exercised

The ASan configuration already in `CMakeLists.txt` SHALL be run by CI on a
defined cadence, rather than existing as configuration nothing executes.

#### Scenario: ASan run occurs
- **WHEN** the defined cadence elapses (per-pull-request, nightly, or a documented alternative)
- **THEN** an ASan-instrumented run of the test suite executes and its result is visible without opening the workflow file to check whether it ran
