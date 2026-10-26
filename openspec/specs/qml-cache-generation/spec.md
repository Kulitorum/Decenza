# qml-cache-generation Specification

## Purpose
Defines how the build turns QML sources into compiled cache units: an edit recompiles only what its generated output actually changed, QML work is not serialized behind unrelated steps, and stale units in any QML module are detected rather than silently shipped into a local build.

## Requirements

### Requirement: Unchanged generated output does not trigger recompilation
In the native app module, when re-running the QML cache generator produces output byte-identical to what is already on disk, the build SHALL NOT recompile or relink anything because of that unit.

#### Scenario: Comment-only edit
- **WHEN** one leaf QML file gains only a trailing comment and an incremental build runs
- **THEN** at most that file's own native QML cache object SHALL be recompiled

#### Scenario: Revert restores without a full recompile
- **WHEN** an edit that changed N units' generated output is reverted
- **THEN** the rebuild SHALL recompile at most those N units

### Requirement: Native cross-file invalidation is preserved
Editing a QML or JS source of the native app module SHALL still regenerate every native cache unit whose generated output depends on it, in the same incremental build.

#### Scenario: Signature change reaches native callers
- **WHEN** a function's type annotation in `Theme.qml` changes and an incremental build runs
- **THEN** every native cache unit that calls it SHALL be regenerated and recompiled

### Requirement: The Wasm client has cross-file invalidation
Editing a QML or JS source of the Wasm client's QML module SHALL regenerate every Wasm cache unit whose generated output depends on it, in the same incremental build, and SHALL recompile only the units whose generated output changed.

#### Scenario: Signature change reaches Wasm callers
- **WHEN** a function's type annotation in `Theme.qml` changes and an incremental paired build runs
- **THEN** the Wasm build SHALL recompile the same callers the native build does, and the consistency check SHALL find no stale Wasm unit without any manual step

### Requirement: QML declaration rules are enforced per pull request
A build-free check SHALL run on every pull request that touches QML and SHALL fail when a QML function with typed parameters has no return annotation, or when a root object declares a signal above a property.

#### Scenario: A new signal above a property
- **WHEN** a pull request adds a root-level signal declaration above a property declaration
- **THEN** the per-PR check SHALL fail and name the file and line

### Requirement: Native QML compilation does not wait for the Wasm bundle
In a paired native/Wasm build, generating and compiling native QML cache units SHALL NOT be ordered after building, packaging or embedding the Wasm bundle. Only the native link step and steps that read the bundle SHALL depend on it.

#### Scenario: One QML edit in a paired build
- **WHEN** one QML file changes in a paired Debug build
- **THEN** native cachegen SHALL start before the Wasm child build finishes

### Requirement: QML edits do not reconfigure the build
Changing the contents of an existing QML or JS source SHALL NOT re-run CMake configuration in the native or Wasm build. Adding or removing a QML file MAY do so. The package identity SHALL still reflect the edited contents in that same build.

#### Scenario: Content edit
- **WHEN** an existing QML file's contents change and an incremental build runs
- **THEN** neither build SHALL re-run CMake, and the native and Wasm package identities SHALL match each other and differ from the previous build's

### Requirement: Cache consistency is checkable on demand
The build SHALL provide a target that regenerates every cache unit of every QML module (native, Wasm and test) into a scratch location, compares each against the unit on disk, and fails naming every unit that differs. It SHALL report how many units it compared per module, and SHALL fail if any unit could not be regenerated.

#### Scenario: Stale unit is reported
- **WHEN** a cache unit on disk differs from a fresh regeneration of the same source
- **THEN** the check SHALL fail and name that unit and its module

#### Scenario: Consistent tree passes with counts
- **WHEN** every unit matches its fresh regeneration
- **THEN** the check SHALL pass and print the compared-unit count per module

### Requirement: Tests fail on a stale compiled unit they load
The automated test suite SHALL run with Qt's AOT lookup validation set to fatal, so a test that loads a QML cache unit whose compiled lookups no longer match the live types fails, rather than passing while that unit runs interpreted.

#### Scenario: Stale test-module unit
- **WHEN** a test loads a cache unit compiled against an older `AppShell` method layout
- **THEN** that test SHALL fail with Qt's AOT validation message naming the file

### Requirement: Shipped QML passes AOT lookup validation
QML in the app SHALL be written so that every compiled unit's AOT lookups match the live types at run time, and no unit's AOT code is discarded by validation. A function with typed parameters SHALL declare its return type, and a type whose signals other units' AOT code uses SHALL declare its properties before its signals.

#### Scenario: App under the AOT validation check made fatal
- **WHEN** the app is launched with `QV4_FAIL_ON_INVALID_AOT=1` and its main screens are opened
- **THEN** it SHALL not abort with an AOT lookup validation failure

### Requirement: The native invalidation mechanism is exercised, not inspected
A build target SHALL prove the native wiring end to end by making a real edit and observing the build. A cross-file change SHALL leave every native cachegen step dirty, and a content-identical regeneration SHALL leave no other QML object to recompile. The target SHALL restore the edited file whether it passes or fails.

#### Scenario: Wiring regresses after a Qt upgrade
- **WHEN** a Qt upgrade changes how cache outputs are named or how the generator writes them, and the target is run
- **THEN** the target SHALL fail rather than report success, and the edited source SHALL be left unchanged

### Requirement: Generated QML code is unchanged by build-graph changes
The build-graph work in this capability SHALL leave the generated QML C++ byte-identical to the current output for an unchanged tree, so runtime behaviour and AOT coverage do not move.

#### Scenario: Before and after on the same commit
- **WHEN** the tree is fully regenerated before and after this change, with QML sources unchanged
- **THEN** every generated cache unit SHALL be byte-identical between the two
