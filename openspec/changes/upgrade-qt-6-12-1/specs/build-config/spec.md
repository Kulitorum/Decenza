# Spec Delta

## ADDED Requirements

### Requirement: The Qt Version Has One Source

The Qt version the project builds against SHALL be written in exactly one file in the repository. CMake projects, build and packaging scripts, and CI workflows SHALL read the version from that file rather than spell it themselves, so that moving to another Qt release changes the version in one place.

#### Scenario: A Qt patch bump
- **WHEN** the version in the single source file is changed
- **THEN** every CMake project that requires a Qt version, every build or packaging script that checks one, and every CI job that installs Qt or keys a cache or artifact on it SHALL use the new version without any other file being edited

#### Scenario: A version is hard-coded again
- **WHEN** a change adds a Qt version as a literal to a workflow, CMake file or build script outside the single source file
- **THEN** the per-PR text-invariants check SHALL fail and name the file and line

#### Scenario: A dated reference to an earlier Qt version
- **WHEN** a workflow, CMake file or build script must keep an earlier Qt version as a dated record, or a whole-line comment or prose document names one
- **THEN** the check SHALL NOT fail on it: a dated file and version are listed in the check's allowlist, and whole-line comments and prose docs are not checked

### Requirement: Apple Builds Use The Current SDK

The iOS and macOS release builds SHALL be produced with one Xcode version whose SDKs App Store Connect accepts for upload, written once in the release workflow. Moving to a newer SDK SHALL NOT raise the deployment floors or drop an architecture the app ships.

#### Scenario: App Store Connect raises its minimum SDK
- **WHEN** Apple requires uploads to be built with a newer iOS SDK, as it does with the iOS 27 SDK from April 2027
- **THEN** the iOS build SHALL use an Xcode that provides that SDK before the requirement takes effect

#### Scenario: One toolchain for both Apple jobs
- **WHEN** the release workflow builds iOS and macOS
- **THEN** both jobs SHALL select the same Xcode version from a single definition

#### Scenario: Deployment floors survive the SDK move
- **WHEN** the Apple builds move to a newer SDK
- **THEN** the iOS package SHALL still declare iOS 18.0 as its minimum, the macOS package macOS 14.4, and the macOS app SHALL still contain both `x86_64` and `arm64`
