# Design

## One rule, two implementations, one of them narrower

The live enrichment predicate (`equipmentstorage.cpp:1547-1553`) and the heal
(`:1386-1404`) answer the same question — *did this edit only add information?* —
and only the first one answers it correctly. The fix is to make the second match
the first, and the strongest version of that is to **share the shape rather than
re-derive it**, since a second divergence is exactly what happened here.

| component | live rule | heal today | heal after |
|---|---|---|---|
| grinder brand | empty→named ok | must be equal | empty→named ok |
| grinder model | empty→named ok | must be equal | empty→named ok |
| grinder burrs | empty→named ok | **the only test** | empty→named ok |
| basket brand | empty→named ok | must be **equal** | empty→named ok |
| basket model | empty→named ok | must be **equal** | empty→named ok |
| puck prep | empty→named ok | must be **equal** | empty→named ok |

"Equal" is not a stricter form of "empty→named"; it is the inverse for a component
that was absent. That is why the heal reported success while skipping the case it
was written for.

## Where the predicate goes

The heal's scan is SQL over a package pair; the live rule is C++ over an
`EquipmentPackageView`. They cannot literally share a function without loading
both packages per candidate pair.

Two options:

1. **Widen the SQL.** Each `X = Y` becomes `(X = '' OR X = Y)`, per component, with
   the existing `LOWER(TRIM(IFNULL(...)))` folding retained. Cheapest, and the scan
   stays one statement.
2. **Load and reuse.** Select candidate lineage pairs on grinder brand/model only,
   then load both `EquipmentPackageView`s and evaluate a shared `isEnrichmentOf()`
   helper that the live rule also calls.

Prefer (2) if the shared helper falls out cleanly, because a single definition is
what stops this drifting a third time — CLAUDE.md's centralise rule, and this file
is the case study for it. Fall back to (1) if the load-per-pair proves awkward;
inventories are tens of rows, so cost is not the deciding factor.

Whichever is chosen, the folding used by the heal must stay Unicode-correct.
SQLite's `LOWER()` is ASCII-only, which is why identity matching moved its
comparison into C++ (`equipmentstorage.cpp:1096-1103`) — option (2) inherits that
correctness for free, option (1) does not.

## Migration

Migration 35 stamped success on databases that reached it, so its version cannot
be reused. A new migration runs the widened heal.

Databases already at 35 had their burr forks healed and keep them healed; the new
pass then catches any basket or puck-prep fork migration 35 skipped. Databases
below 35 — which is nearly everyone, since stable v2.0.0 predates migration 35 —
run 35 and then the new one. Running both is harmless: the widened predicate is a
superset, and a second pass over an already-folded pair finds nothing.

Data-only, and gated on the backfill landing, per CLAUDE.md's rule on what a
migration may stamp.

## What is deliberately not being done

A `grinders` table. Proposed, reviewed, killed. Two independent passes measured
today's model-string scope against a `grinder_id` scope on both real databases and
got the same 1020 shots, the same 28 distinct settings and the same 0.25 step —
because the step already pools every package sharing a grinder model. The change
would additionally have: split burr-enrichment forks that currently share a step
history (reintroducing #1713's symptom), collapsed six distinct remembered dial
positions into one, and deleted the heal this change repairs — at a moment when
the stable release has never run it.

The recurring error worth naming: the fork on the maintainer's device was read as
proof the schema could not express "one grinder, two baskets". It is not. There is
one basket; the app forked before a rule existed to tell it so.
