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

#### Scenario: Someone proposes adding a pre-merge gate
- **WHEN** a pre-merge compile-and-test gate is proposed
- **THEN** it is weighed against this measurement, and adopted only with new evidence that the yield has changed — not on the general principle that pre-merge CI is good practice

#### Scenario: Local suite is the gate
- **WHEN** a change is prepared for a pull request
- **THEN** the full `ctest` suite is run locally first, and that run is the gate

### Requirement: Cross-Platform Coverage Comes From The Pre-Release Cadence
The six platform workflows SHALL remain the cross-platform check, driven by the project's existing pre-release cadence. There SHALL NOT be a scheduled six-platform build.

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

#### Scenario: Reading a green nightly
- **WHEN** a maintainer sees a passing scheduled run
- **THEN** the workflow states which platform it covered, that coverage is limited to what the suite executes, and which defect classes it cannot see
