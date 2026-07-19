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
The instrumented suite SHALL include a canary that commits deliberate undefined behaviour and SHALL fail if the sanitizer does not trap it.

A sanitizer that is silently not applied produces exactly the same green suite as a codebase with no undefined behaviour in it. The first instrumented run here reported zero findings across all 82 tests — a good result, and one indistinguishable from `-fsanitize=undefined` having been dropped by a flag-ordering mistake, a toolchain quietly ignoring it, or a configure without `-DENABLE_UBSAN=ON`. Every subsequent clean run carries the same ambiguity. Without a canary, the gate can rot into a green no-op and the rot is invisible by construction — which is the same silent-failure shape this whole change exists to remove.

The canary SHALL verify both halves of the evidence: that the process failed, **and** that it failed by printing a sanitizer diagnostic. Exit status alone is insufficient — a canary that failed to link, or crashed for an unrelated reason, would otherwise be read as proof the sanitizer works.

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
The build SHALL be configured so that a sanitizer finding aborts the process, independently of any environment variable set by a CI workflow.

`UBSAN_OPTIONS=halt_on_error=1` achieves this, but only where it is set: it lives in the workflow file, where an unrelated-looking edit can drop it, and it is absent from a developer's local run entirely — so the same code can report undefined behaviour and exit zero on a laptop while failing in CI. Compiling with `-fno-sanitize-recover=all` makes "instrumented" and "fails on a finding" the same switch. The environment variable is kept as well, but as belt-and-braces rather than as the mechanism.

#### Scenario: Local instrumented run without CI environment
- **WHEN** a developer runs the instrumented suite without setting `UBSAN_OPTIONS`
- **THEN** a finding still aborts the test and fails the run

### Requirement: Check Selection Excludes Well-Defined Behaviour
The enabled sanitizer checks SHALL cover genuine undefined behaviour and SHALL NOT include groups that flag legal, intentional constructs.

Beyond the default `undefined` group, `local-bounds` (array index out of bounds on locals) and `float-divide-by-zero` (defined as infinity by IEEE 754, but a defect every time in flow, pressure and ratio arithmetic) are real defects in this codebase's terms. The `integer` group — unsigned overflow and implicit conversions — is deliberately excluded: it is well-defined C++ that CRC and hashing code wraps on purpose, so it reports intent as though it were a defect. A detector that cries wolf gets switched off, which costs more than it ever finds.

#### Scenario: Proposal to enable the integer group
- **WHEN** someone proposes adding `-fsanitize=integer` or `unsigned-integer-overflow` to the gating build
- **THEN** it is rejected for the gate unless each flagged site is shown to be an actual defect, because wrapping unsigned arithmetic is legal and used deliberately here

### Requirement: Detection Extends Past Language-Level Undefined Behaviour
The sanitizer build SHALL additionally enable hardened standard-library assertions and keep Qt assertions live.

There is a defect class that **both** sanitizers miss: an out-of-bounds `std::vector`/`QVector` `operator[]` whose index still lands inside the allocation. ASan sees nothing, because the memory is validly owned; UBSan sees nothing, because there is no undefined behaviour at the language level. The read silently returns garbage which then propagates into a shot. Hardened mode (`_LIBCPP_HARDENING_MODE` on libc++, `_GLIBCXX_ASSERTIONS` on libstdc++) turns that into a trap — verified against a plain build, which prints garbage and exits zero.

`QT_FORCE_ASSERTS` is required for the same reason: the sanitizer job configures a Release-type build, in which every `Q_ASSERT` in Qt's code and ours compiles to nothing. The invariants those assertions document would go unchecked in precisely the run built to check invariants.

#### Scenario: Out-of-bounds container access inside the allocation
- **WHEN** a test executes an out-of-range `operator[]` whose index still falls within the allocated block
- **THEN** the hardened build traps it, rather than returning garbage that no sanitizer reports

#### Scenario: Release-configured sanitizer build
- **WHEN** the instrumented build is configured as a Release-type build
- **THEN** `Q_ASSERT` remains active, so documented invariants are still checked

### Requirement: Existing ASan Configuration Is Exercised
The ASan configuration already present in `CMakeLists.txt` SHALL be run by CI on a defined cadence, rather than existing only as configuration nothing executes.

ASan is currently auto-enabled for local non-Apple Debug builds and is invoked by no workflow, so memory defects it would catch are found only if a developer happens to run a Debug build locally.

#### Scenario: ASan run occurs
- **WHEN** the defined cadence elapses (per-pull-request, nightly, or a documented alternative)
- **THEN** an ASan-instrumented run of the test suite executes and its result is visible without opening the workflow file to check whether it ran
