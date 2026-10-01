# build-config Specification

## Purpose
Records the Qt version, required Qt modules, platform deployment targets, and CMake configuration constraints that the Decenza build pins across all supported platforms (Windows, macOS, iOS, Android, Linux x64, Linux arm64). Bumps to Qt minor/patch versions and new platform requirements are recorded here so CI workflows, dev-machine installs, and `CMakeLists.txt` stay aligned.

## Requirements

### Requirement: iOS Minimum Deployment Target
The iOS build SHALL target iOS 18.0 as the minimum deployment target, matching Qt 6.12's minimum.

#### Scenario: iOS CMake configure
- **WHEN** CMake is configured for iOS
- **THEN** CMAKE_OSX_DEPLOYMENT_TARGET SHALL be 18.0 and the generated app project SHALL carry that minimum
- **AND** bundled widget target settings SHALL be compatible with installation and operation on iOS 18

#### Scenario: A device below the floor is not treated as validated
- **WHEN** the deployment target exceeds the OS supported by an available test device
- **THEN** validation SHALL name the device/OS actually used or explicitly record the remaining hardware validation hold
- **AND** a successful build SHALL NOT be represented as successful device validation

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

### Requirement: Qt 6.12 as Build Framework
The system SHALL be built with the same released Qt 6.12 patch version across Windows, macOS, iOS, Android, Linux x64, and Linux arm64.

Every workflow, dev-machine install path, and active CMake reference SHALL agree on that version. Subsequent patch bumps within the series SHALL NOT introduce deployment-target, JDK, NDK, or module-list changes without investigating and documenting why they are required.

#### Scenario: Windows desktop build
- **WHEN** the developer configures CMake with the selected Qt 6.12 installation as CMAKE_PREFIX_PATH
- **THEN** CMake finds all required modules and the project builds without errors

#### Scenario: CI platform build
- **WHEN** a release tag is pushed and GitHub Actions runs
- **THEN** all six platform workflows install the same released Qt 6.12 patch via jurplel/install-qt-action@v4 and produce successful build artifacts
- **AND** nightly-sanitizers.yml installs that same version

#### Scenario: A cache key outlives the version it was populated for
- **WHEN** the pinned Qt version changes
- **THEN** every build cache key naming a Qt version SHALL be updated in the same change

#### Scenario: Android toolchain matches the installed Qt
- **WHEN** an Android build is configured
- **THEN** it SHALL use JDK 21 and compatible Gradle/Android Gradle Plugin versions for the selected Qt release
- **AND** the NDK revision SHALL be derived from Qt's toolchain rather than guessed
- **AND** the build SHALL produce a signed APK with the intended version metadata

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

### Requirement: macOS Minimum Deployment Target
The macOS build SHALL set macOS 14.4 as its minimum deployment target, matching Qt 6.12's minimum.

#### Scenario: macOS package is produced
- **WHEN** the macOS app is configured and packaged
- **THEN** the effective deployment target and packaged minimum-OS metadata SHALL agree on macOS 14.4
- **AND** the release workflow SHALL NOT continue advertising a 13.0 deployment target

### Requirement: Qt Configuring Tool Version
Qt 6.12 builds SHALL use CMake 3.25 or newer as the configuring executable. This tool requirement SHALL be checked separately from the project's cmake_minimum_required policy baseline.

#### Scenario: Developer or CI configures the project
- **WHEN** a Qt Creator kit or CI runner configures a Qt 6.12 build
- **THEN** the actual CMake executable SHALL be version 3.25 or newer
- **AND** a lower policy-baseline declaration SHALL NOT be treated as permission to configure with an older executable

### Requirement: Android Target SDK Follows Qt
The Android build SHALL target the API level that the selected Qt release defaults to (37 for Qt 6.12.0) and SHALL keep minSdk 28. A target bump SHALL NOT ship until the platform behaviour changes it opts into are handled or explicitly accepted.

#### Scenario: Packaged manifest
- **WHEN** the Android package is built
- **THEN** its manifest SHALL declare targetSdkVersion 37 and minSdkVersion 28
- **AND** the compile SDK SHALL be at least the target SDK

#### Scenario: Older devices are unaffected
- **WHEN** the app runs on a device below Android 17
- **THEN** it SHALL install and behave as that device's Android version requires, with no new prompt

### Requirement: Android Local Network Access Is Requested
On Android 17 and later, the app SHALL declare `ACCESS_LOCAL_NETWORK` and request it at launch when any local-network feature is enabled (ShotServer, MQTT, remote access, a saved WiFi scale), or otherwise when the user first enables or uses one. Denial SHALL be reported visibly rather than failing as a timeout.

#### Scenario: Feature enabled before the upgrade
- **WHEN** the app launches on Android 17 with a local-network feature already enabled and the permission not yet decided
- **THEN** the app SHALL request the permission at launch

#### Scenario: Permission granted
- **WHEN** the user starts a LAN feature (WiFi scale scan or connect, ShotServer, MQTT, device migration, remote access) and grants the permission
- **THEN** the feature SHALL work as on earlier Android versions

#### Scenario: Permission denied
- **WHEN** the user has denied the permission
- **THEN** the app SHALL state which feature needs local network access and where to allow it
- **AND** BLE operation of the machine and scales SHALL be unaffected

### Requirement: Android Package Is Signed By Qt's Deployment Tool
The release APK SHALL be signed by androiddeployqt via `QT_ANDROID_SIGN_APK`, with keystore credentials taken only from the environment. No AAB SHALL be produced, since Decenza is not distributed through Google Play.

#### Scenario: Release build with credentials
- **WHEN** a CI release build runs with the keystore environment set
- **THEN** it SHALL produce exactly one signed APK and no AAB
- **AND** the repository SHALL contain no keystore password or machine-specific keystore path

#### Scenario: Release build without credentials
- **WHEN** a CI release build runs without keystore credentials
- **THEN** it SHALL fail rather than publish an unsigned artifact

### Requirement: Raised Platform Floors Are Explained
A release raising a supported OS minimum SHALL state the new floor, its cause, and the affected OS/device classes accurately.

#### Scenario: Qt 6.12 release communication
- **WHEN** the upgrade is released
- **THEN** release notes SHALL identify iOS/iPadOS 18 and macOS 14.4 as requirements inherited from Qt 6.12
- **AND** affected-device wording SHALL use OS compatibility rather than an inaccurate universal chipset cutoff
- **AND** the notes SHALL NOT claim that macOS users are unaffected
