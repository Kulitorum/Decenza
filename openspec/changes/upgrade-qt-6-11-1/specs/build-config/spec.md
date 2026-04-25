## ADDED Requirements

### Requirement: Qt 6.11.1 as Build Framework
The system SHALL be built with Qt 6.11.1 across all supported platforms (Windows, macOS, iOS, Android, Linux x64, Linux arm64).

#### Scenario: Windows desktop build
- **WHEN** the developer configures CMake with `-DCMAKE_PREFIX_PATH="C:/Qt/6.11.1/msvc2022_64"`
- **THEN** CMake finds all required Qt modules and the project builds without errors

#### Scenario: CI platform build
- **WHEN** a release tag is pushed and GitHub Actions runs
- **THEN** all six platform workflows (Windows, macOS, iOS, Android, Linux, Linux arm64) install Qt 6.11.1 via `jurplel/install-qt-action@v4` and produce a successful build artifact

### Requirement: iOS Minimum Deployment Target
The iOS build SHALL target iOS 17.0 as the minimum deployment target, matching the minimum iOS version required by Qt 6.11.1.

#### Scenario: iOS CMake configure
- **WHEN** CMake is configured for the iOS platform
- **THEN** `CMAKE_OSX_DEPLOYMENT_TARGET` is set to `"17.0"` and the Xcode project is generated with that minimum

### Requirement: No New Qt Policy Warnings
The build SHALL produce zero CMake Qt policy warnings during configuration.

#### Scenario: Clean CMake configure on any platform
- **WHEN** CMake configure runs on any supported platform
- **THEN** no `QTP` policy warning lines appear in configure output; any new policies introduced by Qt 6.11 are explicitly set to NEW inside the `VERSION_GREATER_EQUAL "6.5.0"` guard in `CMakeLists.txt`
