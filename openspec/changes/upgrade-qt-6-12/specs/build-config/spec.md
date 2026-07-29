# build-config Specification (delta)

Decided 2026-07-29: one Qt version across every platform, taking Qt 6.12's iOS 18 floor with it.

## RENAMED Requirements

- FROM: `### Requirement: Qt 6.11.1 as Build Framework`
- TO: `### Requirement: Qt 6.12 as Build Framework`

## MODIFIED Requirements

### Requirement: Qt 6.12 as Build Framework
The system SHALL be built with Qt 6.12 across all supported platforms (Windows, macOS, iOS, Android, Linux x64, Linux arm64).

#### Scenario: Windows desktop build
- **WHEN** the developer configures CMake with `-DCMAKE_PREFIX_PATH="C:/Qt/6.12/msvc2022_64"`
- **THEN** CMake finds all required Qt modules and the project builds without errors

#### Scenario: CI platform build
- **WHEN** a release tag is pushed
- **THEN** all six platform workflows (Windows, macOS, iOS, Android, Linux, Linux arm64) install Qt 6.12 via `jurplel/install-qt-action@v4` and produce a successful build artifact
- **AND** `nightly-sanitizers.yml` installs the same Qt version, so the nightly ASan/UBSan run exercises the version the releases ship

#### Scenario: Android toolchain matches what Qt 6.12 requires
- **WHEN** `android-release.yml` runs
- **THEN** it provisions **JDK 21** (Qt 6.12's required JDK; Qt 6.11.1 needed only 17)
- **AND** the NDK revision it resolves SHALL be the one Qt's own toolchain file names, rather than a hardcoded guess

### Requirement: iOS Minimum Deployment Target
The iOS build SHALL target iOS 18.0 as the minimum deployment target, matching the minimum iOS version required by Qt 6.12.

#### Scenario: iOS CMake configure
- **WHEN** CMake is configured for the iOS platform
- **THEN** `CMAKE_OSX_DEPLOYMENT_TARGET` SHALL be `18.0`
- **AND** the accompanying comment SHALL state that the floor comes from Qt 6.12, not from Decenza's own API use

#### Scenario: A device below the floor is not silently unsupported
- **WHEN** the iOS deployment target rises above what an existing test or user device can run
- **THEN** the change that raises it SHALL record which device classes are dropped
- **AND** SHALL NOT be merged on the assumption that "the build is green" means iOS is still covered — a green iOS build proves compilation, not that any available device can run it

#### Scenario: A raised platform minimum is explained to users
- **WHEN** a release raises the minimum OS version on any platform
- **THEN** the release notes SHALL state the new minimum, the cause, and the device classes affected
- **AND** where the cause is an upstream framework requirement rather than a Decenza decision, the notes SHALL say so plainly, so a user who stops receiving updates can identify why
- **AND** SHALL state which platforms are unaffected, so a single-platform floor is not read as an app-wide one

## ADDED Requirements

### Requirement: Patched Qt Platform Artifacts Are Version-Locked
Where Decenza ships a patched Qt artifact (an Android platform plugin or Qt jar) to fix an upstream bug, that artifact SHALL be locked to the exact Qt version it was built from, and the build SHALL fail rather than package a mismatched one.

#### Scenario: Qt version is bumped without dealing with the override
- **WHEN** `env.QT_VERSION` in `android-release.yml` no longer matches `android/qt-overrides/BUILT_AGAINST_QT`
- **THEN** the workflow SHALL fail with an explicit error
- **AND** SHALL NOT fall back to the stock artifact or continue with a warning, because a stale plugin links against Qt private symbols and produces an APK that dies at startup on every device

#### Scenario: An override's upstream bug is fixed
- **WHEN** a Qt upgrade includes the upstream fix for a bug an override patches
- **THEN** that portion of the override SHALL be deleted rather than rebuilt
- **AND** the deletion SHALL be justified by the fix's presence on the release branch being upgraded to — not by its presence on `dev`, and not by a doc page or changelog entry
- **AND** where only some of an override's patches are upstream, the remaining ones SHALL be rebuilt against the new Qt and the README updated to list only the bugs still being patched

#### Scenario: A patched behavior is verified on device after the override is dropped
- **WHEN** an override that fixed a user-visible behavior is removed because upstream fixed it
- **THEN** that behavior SHALL be verified on a real device before release
- **AND** a regression in the upstream fix SHALL be treated as a blocker for the upgrade, restoring the patch
