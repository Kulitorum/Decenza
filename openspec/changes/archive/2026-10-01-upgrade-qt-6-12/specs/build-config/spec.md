# build-config Specification (delta)

## RENAMED Requirements

- FROM: `### Requirement: Qt 6.11.2 as Build Framework`
- TO: `### Requirement: Qt 6.12 as Build Framework`

## MODIFIED Requirements

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

## ADDED Requirements

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
