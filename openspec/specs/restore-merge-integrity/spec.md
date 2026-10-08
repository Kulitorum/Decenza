# restore-merge-integrity Specification

## Purpose
Defines what a merge-mode database import must establish about its destination before it writes anything, so that a restore can neither duplicate an existing history nor silently replace one, and so its reported outcome tells the two apart.

## Requirements

### Requirement: A merge import SHALL establish the destination's true row count before writing

Merge mode SHALL decide whether each incoming shot is new from a pre-existing-shot read whose success is verified; a failed read SHALL NOT be treated as evidence that the destination is empty. If that read fails for any reason, the import SHALL abort without writing, leave the destination unchanged, and report a failure naming the pre-read as the cause. It SHALL NOT fall back to importing every source row.

#### Scenario: Failed pre-read aborts instead of importing everything

- **GIVEN** a destination database containing shots
- **AND** the query that enumerates existing shots fails
- **WHEN** a merge-mode import runs
- **THEN** no rows SHALL be inserted into the destination
- **AND** the destination SHALL contain exactly the shots it held before
- **AND** the reported result SHALL be a failure identifying the pre-read as the cause

#### Scenario: Genuinely empty destination imports normally

- **GIVEN** a destination database with no shots
- **AND** the pre-read succeeds and returns zero shots
- **WHEN** a merge-mode import of a source containing shots runs
- **THEN** every source shot SHALL be imported
- **AND** the result SHALL report that the destination was empty before the import

### Requirement: A merge import SHALL refuse a state that would duplicate an existing history

Merge mode SHALL independently count the destination's shots and compare that count with the existing-shot count from the de-duplication pre-read. When the destination is non-empty but the pre-read found none, the import SHALL abort without writing and report the disagreement, including both counts. This check SHALL NOT apply to replace mode, which clears the destination first.

#### Scenario: Non-empty destination with an empty pre-read is refused

- **GIVEN** a destination database containing shots
- **AND** the de-duplication pre-read reports zero existing shots
- **WHEN** a merge-mode import runs
- **THEN** the import SHALL abort before inserting any row
- **AND** the destination SHALL be left unchanged
- **AND** the reported failure SHALL state both the counted destination rows and the zero the pre-read found

#### Scenario: Agreeing counts proceed

- **GIVEN** a destination database containing shots
- **AND** the pre-read reports the same number of existing shots as a direct count
- **WHEN** a merge-mode import runs
- **THEN** the import SHALL proceed
- **AND** source shots already present SHALL be skipped rather than inserted again

#### Scenario: Replace mode is not subject to the check

- **GIVEN** a destination database containing shots
- **WHEN** a replace-mode import runs and clears the destination first
- **THEN** the empty-destination state SHALL NOT be reported as a disagreement
- **AND** the import SHALL proceed

### Requirement: An import SHALL report an outcome that distinguishes its cases

The result of an import SHALL report how many shots the destination held before the import, and how many were inserted, skipped as already present, failed, and remapped. A count of zero SHALL be reported explicitly rather than omitted, so the result alone shows whether the import merged into a populated or an empty destination.

#### Scenario: Merging into an empty destination is distinguishable from merging into a populated one

- **WHEN** a merge import inserts N shots into an empty destination
- **AND** a separate merge import inserts N shots into a destination that already held M shots
- **THEN** the two reported results SHALL differ
- **AND** each SHALL state the destination's pre-import shot count

#### Scenario: Reference remapping is reported

- **WHEN** an import remaps stored references from source shot ids to destination shot ids
- **THEN** the result SHALL state how many references were remapped
- **AND** SHALL state how many were cleared because their source shot was not imported

### Requirement: A refused import SHALL be surfaced to the user, not only logged

When an import aborts under any requirement in this capability, the surface that initiated it — backup restore, device-to-device migration, or the backup endpoint — SHALL present a failure the user can act on, naming what was inconsistent. The destination SHALL remain usable and unchanged.

A refusal SHALL NOT be reported as a completed restore.

#### Scenario: Restore refuses and says why

- **GIVEN** a user restores a backup
- **WHEN** the import aborts because the destination's counts disagree
- **THEN** the user SHALL see a failure message describing the inconsistency
- **AND** the user SHALL NOT see a success or completion message
- **AND** the existing shot history SHALL remain intact
