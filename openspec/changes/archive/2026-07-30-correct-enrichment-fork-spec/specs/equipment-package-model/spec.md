## MODIFIED Requirements

### Requirement: Filling in an empty identity component SHALL NOT fork
Copy-on-write forking SHALL apply to identity *changes* only. When every component of the identity tuple that differs was EMPTY on the package and now carries a value, the edit SHALL be applied in place — same package id, `inInventory` unchanged, every referencing shot, bag and recipe still pointing at it — regardless of how many shots the package has. Replacing a component that already had a value, or clearing one, SHALL still fork a used package.

Puck prep SHALL be compared as a whole canonical flag string, so adding a technique to an existing routine is a change and not enrichment.

A package with NO grinder component at all is the one exception. Such a package is deliberately grinder-less — a basket-only tea setup — and the identity model treats "no grinder" as a real, matchable value rather than as missing data. Giving it a grinder SHALL therefore fork, because its shots were pulled with nothing ground and must not begin reporting a grinder that never touched them. The exception SHALL NOT extend to an absent basket or an absent puck prep: no espresso is pulled without a basket, so an absent one is a basket nobody recorded, exactly like absent burrs.

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

#### Scenario: A grinder-less package gaining a grinder forks
- **WHEN** a package with recorded shots and no grinder component at all is given a grinder identity
- **THEN** a new package SHALL be forked, and the earlier shots SHALL stay on the grinder-less package

#### Scenario: Naming a basket that was never recorded is enrichment
- **WHEN** a package with recorded shots and no basket recorded has a basket filled in
- **THEN** the edit SHALL be applied in place, because every one of those shots used a basket that simply was not written down

### Requirement: Two packages SHALL be mergeable into one
The system SHALL provide a merge that folds a source package into a target package by explicit id. Every shot, bag and recipe referencing the source SHALL be repointed at the target; supersession pointers naming the source SHALL name the target instead; and the source package row and its items SHALL be deleted. The merge SHALL be atomic — a failure at any step SHALL leave both packages exactly as they were.

The target SHALL be returned to inventory ONLY when nothing else supersedes it, and a supersession pointer aimed at the deleted source SHALL be cleared. A target retired because a THIRD package replaced it is still genuinely retired: reviving it would put a stale duplicate back in the inventory carrying the same derived name as its own successor, and both the inventory listing and identity dedup key on the in-inventory flag alone.

Merge SHALL accept a retired source or target, since undoing a fork is the case it exists for. It SHALL refuse, changing nothing, when either id is unknown or the two ids name one package, and the refusal SHALL carry a machine-readable reason.

Merge is destructive and not undoable: the source identity is gone afterwards and its shots then report the target's gear. It SHALL only be exposed where the user names both packages.

#### Scenario: Undoing a fork
- **WHEN** a grinder was split into two packages and the user merges the newer one into the package holding the history
- **THEN** all shots, bags and recipes SHALL resolve to the surviving package
- **AND** the surviving package SHALL be in inventory with no supersession pointer
- **AND** the merged-away package SHALL no longer exist

#### Scenario: A superseded target is not revived
- **WHEN** the merge target is itself superseded by a third package
- **THEN** it SHALL keep its supersession pointer and SHALL NOT be returned to the inventory

#### Scenario: Lineage follows the merge
- **WHEN** a third package was superseded by the package being merged away
- **THEN** its supersession pointer SHALL name the surviving package rather than a deleted row

#### Scenario: Merge refuses an unknown or self-referencing package
- **WHEN** a merge names a package id that does not exist, or names the same package as both source and target
- **THEN** nothing SHALL be moved or deleted and the refusal SHALL carry a machine-readable reason

#### Scenario: Active package survives the merge
- **WHEN** the package merged away was the active equipment
- **THEN** the surviving package SHALL become the active equipment

## ADDED Requirements

### Requirement: Equipment decisions SHALL be retrievable from a submitted log
Equipment package events SHALL log under a registered `[Equipment]` subsystem marker, so one filter returns the subsystem's whole narrative from a log a user submitted or an assistant reads over MCP.

Every identity edit SHALL log which branch it took — applied in place (and whether enrichment or an unused package earned that), forked (naming both package ids and the identity before and after), or merged into an existing package. A completed merge SHALL be logged once, from a path shared by every entry point, with the number of shots, bags and recipes moved. The one-time heal SHALL log its outcome even when it merges nothing, because "ran and found nothing" and "never ran" are otherwise the same silence.

These are user-facing outcomes rather than developer detail, so they SHALL be logged at INFO.

#### Scenario: A fork is explicable after the fact
- **WHEN** an identity edit forks a package
- **THEN** the log SHALL name both package ids and what changed, so a reader can tell why a grinder's history detached without reproducing the edit

#### Scenario: A heal that finds nothing still reports
- **WHEN** the one-time heal runs and matches no packages
- **THEN** it SHALL log that it completed with a count of zero
