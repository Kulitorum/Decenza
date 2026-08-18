# build-config Specification

## Purpose
Records the Qt version, required Qt modules, platform deployment targets, and CMake configuration constraints that the Decenza build pins across all supported platforms (Windows, macOS, iOS, Android, Linux x64, Linux arm64). Bumps to Qt minor/patch versions and new platform requirements are recorded here so CI workflows, dev-machine installs, and `CMakeLists.txt` stay aligned.
## Requirements
### Requirement: iOS Minimum Deployment Target
The iOS build SHALL target iOS 17.0 as the minimum deployment target, matching the minimum iOS
version required by Qt 6.11.2.

#### Scenario: iOS CMake configure
- **WHEN** CMake is configured for the iOS platform
- **THEN** `CMAKE_OSX_DEPLOYMENT_TARGET` is set to `"17.0"` and the Xcode project is generated with
  that minimum

### Requirement: No New Qt Policy Warnings
The build SHALL produce zero CMake Qt policy warnings during configuration.

#### Scenario: Clean CMake configure on any platform
- **WHEN** CMake configure runs on any supported platform
- **THEN** no `QTP` policy warning lines appear in configure output; any new policies introduced by
  Qt 6.11 are explicitly set to NEW inside the `VERSION_GREATER_EQUAL "6.5.0"` guard in
  `CMakeLists.txt`

### Requirement: Verification Does Not Wait For A Release Tag
The project SHALL verify changes before they reach a release tag: by failing the build on compiler diagnostics at every developer's keyboard, and by building all six platforms and running the sanitizer suite on a nightly schedule.

The existing spec describes CI as the six tag-triggered platform workflows. That remains true for producing release artifacts, but as written it meant the first compile of a change on any platform other than the author's happened at release time — the wrong moment to discover that a change does not build, and how a build break reached a release tag (#1558).

Note what this requirement does **not** say: it does not require per-pull-request CI. That was built, measured, and rejected — see the `change-verification` capability for the evidence, which needs to be read before anyone reinstates it.

#### Scenario: Change is verified before it reaches a tag
- **WHEN** a change is developed
- **THEN** enabled diagnostics fail the author's own build, the full suite is run locally before the pull request, and the nightly six-platform build covers toolchains the author did not compile on

#### Scenario: Release workflows keep their existing role
- **WHEN** a release tag is pushed
- **THEN** the six platform workflows build and upload artifacts exactly as before, unchanged by any verification workflow

### Requirement: Debug Builds Are Instrumented By Default
Desktop Debug builds SHALL enable AddressSanitizer and UndefinedBehaviorSanitizer automatically, so ordinary local development exercises the instrumentation rather than requiring a special configuration nobody remembers to use. Release builds SHALL be untouched.

UBSan SHALL be in recovering mode for these auto-enabled builds (it reports and continues, so a finding does not halt a debugging session), while an explicit `-DENABLE_UBSAN=ON` SHALL give the halting mode CI uses.

#### Scenario: Developer builds Debug
- **WHEN** a developer configures a desktop Debug build with no sanitizer flags of their own
- **THEN** ASan and UBSan are active, and the application reports at startup which sanitizers are on

#### Scenario: Release build
- **WHEN** a Release build is configured
- **THEN** no sanitizer flags are added and runtime performance is unaffected

### Requirement: Instrumented Builds Are Identifiable At Runtime
The application SHALL report at startup which sanitizers are active, and SHALL make instrumentation state available to code that sizes memory thresholds.

An instrumented build's memory profile differs enough to trip guards calibrated for Release — ASan alone raises this application's startup RSS to roughly 460 MB — so a fixed ceiling either fires spuriously under instrumentation or is too loose to be useful without it.

Determining instrumentation state SHALL NOT rely on compiler macros alone: GCC defines no macro for UBSan at all, so a macro-only check reports "no sanitizers" on a fully instrumented binary.

#### Scenario: Startup on an instrumented build
- **WHEN** the application starts in a build with sanitizers enabled
- **THEN** it logs which sanitizers are active, so a clean run is known to mean something

#### Scenario: Memory ceiling under instrumentation
- **WHEN** a subsystem enforces a memory ceiling
- **THEN** the ceiling is scaled for instrumented builds rather than firing on ASan's overhead

### Requirement: Qt 6.11.2 as Build Framework
The system SHALL be built with Qt 6.11.2 across all supported platforms (Windows, macOS, iOS,
Android, Linux x64, Linux arm64).

