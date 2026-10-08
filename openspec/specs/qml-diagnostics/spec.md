# qml-diagnostics Specification

## Purpose

Defines how QML code is checked for diagnostics that the build can act on: C++ objects exposed to QML must be statically resolvable, `qmllint` runs over every QML file as part of the build, and the `unqualified` category is enforced per file against a clean list that only grows. It also records the exemption backlog and the residual scope diagnostics that remain.

## Requirements
### Requirement: QML-Visible C++ Objects Are Statically Resolvable
Every C++ object exposed to QML SHALL be registered as a QML singleton under the project's module URI, not injected with `setContextProperty()`. A context property exists only at runtime, so `qmllint` reports every reference to it as unqualified, which hides genuine undeclared identifiers among them. The registration form changes, but the QML-visible name does not, so call sites keep reading `Settings.theme.x`.

#### Scenario: A new C++ object is exposed to QML
- **WHEN** a contributor exposes a new C++ object to QML
- **THEN** it is registered as a QML singleton under the module URI, and `qmllint` resolves every
  reference to it without a new exemption

#### Scenario: A context property is reintroduced
- **WHEN** a `setContextProperty()` call is added for an object QML code references by name
- **THEN** the qmllint gate fails, because the resulting unqualified accesses are not exemptible

#### Scenario: Migration preserves runtime behaviour
- **WHEN** an object moves from `setContextProperty()` to singleton registration
- **THEN** every QML expression reading it resolves to the same object with the same property
  values, and no QML call site is edited to accommodate the change

### Requirement: The Build Enforces QML Diagnostics
The project SHALL run `qmllint` over every QML file in the module as part of a build target, and CI SHALL fail on any non-exempt diagnostic. Coverage SHALL be unconditional: every file on every gate platform, with the pinned Qt's `qmllint`. No file SHALL be excluded because the tool cannot process it; such a gap SHALL be stated and fail the run. Enforcement SHALL apply from day one, with the exemption list sized to the current backlog.

#### Scenario: New QML introduces a non-exempt diagnostic
- **WHEN** a contributor adds QML producing a diagnostic in a category not on the exemption list
- **THEN** the gate fails before the change can merge

#### Scenario: The gate runs without a release tag
- **WHEN** a branch is pushed
- **THEN** the QML diagnostics run, rather than waiting for a tag-triggered release build

#### Scenario: Every file is analysed
- **WHEN** the gate runs on any platform
- **THEN** the file count it reports as analysed equals the number of `.qml` files in
  `qt_add_qml_module`, and no file is reported as skipped

#### Scenario: A run did not finish
- **WHEN** the tool exits non-zero, is killed, or does not reach every file
- **THEN** the run SHALL be reported as failed, and SHALL NOT be used to record or lower any
  recorded count — a file the tool never reached emits no warnings and must never be counted clean

### Requirement: The Gate Runs In The Default Build, And Costs Nothing When Idle
The QML diagnostics check SHALL run as part of the default build target on desktop platforms, so a regression fails the build of the developer who wrote it. It SHALL be skipped when nothing it reads has changed. It SHALL run after linking and SHALL NOT prevent a binary from being produced. A failure SHALL NOT be recorded as done: the next build SHALL re-run the check.

#### Scenario: A QML regression is written
- **WHEN** a developer introduces a non-exempt diagnostic and builds
- **THEN** the build fails, naming the file, the count and the fix — and explicitly refusing the
  wrong fix of adding the file to the baseline

#### Scenario: A C++-only edit is rebuilt
- **WHEN** a build changes no QML source, no gate script and no baseline
- **THEN** the check does not run at all

#### Scenario: A release build for a mobile platform
- **WHEN** the target platform is Android or iOS
- **THEN** the check is not part of the default build, because the same QML has already been
  checked on desktop and a release must not acquire a new way to fail

### Requirement: The `unqualified` Category Is Never Exempt
The `unqualified` category SHALL NOT appear on the exemption list, and SHALL NOT be disabled per file, per line, or by configuration. It is the only automated detector for an undeclared QML identifier, and such an identifier compiles clean and fails only when its binding is first evaluated. Where an unqualified access is unavoidable, the identifier SHALL be made resolvable instead of the report being silenced.

#### Scenario: Exemption list is proposed to include the category
- **WHEN** `unqualified` is added to the exemption list to make the gate pass
- **THEN** the change is rejected, and the underlying identifiers are made resolvable instead

#### Scenario: Undeclared identifier in a rarely-evaluated binding
- **WHEN** a contributor writes a binding referencing an identifier not in scope, in a property
  evaluated only under a setting nobody enables during review
- **THEN** the gate fails at build time rather than the binding throwing on a user's device

### Requirement: `unqualified` Is Enforced Per File, Against A Clean List That Only Grows
Enforcement for `unqualified` SHALL be keyed per file. Every QML file with zero unqualified warnings SHALL be recorded on a clean list and SHALL stay at zero. Every other file SHALL carry a recorded count that SHALL only decrease, and a new file SHALL start on the clean list. A category-level exemption does not work here, because exempting `unqualified` would discard the signal this capability protects.

#### Scenario: A locked file regresses
- **WHEN** a contributor introduces an unqualified access in a file on the clean list
- **THEN** the gate fails, regardless of the tree-wide total

#### Scenario: A file is cleaned
- **WHEN** the last unqualified access in a listed file is fixed
- **THEN** that file moves to the clean list in the same change, and cannot regress afterwards

#### Scenario: A new QML file is added
- **WHEN** a contributor adds a QML file
- **THEN** it is held to zero unqualified warnings, with no entry available to carry a backlog for
  new code

#### Scenario: A dirty file grows
- **WHEN** a change raises the recorded count of a file not yet on the clean list
- **THEN** the gate fails; the recorded counts are ceilings, not budgets to spend

#### Scenario: Initial split after the singleton migration
- **GIVEN** the singleton migration has landed
- **WHEN** the per-file state is recorded
- **THEN** 104 of 212 files SHALL be locked on the clean list immediately
- **AND** the remaining 108 files SHALL carry recorded counts totalling 4,274 warnings

### Requirement: The Exemption List Is Explicit And Only Shrinks
Diagnostic categories not yet cleared SHALL be recorded as a single labelled block in the build configuration, and that block SHALL only ever have entries removed. Each entry SHALL carry the count of occurrences it currently covers. The capability is complete when the block is empty and deleted.

#### Scenario: Clearing a category
- **WHEN** the last occurrence of an exempt category is fixed
- **THEN** its entry is removed from the block in the same change

#### Scenario: A cleared category regresses
- **WHEN** a contributor reintroduces a diagnostic in a category already removed from the block
- **THEN** the build fails, with no count comparison involved

### Requirement: Residual Scope Diagnostics Are Recorded, Not Hidden
Scope diagnostics left after the singleton migration (unqualified access to delegate and file-scope identifiers such as `modelData`, `root`, `index` and `model`) SHALL be recorded as per-file counts under the preceding requirement, with `pragma ComponentBehavior: Bound` named as their remedy. They SHALL NOT be folded into the category exemption list.

#### Scenario: Reader asks what is left
- **WHEN** a contributor reads the recorded state after the migration lands
- **THEN** the scope backlog is visible as per-file counts with `pragma ComponentBehavior: Bound`
  named as its remedy, and is distinguishable from the category exemptions

#### Scenario: Scope backlog is not silently absorbed
- **WHEN** a change would make the gate pass by adding `unqualified` to the category exemption
  list on the grounds that the scope warnings are unavoidable
- **THEN** the change is rejected in favour of the per-file clean list

