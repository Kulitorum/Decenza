# Spec Delta

## MODIFIED Requirements

### Requirement: No New Qt Policy Warnings
The build SHALL produce zero CMake Qt policy warnings during configuration.

#### Scenario: Clean CMake configure on any platform
- **WHEN** CMake configure runs on any supported platform
- **THEN** no `QTP` policy warning lines appear in configure output; any policy a Qt release
  introduces that would warn is set explicitly in `CMakeLists.txt`
