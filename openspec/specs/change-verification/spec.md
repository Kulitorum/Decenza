# change-verification Specification

## Purpose
Defines where a change is verified before it lands: in the build on the developer's own machine, in the full `ctest` suite run locally before a pull request, and in scheduled six-platform and sanitizer runs that cover the toolchains one machine cannot. It also records, with the measurement behind it, the deliberate decision **not** to run a per-pull-request CI gate — a gate that was built, run, and removed inside the change that created this capability. That evidence is kept here so the gate is not silently reinstated on the general principle that pre-merge CI is good practice.

## Requirements
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

### Requirement: Cross-Platform Coverage Comes From The Pre-Release Cadence
The six platform workflows SHALL remain the cross-platform check, driven by the project's existing pre-release cadence. There SHALL NOT be a scheduled six-platform build.

**A scheduled version of this existed and was removed within a day of shipping, because it was slower than what the project already does.**

Measured over the 14 days to 2026-07-19, each platform workflow ran **37-48 times** — Linux 48, iOS 42, Linux arm64 43, Windows 37, macOS 37 — roughly **three builds per platform per day**, driven by near-daily pre-releases against a rolling `v2.0.0` beta. A nightly would have compiled these platforms at *one third* the existing frequency. It was not redundant coverage; it was worse coverage with an added standing cost.

Note that the version-tag list does not show this: those tags land every 1-2 weeks, and reading them as the build cadence understates it by an order of magnitude. The pre-release runs are the real signal.

The gap this was aimed at is nonetheless real and demonstrated: local verification happens on macOS/clang, and #1558 — the break that motivated this whole change — was inside `#ifdef Q_OS_IOS` and compiled nowhere else. Enabling these diagnostics took **seven** rounds of platform burndown, each finding a class exactly one platform could see. But that burndown was the one-off cost of turning the flags on, and the pre-release cadence already closes the ongoing gap at a rate no nightly could match.

#### Scenario: Change touches platform-guarded code
- **WHEN** a change modifies code inside `#ifdef Q_OS_IOS`, `Q_OS_ANDROID`, or another platform guard
- **THEN** it is compiled by the next pre-release run of that platform, typically the same day; dispatching that platform build-only is available when the author does not want to wait

#### Scenario: Proposal to restore a scheduled build
- **WHEN** a scheduled six-platform build is proposed again
- **THEN** it is first measured against the actual pre-release build frequency, since a nightly is only worth adding if it would run *more* often than the platforms already build

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
