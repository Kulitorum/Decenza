## ADDED Requirements

### Requirement: Filling in an empty identity component SHALL NOT fork
Copy-on-write forking SHALL apply to identity *changes* only. When every component of the identity tuple that differs was EMPTY on the package and now carries a value, the edit SHALL be applied in place — same package id, `inInventory` unchanged, every referencing shot, bag and recipe still pointing at it — regardless of how many shots the package has. Replacing a component that already had a value, or clearing one, SHALL still fork a used package.

Puck prep SHALL be compared as a whole canonical flag string, so adding a technique to an existing routine is a change and not enrichment.

The distinction is what the edit means: recording burrs, a basket, or a puck-prep routine the package always had is the user describing existing gear rather than swapping it, and forking there retires the package the entire shot history hangs off.

#### Scenario: Recording burrs on a long-used grinder
- **WHEN** a package with recorded shots and no burrs recorded has burrs filled in
- **THEN** the package SHALL be edited in place, keep its id, stay in inventory, and keep every shot pointing at it

#### Scenario: Swapping burrs still forks
- **WHEN** a package with recorded shots has its burrs changed from one named set to another
- **THEN** a new package SHALL be forked under copy-on-write, so the earlier shots keep the burrs they were pulled on

#### Scenario: Clearing a component is a change, not enrichment
- **WHEN** a package with recorded shots has a named identity component cleared
- **THEN** the edit SHALL fork rather than being applied in place

#### Scenario: Adding a technique to an existing puck prep forks
- **WHEN** a package with recorded shots has a second puck-prep flag set alongside an existing one
- **THEN** the edit SHALL fork, because the earlier shots were pulled with the shorter routine

### Requirement: Two packages SHALL be mergeable into one
The system SHALL provide a merge that folds a source package into a target package by explicit id. Every shot, bag and recipe referencing the source SHALL be repointed at the target; supersession pointers naming the source SHALL name the target instead; the target SHALL be returned to inventory with no supersession pointer at the deleted row; and the source package row and its items SHALL be deleted. The merge SHALL be atomic — a failure at any step SHALL leave both packages exactly as they were.

Merge SHALL accept a retired source or target, since undoing a fork is the case it exists for. It SHALL refuse, changing nothing, when either id is unknown or the two ids name one package, and the refusal SHALL carry a machine-readable reason.

Merge is destructive and not undoable: the source identity is gone afterwards and its shots then report the target's gear. It SHALL only be exposed where the user names both packages.

#### Scenario: Undoing a fork
- **WHEN** a grinder was split into two packages and the user merges the newer one into the package holding the history
- **THEN** all shots, bags and recipes SHALL resolve to the surviving package
- **AND** the surviving package SHALL be in inventory with no supersession pointer
- **AND** the merged-away package SHALL no longer exist

#### Scenario: Lineage follows the merge
- **WHEN** a third package was superseded by the package being merged away
- **THEN** its supersession pointer SHALL name the surviving package rather than a deleted row

#### Scenario: Merge refuses an unknown or self-referencing package
- **WHEN** a merge names a package id that does not exist, or names the same package as both source and target
- **THEN** nothing SHALL be moved or deleted and the refusal SHALL carry a machine-readable reason

#### Scenario: Active package survives the merge
- **WHEN** the package merged away was the active equipment
- **THEN** the surviving package SHALL become the active equipment

### Requirement: Packages split by a pre-enrichment fork SHALL be healed once on upgrade
On upgrade the system SHALL fold together each pair of packages where one is superseded by the other AND the two differ only in that the superseded package has no burrs while its successor does — grinder brand and model, basket brand and model, and the puck-prep string all being equal. The successor SHALL survive. Pairs not matching that signature — burrs changed between two named sets, a component cleared, a basket or puck-prep difference, or two similar packages with no supersession between them — SHALL be left untouched.

The heal SHALL run inside the migration's transaction, and the schema version SHALL NOT advance if it fails, so a failed heal retries on the next launch rather than being recorded as done. Each fold SHALL be logged with both package ids and the number of shots, bags and recipes moved.

When the active equipment selection names a package folded away, it SHALL be moved to the surviving package.

#### Scenario: A grinder split by recording burrs is reunited
- **WHEN** a database is upgraded that holds a retired package with no burrs superseded by an otherwise identical package with burrs
- **THEN** the two SHALL become one package carrying all of the history, and the older row SHALL no longer exist

#### Scenario: A burr swap survives the heal
- **WHEN** the superseded package and its successor both name burrs
- **THEN** both packages SHALL remain, because the shots on each were pulled on different burrs

#### Scenario: Lookalike packages with no lineage survive the heal
- **WHEN** two packages share a grinder brand and model, one with burrs and one without, and neither supersedes the other
- **THEN** both SHALL remain — they are two grinders the user owns, not a fork

#### Scenario: Re-running the heal changes nothing
- **WHEN** the heal runs a second time on an already-healed database
- **THEN** it SHALL fold nothing and report zero

### Requirement: Hard delete SHALL count every reference to a package
The pre-check that decides whether a package may be hard-deleted SHALL count references from `shots`, `coffee_bags` and `recipes`, plus packages superseded by it. A package with any reference SHALL be soft-deleted instead, so no row is left pointing at an id that no longer exists.

#### Scenario: A recipe blocks a hard delete
- **WHEN** a delete is requested for a package that only a recipe references
- **THEN** the package SHALL NOT be hard-deleted
