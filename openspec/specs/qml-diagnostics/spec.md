# qml-diagnostics Specification

## Purpose
TBD - created by archiving change fix-qmllint-usability. Update Purpose after archive.
## Requirements
### Requirement: QML-Visible C++ Objects Are Statically Resolvable
Every C++ object exposed to QML SHALL be registered in a form a static analyser can resolve —
a QML singleton under the project's module URI — rather than injected into the root context with
`setContextProperty()`.

A context property exists only at runtime, so `qmllint`, `qmlcachegen` and the QML language server
cannot know the name is valid. Each one therefore produces an `Unqualified access` warning at every
call site, and those warnings are indistinguishable from the ones that mark a genuine undeclared
identifier. Thirty-nine such registrations produced 7,167 warnings — 61% of the file's total — and
buried the one real error that shipped in 2.0.1.

Registration form is what changes; the QML-visible name does not. Call sites keep reading
`Settings.theme.x`, so the migration touches registrations and imports, not the ~7,167 lines that
use them.

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
The project SHALL run `qmllint` over every QML file in the module as part of a build target, and
CI SHALL fail when it reports a non-exempt diagnostic.

Coverage SHALL be unconditional: every file in the module, on every platform that runs the gate,
using the `qmllint` that ships with the pinned Qt. No file SHALL be excluded from the run on the
grounds that the tool cannot process it. Should a future toolchain reintroduce such a file, the
correct response is to state the gap and fail — never to exclude the file and report the remainder
as full coverage.

Enforcement goes on with the exemption list sized to the current backlog, so it is real from day
one for everything outside it. This mirrors the `-Wall -Wextra -Werror` contract in
`compiler-diagnostics`, which governs the same question for C++ and deliberately says nothing
about QML.

A developer-only instruction to "run qmllint before pushing" is not enforcement: it is what the
project had when a `ReferenceError` reached a release.

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
The QML diagnostics check SHALL run as part of the default build target on desktop platforms, so a
regression fails the build of the developer who wrote it rather than a nightly job hours later.

It SHALL be skipped when nothing it reads has changed. A check that re-runs on every build taxes
edits it cannot possibly be affected by, and the first response to that tax is to switch the check
off — which costs more than the check ever saved.

It SHALL NOT prevent a binary from being produced. The check runs after linking, so a developer is
never blocked from running the application to investigate the very thing they are working on.

A failure SHALL NOT be recorded as done: the next build re-runs the check rather than treating the
previous failure as satisfied.

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
The `unqualified` category SHALL NOT appear on the exemption list, and SHALL NOT be disabled
per-file, per-line, or via configuration.

This category is the entire reason the capability exists. It is the only automated detector for an
undeclared QML identifier — which compiles clean, fails only when its binding is first evaluated,
and therefore reaches users. Suppressing it to reach a green gate would trade the one signal that
matters for the appearance of cleanliness.

Where an unqualified access is genuinely unavoidable, the fix is to make the identifier resolvable,
not to silence the report.

#### Scenario: Exemption list is proposed to include the category
- **WHEN** `unqualified` is added to the exemption list to make the gate pass
- **THEN** the change is rejected, and the underlying identifiers are made resolvable instead

#### Scenario: Undeclared identifier in a rarely-evaluated binding
- **WHEN** a contributor writes a binding referencing an identifier not in scope, in a property
  evaluated only under a setting nobody enables during review
- **THEN** the gate fails at build time rather than the binding throwing on a user's device

### Requirement: `unqualified` Is Enforced Per File, Against A Clean List That Only Grows
Because the `unqualified` category cannot be exempted and cannot reach zero tree-wide in this
change, enforcement for it SHALL be keyed on the file: every QML file with zero unqualified
warnings is recorded on a clean list and SHALL stay at zero, and every file not on that list
carries a recorded count that SHALL only decrease.

A category-level exemption is the device `compiler-diagnostics` uses, and it does not work here —
exempting `unqualified` would discard the only signal this capability exists to protect. The
file-level list is the same idea in the only form available: an explicit backlog that shrinks,
whose length is the remaining work.

After the singleton migration, 104 of 212 files reach zero and are locked immediately; the
remaining 108 hold 4,274 warnings, concentrated in the large editor and review pages. A new file
starts on the clean list, so the boundary moves in one direction only.

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

### Requirement: The Exemption List Is Explicit And Only Shrinks
Diagnostic categories not yet cleared SHALL be recorded as a single labelled block in the build
configuration, and that block SHALL only ever have entries removed.

The block's length is the remaining work, and the capability is complete when it is empty and
deleted. Because every category absent from it already fails the build, no count baseline or
ratchet is needed.

Each entry SHALL carry the count of occurrences it currently covers, so a reader can tell a
category with three instances from one with three thousand without running the tool.

#### Scenario: Clearing a category
- **WHEN** the last occurrence of an exempt category is fixed
- **THEN** its entry is removed from the block in the same change

#### Scenario: A cleared category regresses
- **WHEN** a contributor reintroduces a diagnostic in a category already removed from the block
- **THEN** the build fails, with no count comparison involved

### Requirement: Residual Scope Diagnostics Are Recorded, Not Hidden
The diagnostics remaining after the singleton migration — unqualified access to delegate and
file-scope identifiers such as `modelData`, `root`, `index` and `model` — SHALL be recorded as the
per-file counts of the preceding requirement, with their distinct remedy named, rather than
folded into the category exemption list as though they were the same problem.

These are 4,274 warnings with a different cause (QML's implicit component scoping) and a different
fix (`pragma ComponentBehavior: Bound`). Keeping them out of the category list keeps that list an
honest measure of one problem instead of a bucket that stops meaning anything, and keeps them
visible as a scheduled backlog rather than a permanent carve-out.

#### Scenario: Reader asks what is left
- **WHEN** a contributor reads the recorded state after the migration lands
- **THEN** the scope backlog is visible as per-file counts with `pragma ComponentBehavior: Bound`
  named as its remedy, and is distinguishable from the category exemptions

#### Scenario: Scope backlog is not silently absorbed
- **WHEN** a change would make the gate pass by adding `unqualified` to the category exemption
  list on the grounds that the scope warnings are unavoidable
- **THEN** the change is rejected in favour of the per-file clean list

