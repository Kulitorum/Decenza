## ADDED Requirements

### Requirement: Diagnostics Are Enforced At The Developer's Keyboard
The project SHALL enforce compiler diagnostics in every build, on every machine, rather than in a CI job that runs after the fact. `-Wall -Wextra -Werror` (plus probed additions) SHALL be on by default in `CMakeLists.txt`.

This is where a warning is cheapest to fix: before it is committed, by the person holding the context. A red build is a forcing function; a count in a CI log is not.

#### Scenario: New warning introduced
- **WHEN** a developer writes code that trips an enabled diagnostic
- **THEN** their own build fails, naming the file and line, before anything is committed

#### Scenario: Warning reaches CI instead
- **WHEN** someone proposes catching diagnostics in a CI job rather than the build
- **THEN** it is rejected: the build already fails on them everywhere, and a second reporting channel adds latency without adding coverage

### Requirement: A Pre-Merge CI Gate Is Deliberately Not Used
The project SHALL NOT run a compile-and-test gate on every pull request. This is a decision with evidence behind it, not an unfinished migration.

A pre-merge gate was built, run, and removed inside this change. The evidence for removing it is the reason it must not be silently reinstated: three detectors (UBSan, ASan, `-Wall -Wextra`) were run for the first time across eight months of previously unexamined code — the moment a new tool's harvest should be at its largest — and they found **no pre-existing runtime defects at all**. The only two failures the gate ever produced were problems it created itself: a `vptr` typeinfo link break from enabling UBSan, and a clang-only `-fsanitize` flag that GCC rejected.

A near-empty first harvest is evidence the codebase is clean on these axes, which makes the expected future yield low. A low-yield detector does not earn a place on the critical path of every push, and the cost was real: 10–15 minutes added to each pull request for a team of three that already runs the full suite locally before opening one.

#### Scenario: Someone proposes adding a pre-merge gate
- **WHEN** a pre-merge compile-and-test gate is proposed
- **THEN** it is weighed against this measurement, and adopted only with new evidence that the yield has changed — not on the general principle that pre-merge CI is good practice

#### Scenario: Local suite is the gate
- **WHEN** a change is prepared for a pull request
- **THEN** the full `ctest` suite is run locally first, and that run is the gate

### Requirement: Cross-Platform Divergence Is Covered By Nightly Builds
Because the local gate runs on one platform, the project SHALL build all six platforms on a schedule, so a toolchain-specific break is found within a day rather than at a release tag.

The gap this closes is specific and demonstrated. Local verification happens on macOS/clang; Linux/GCC, Windows/MSVC, iOS and Android were first compiled at release-tag time. #1558 — the build break that motivated this whole change — was inside `#ifdef Q_OS_IOS` and compiled nowhere else.

That gap is not theoretical: enabling these diagnostics took **seven** rounds of platform burndown, and every round after the third found exactly one GCC-only diagnostic class that macOS could not have surfaced.

#### Scenario: Toolchain-specific break introduced
- **WHEN** a change compiles on the author's platform but not on another
- **THEN** the nightly six-platform run fails, and the break is found within a day instead of at the next release tag

#### Scenario: Nightly builds are cold
- **WHEN** the nightly platform builds run
- **THEN** they build without a warm compiler cache, since nobody waits on them and the shared cache budget is better spent keeping release builds warm

### Requirement: Scheduled Verification Is Not Release-Gated
Scheduled verification workflows SHALL be independent of the six platform release workflows: they SHALL NOT upload artifacts, bump the version code, or publish to any release.

#### Scenario: Nightly run completes
- **WHEN** a scheduled verification workflow finishes
- **THEN** no GitHub Release is created or modified, `versioncode.txt` is unchanged, and nothing is uploaded to a release

### Requirement: A Green Scheduled Run States What It Did Not Cover
A scheduled verification workflow SHALL document its coverage limits where its result is read, so a green run is not mistaken for broader assurance than it provides.

A green tick invites the reading "everything is fine". The sanitizer nightly covers one platform, only code the test suite executes, with coverage unmeasured, and cannot detect data races at all — ThreadSanitizer being unusable against an uninstrumented Qt (measured: 10,194 reports, 94% false positives from Qt's own queued-connection machinery).

#### Scenario: Reading a green nightly
- **WHEN** a maintainer sees a passing scheduled run
- **THEN** the workflow states which platform it covered, that coverage is limited to what the suite executes, and which defect classes it cannot see
