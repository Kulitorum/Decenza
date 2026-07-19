## ADDED Requirements

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
