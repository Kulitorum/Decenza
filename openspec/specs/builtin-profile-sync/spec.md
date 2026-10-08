# builtin-profile-sync Specification

## Purpose
Covers equivalence of built-in profiles shared between Decenza and Decaid: the machine-observable comparison rule, keeping shared profiles content-equivalent with de1app as the reference, and classifying divergences by cause rather than suppressing them.

## Requirements
### Requirement: Cross-app profile equivalence is machine-observable

Two copies of a profile SHALL be considered equivalent when they cause the DE1 to do the same thing, not when their files agree textually. The comparison SHALL normalise these before declaring a difference: absent, "" and 0 are the same; the axis a frame's pump does not drive is not compared; a limiter with value 0 equals an absent limiter; numbers compare numerically; exit compares as (type, condition, value).

#### Scenario: Encoding differences are not divergences

- **WHEN** two copies of a profile differ only in omitted zeroes, a zero-valued limiter, inactive-axis encoding, or numeric formatting
- **THEN** they are reported as equivalent

#### Scenario: A stop-target difference is a divergence

- **WHEN** two copies of a profile have identical frames but different target weight or target volume
- **THEN** they are reported as divergent, because the shot ends differently

### Requirement: Stop fields are part of profile equivalence
The scalar fields that decide when a shot stops SHALL be part of the comparison, not metadata. Frames alone are insufficient for equivalence.


#### Scenario: Identical frames, different stop weight
- **WHEN** two copies of a profile have identical frames but stop at different weights
- **THEN** they are not reported as equivalent

### Requirement: Common built-in profiles are content-equivalent across apps

The bundled profiles Decenza and Decaid share SHALL produce the same extraction: importing a shared profile into either app SHALL yield functionally identical frames and stop at the same point. Divergences SHALL be resolved case-by-case against de1app as the reference, never against de1app's stored advanced_shot frames for a profile type that derives its frames.

#### Scenario: A reconciled shared profile makes the same coffee in either app

- **WHEN** a built-in common to Decenza and Decaid has been reconciled
- **THEN** the two apps' copies parse to functionally-equal frames and the same stop targets
- **AND** the divergence resolution is recorded in the audit with its chosen reference source

### Requirement: Reconciliation is bidirectional and re-checked
Neither app SHALL be automatically authoritative: a blanket "de1app wins" rule MUST NOT be carried across to Decaid as an assumption. Outcomes of the comparison MUST be re-established, not assumed, when it is re-run.


#### Scenario: Re-run re-establishes outcomes
- **WHEN** the comparison is re-run
- **THEN** earlier findings are checked again rather than carried forward as assumptions

### Requirement: Divergences are classified by cause, never suppressed by category

Every divergence the comparison finds SHALL be reported and classified by its cause. A divergence with a known upstream cause SHALL be recorded as such rather than queued as a Decenza defect, and one with no established cause SHALL be surfaced as unexplained. No profile family SHALL be excluded from the comparison in advance.

#### Scenario: A known upstream cause is classified, not queued as a defect

- **WHEN** the comparison reports a divergence whose cause is an identified upstream defect
- **THEN** it is recorded with that cause rather than queued as a Decenza defect
- **AND** the same profile's parity against de1app is still enforced

#### Scenario: An unexplained divergence is surfaced

- **WHEN** the comparison reports a divergence with no established cause
- **THEN** it is reported as unexplained rather than filtered out or attributed by assumption

<!--
REMOVED from this spec: "A settings_2a profile carrying stale advanced_shot frames
is caught". That behaviour shipped in fix-de1app-profile-drift and is now specified
as "Simple profiles derive frames from their scalars" in
openspec/specs/de1app-profile-parity/spec.md. Restating it here would give one
behaviour two owners, which is how the two drift apart.

Also deliberately absent: requirements for the 3-way tooling and the Decaid
regression gate. Both were descoped — see design D3 and D5 — so specifying them
would describe behaviour this change does not deliver.
-->

#### Scenario: A-Flow profiles are compared
- **WHEN** the Decaid comparison runs over A-Flow and D-Flow profiles
- **THEN** they are included, and any divergence is classified by cause rather than filtered out
