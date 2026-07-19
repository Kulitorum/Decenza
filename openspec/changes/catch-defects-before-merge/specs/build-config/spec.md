## ADDED Requirements

### Requirement: CI Verification Is Not Release-Gated
The project SHALL verify changes by compiling and testing them on pull requests, in addition to the six platform workflows that build on a release tag push.

The existing spec describes CI as the six tag-triggered platform workflows. That remains true for producing release artifacts, but it is no longer the whole picture: as written, the first compile of any change happened at release time, which is the wrong moment to discover that a change does not build.

#### Scenario: Change is verified before it reaches a tag
- **WHEN** a change is proposed as a pull request
- **THEN** it is compiled and its tests are run by CI before merge, independently of any release tag

#### Scenario: Release workflows keep their existing role
- **WHEN** a release tag is pushed
- **THEN** the six platform workflows build and upload artifacts exactly as before, unchanged by the pre-merge job