Every workflow, dev-machine install path and `CMakeLists.txt` reference SHALL name the same version.
A patch bump within a Qt series changes no API and no platform floor, so it SHALL NOT be accompanied
by deployment-target, JDK, NDK or module-list changes; where one appears necessary, that is evidence
the bump is not what it was assumed to be and SHALL be investigated rather than absorbed.

#### Scenario: Windows desktop build
- **WHEN** the developer configures CMake with `-DCMAKE_PREFIX_PATH="C:/Qt/6.11.2/msvc2022_64"`
- **THEN** CMake finds all required Qt modules and the project builds without errors

#### Scenario: CI platform build
- **WHEN** a release tag is pushed and GitHub Actions runs
- **THEN** all six platform workflows (Windows, macOS, iOS, Android, Linux, Linux arm64) install
  Qt 6.11.2 via `jurplel/install-qt-action@v4` and produce a successful build artifact
- **AND** `nightly-sanitizers.yml` installs the same version, so the nightly ASan/UBSan run exercises
  the version the releases ship

#### Scenario: A cache key outlives the version it was populated for
- **WHEN** the pinned Qt version changes
- **THEN** every build cache key that names a Qt version SHALL be updated in the same change, so a
  stale entry cannot be hit against a different Qt

### Requirement: Decenza Ships Stock Qt Runtime Binaries
The application SHALL be packaged against the Qt binaries the upstream installer provides. The
project SHALL NOT ship a patched Qt runtime artifact — platform plugin, jar, framework or library —
in place of a stock one.

A patched runtime artifact carries costs that outlive the bug it fixes: it is ABI-locked to one Qt
version, it must be rebuilt or deleted at every bump, and it is a binary in the tree that only its
author can reproduce. Where an upstream bug is worth fixing, the fix belongs upstream, and the
project's position is to wait for the release that carries it rather than to fork.

This SHALL NOT be read as a claim that no upstream bug affects Decenza. It is a decision about where
the fix lives.

#### Scenario: An upstream Qt bug affects the app
- **WHEN** a Qt defect is identified that degrades Decenza on some platform
- **THEN** the remedy SHALL be an upstream patch, a workaround in Decenza's own code, or an accepted
  known issue — not a patched Qt binary committed to this repository

#### Scenario: The decision is revisited
- **WHEN** field crash or defect reports show an unpatched upstream bug occurring at a rate that
  justifies packaging a binary again
- **THEN** the change that reintroduces one SHALL state the observed rate it is responding to, and
  SHALL restore the version-lock guard that fails the build on a Qt/artifact mismatch before the
  artifact is packaged

#### Scenario: A Qt upgrade lands
- **WHEN** the pinned Qt version changes
- **THEN** no step in any workflow SHALL replace a file in the installed Qt tree with one built
  elsewhere, so a bump cannot produce a package assembled from a mixture of Qt versions
- **AND** *removing* a stock Qt file for packaging reasons is explicitly permitted — the Linux and
  Linux-arm64 workflows delete unused SQL, image-format and position plugins before `linuxdeploy`
  runs, because those plugins pull external dependencies an AppImage cannot satisfy. Deleting a
  file cannot introduce a foreign version; substituting one can. The rule is about provenance, not
  about the Qt tree being read-only

### Requirement: A Version Bump Records What It Inherits
A change that moves the pinned Qt version SHALL record which upstream fixes it is relying on, with
evidence taken from the released source or the upstream review system rather than from documentation
pages or release-note prose.

This exists because two of this project's local Qt workarounds are deleted on the strength of "it is
fixed upstream now", and that claim has a specific failure mode: a fix merged to `dev`, or to a
series branch after the release branch was cut, is not in the release. The distinction is invisible
in a release blog post.

#### Scenario: A workaround is deleted because upstream fixed it
- **WHEN** a change removes a local workaround on the grounds that the upstream fix has shipped
- **THEN** it SHALL cite the evidence that the fix is present in the exact released version —
  the tagged source, or the review record showing the merge onto that release's branch
- **AND** a fix present only on `dev` or on a later series SHALL NOT be treated as shipped

#### Scenario: A workaround's upstream fix has not shipped
- **WHEN** a bump is taken while some workaround's upstream fix is still outstanding
- **THEN** the change SHALL state plainly what behaviour is given up and under what condition it
  would be restored, rather than removing the workaround silently

