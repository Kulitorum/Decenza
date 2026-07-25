# builtin-profile-sync Specification

## Purpose
TBD - created by archiving change sync-builtin-profiles. Update Purpose after archive.
## Requirements
### Requirement: Cross-app profile equivalence is machine-observable

Two copies of the same profile SHALL be considered equivalent when they cause the DE1 to do the same thing, not when their files agree textually. The comparison SHALL normalise the following before declaring a difference:

- absent, `""` and `0` are the same value;
- the axis a frame's pump does not drive is not compared, because the DE1 ignores it;
- a `limiter` with `value == 0` equals an absent `limiter`;
- numbers compare numerically, never as strings — `"8.00"` equals `"8.0"`;
- `exit` compares as (type, condition, value) under the same numeric rule.

This definition is load-bearing rather than stylistic. A structural JSON diff over the 63 profiles common to Decenza and reaprime reports 55 of them differing; under this relation the true count is 11, and the 630-odd remaining rows are three encoding conventions — an omitted zero `weight`, a no-op zero-valued `limiter`, and `""` versus `0.00` on the inactive axis. A comparison that cannot tell those apart buries the real findings.

The scalar fields that decide when a shot **stops** are part of the comparison, not metadata. Frames alone are insufficient: a profile can carry identical frames and still stop at a different weight.

#### Scenario: Encoding differences are not divergences

- **WHEN** two copies of a profile differ only in omitted zeroes, a zero-valued limiter, inactive-axis encoding, or numeric formatting
- **THEN** they are reported as equivalent

#### Scenario: A stop-target difference is a divergence

- **WHEN** two copies of a profile have identical frames but different target weight or target volume
- **THEN** they are reported as divergent, because the shot ends differently

### Requirement: Common built-in profiles are content-equivalent across apps

The bundled profiles Decenza and reaprime share SHALL produce the same extraction — importing a shared profile into either app yields functionally-identical frames and stops at the same point. Divergences SHALL be resolved case-by-case against de1app as the reference, never against de1app's stored `advanced_shot` frames for a profile type that derives its frames.

Reconciliation is **bidirectional in principle**: neither app is automatically authoritative, and a blanket "de1app wins" rule governed the de1app leg of this work and MUST NOT be carried across to reaprime as an assumption.

In practice the audit found Decenza the more faithful side in all 11 divergent cases, each traceable to one of two upstream mechanisms — de1app writing `advanced_shot` out of the global `::settings` array, and de1app issue #350 shadowing the A-Flow profiles. That outcome is a finding, not a rule: it was reached case-by-case and MUST be re-established, not assumed, if the comparison is re-run.

#### Scenario: A reconciled shared profile makes the same coffee in either app

- **WHEN** a built-in common to Decenza and reaprime has been reconciled
- **THEN** the two apps' copies parse to functionally-equal frames and the same stop targets
- **AND** the divergence resolution is recorded in the audit with its chosen reference source

### Requirement: Divergences are classified by cause, never suppressed by category

Every divergence the comparison finds SHALL be reported and classified by its cause. A divergence with a known upstream cause SHALL be recorded as such rather than queued as a Decenza defect, and a divergence with no established cause SHALL be surfaced as unexplained rather than filtered out.

No profile family SHALL be excluded from the comparison in advance. An earlier draft of this spec excluded A-Flow and D-Flow from the reaprime comparison on the grounds that reaprime was "believed broken" for those editor types. The measurement disproved the premise: reaprime's A-Flow files are byte-faithful copies of de1app's stale 6-frame distribution snapshot — de1app issue #350, an upstream bug with a filed report and a known fix — and D-Flow is unaffected, its one common profile comparing equivalent. Excluding them would have suppressed the clearest signal the comparison produced.

Classification reaches the same practical outcome as exclusion — a known-upstream difference is not treated as a Decenza defect — without the false premise or the blind spot.

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

Also deliberately absent: requirements for the 3-way tooling and the reaprime
regression gate. Both were descoped — see design D3 and D5 — so specifying them
would describe behaviour this change does not deliver.
-->

