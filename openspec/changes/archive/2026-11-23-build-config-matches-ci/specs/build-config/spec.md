# Spec Delta

## MODIFIED Requirements

### Requirement: Verification Does Not Wait For A Release Tag

The project SHALL verify changes before they reach a release tag: developer builds SHALL fail on compiler diagnostics, the sanitizer suite SHALL run on Linux on a nightly schedule, and all six platforms SHALL be buildable on demand from any pushed branch through the release workflow. This requirement does not require per-pull-request CI; the `change-verification` capability holds the evidence for that decision.

#### Scenario: Change is verified before it reaches a tag
- **WHEN** a change is developed
- **THEN** enabled diagnostics fail the author's own build, the full suite is run locally before the pull request, and a toolchain the author did not compile on can be covered by dispatching the release workflow for that platform on the branch

#### Scenario: Release workflows keep their existing role
- **WHEN** a release tag is pushed
- **THEN** the release workflow's six platform jobs build and upload artifacts exactly as before, unchanged by any verification workflow

### Requirement: Debug Builds Are Instrumented By Default
Desktop Debug builds made with a single-configuration generator (Ninja or Makefiles) SHALL enable AddressSanitizer automatically, and UndefinedBehaviorSanitizer too except under MSVC, so ordinary local development exercises the instrumentation rather than requiring a special configuration nobody remembers to use. Release builds SHALL be untouched.

UBSan SHALL be in recovering mode for these auto-enabled builds (it reports and continues, so a finding does not halt a debugging session), while an explicit `-DENABLE_UBSAN=ON` SHALL give the halting mode CI uses.

#### Scenario: Developer builds Debug
- **WHEN** a developer configures a desktop Debug build with a single-configuration generator and no sanitizer flags of their own
- **THEN** ASan is active, UBSan is active unless the compiler is MSVC, and the application reports at startup which sanitizers are on

#### Scenario: Release build
- **WHEN** a Release build is configured
- **THEN** no sanitizer flags are added and runtime performance is unaffected

### Requirement: Qt 6.12 as Build Framework
The system SHALL be built with the same released Qt 6.12 patch version across Windows, macOS, iOS, Android, Linux x64, and Linux arm64.

Every workflow, dev-machine install path, and active CMake reference SHALL agree on that version. Subsequent patch bumps within the series SHALL NOT introduce deployment-target, JDK, NDK, or module-list changes without investigating and documenting why they are required.

#### Scenario: Windows desktop build
- **WHEN** the developer configures CMake with the selected Qt installation as CMAKE_PREFIX_PATH
- **THEN** CMake finds all required modules and the project builds without errors

#### Scenario: CI platform build
- **WHEN** a release tag is pushed and GitHub Actions runs
- **THEN** each of the release workflow's six platform jobs installs the version in `qt-version.txt` via jurplel/install-qt-action@v4 and produces a successful build artifact
- **AND** nightly-sanitizers.yml installs that same version

#### Scenario: A cache key outlives the version it was populated for
- **WHEN** the pinned Qt version changes
- **THEN** every build cache key naming a Qt version SHALL be updated in the same change

#### Scenario: Android toolchain matches the installed Qt
- **WHEN** an Android build is configured
- **THEN** it SHALL use JDK 21 and compatible Gradle/Android Gradle Plugin versions for the selected Qt release
- **AND** the NDK revision SHALL be derived from Qt's toolchain rather than guessed
- **AND** the build SHALL produce a signed APK with the intended version metadata
