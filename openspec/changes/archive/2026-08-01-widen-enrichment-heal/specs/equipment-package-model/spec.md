# equipment-package-model (delta)

## MODIFIED Requirements

### Requirement: Packages split by a pre-enrichment fork SHALL be healed once on upgrade

On upgrade the system SHALL fold together each pair of packages where one is
superseded by the other AND the difference between them is **enrichment only** —
that is, every component whose value differs was EMPTY on the superseded package
and carries a value on its successor. The successor SHALL survive.

The set of components tested SHALL be the same set the live enrichment rule
tests: grinder brand, grinder model, grinder burrs, basket brand, basket model,
and the canonical puck-prep string. The heal exists to apply that rule
retroactively, so any component the rule treats as enrichable SHALL be treated as
enrichable here. Testing only the burrs — and additionally requiring basket and
puck prep to be EQUAL, which is the inverse of enrichment for those components —
left a fork caused by recording a basket unhealed while reporting that nothing
needed healing.

Pairs not matching that signature SHALL be left untouched: a component whose value
CHANGED between two non-empty values (a burr swap, a different basket, an altered
puck-prep routine), a component that was CLEARED, and two similar packages with no
supersession between them.

A superseded package with NO grinder component SHALL NOT be folded into a
successor that has one. That exception already governs the live rule — a
grinder-less package is a deliberate basket-only tea setup, and its shots were
pulled with nothing ground — and it SHALL apply identically here.

The heal SHALL run inside the migration's transaction, and the schema version
SHALL NOT advance if it fails, so a failed heal retries on the next launch rather
than being recorded as done. Each fold SHALL be logged with both package ids, the
components that were filled in, and the number of shots, bags and recipes moved.

When the active equipment selection names a package folded away, it SHALL be moved
to the surviving package.

#### Scenario: A grinder split by recording burrs is reunited
- **WHEN** a database is upgraded that holds a retired package with no burrs superseded by an otherwise identical package with burrs
- **THEN** the two SHALL become one package carrying all of the history, and the older row SHALL no longer exist

#### Scenario: A grinder split by recording a BASKET is reunited
- **WHEN** a retired package with no basket and no puck prep is superseded by a package with the same grinder that names both
- **THEN** the two SHALL become one package carrying all of the history, because a package that recorded no basket was still pulling shots through one

#### Scenario: Several components filled in at once
- **WHEN** the superseded package lacks both a basket and a puck prep and its successor names both
- **THEN** the pair SHALL still be folded — enrichment is not limited to one component per fork

#### Scenario: A burr swap survives the heal
- **WHEN** the superseded package and its successor both name burrs, and the two differ
- **THEN** both packages SHALL remain, because the shots on each were pulled on different burrs

#### Scenario: A changed basket survives the heal
- **WHEN** the superseded package names one basket and its successor names a different one
- **THEN** both packages SHALL remain — that is a swap, not a component being written down for the first time

#### Scenario: Lookalike packages with no lineage survive the heal
- **WHEN** two packages share a grinder, one more completely specified than the other, and neither supersedes the other
- **THEN** both SHALL remain — with no supersession there is no fork to undo

#### Scenario: A grinder-less package is not folded into one with a grinder
- **WHEN** the superseded package has no grinder component at all and its successor has one
- **THEN** both SHALL remain, because those shots were pulled with nothing ground

#### Scenario: Re-running the heal changes nothing
- **WHEN** the heal runs a second time on an already-healed database
- **THEN** it SHALL fold nothing and report zero
