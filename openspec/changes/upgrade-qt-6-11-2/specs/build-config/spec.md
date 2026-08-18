# build-config Specification (delta)

Qt 6.11.1 → 6.11.2, and a standing decision to ship stock Qt runtime binaries. Decided 2026-08-18.

## RENAMED Requirements

- FROM: `### Requirement: Qt 6.11.1 as Build Framework`
- TO: `### Requirement: Qt 6.11.2 as Build Framework`

## MODIFIED Requirements

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

## ADDED Requirements

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
- **THEN** no step in any workflow overwrites a file inside the installed Qt tree, so a bump cannot
  produce a package built from a mismatched mixture of Qt versions

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
