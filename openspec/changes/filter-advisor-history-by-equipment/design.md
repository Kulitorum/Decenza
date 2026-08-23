# Design

## Context

This change was implemented once before (PR #1852) and re-implemented (PR #1857). The
requirements in `specs/` are carried forward unchanged, because nothing about what the advisor
must do turned out to be wrong. What was wrong was the shape of the implementation.

#1852 scoped seven selection points by adding an `equipment_id` predicate at each one. It was
correct — a full trace of its selection points, conversation-key path, import/migration and
threading found no defect in the algorithm — and it still took nine rounds of review, whose
final state produced four more findings, all in the maintenance layer rather than the logic.

That is the fact this design answers. When a rule is "remember to add this predicate", every
site is free to forget independently, and each review round can only fix the site in front of
it.

## Goals / Non-Goals

**Goals**
- Make the equipment scope impossible to omit from a read that should carry it.
- Keep the three distinct scoping questions in this subsystem separable and named.
- Land net-neutral or better on source size, so the mechanism pays for itself.

**Non-Goals**
- Changing which shots the advisor *should* see. That is the spec's job and it is unchanged.
- Unifying every `equipment_id` predicate in the codebase. Two of the three questions below are
  already correct and must not be collapsed into the third.

## Decisions

### The scope is a value, not a SQL fragment

`AdviceScope` holds the bucket and emits its own predicate. A read that should be scoped takes
one as a required parameter, so the compiler asks the question the reviewer previously had to.

The alternative — a shared helper returning a predicate string — was what #1852 converged on. It
centralises the *fragment* and leaves the *decision* at the call site: nothing forces a new query
to call it, and the caller still has to supply the alias, the placeholder style and the bind.

### The bucket is embedded, not bound

`sql()` formats the `qint64` into the statement rather than emitting a placeholder. The value
comes from our own schema, so there is nothing to quote, and it removes four hazards at once: no
bind to forget, no ordinal to miscount, no named-vs-positional mismatch to fail the exec, and no
null bind to match zero rows silently.

The last one is not hypothetical. The test fixture written alongside this change bound a
default-constructed `QString`, Qt turned it into SQL `NULL`, `x = NULL` was never true, and every
fixture shot forked its own package. The embedded form cannot express that bug.

`COALESCE` on the column stays load-bearing: bucket 0 is the unpackaged pool, and `equipment_id
= 0` drops NULL rows instead of matching them.

### Three questions, not one

The subsystem asks three different things that all read as "the same grinder", and only one of
them is this scope:

| Question | Key | Where |
|---|---|---|
| May these shots be compared? | equipment package | `AdviceScope` — this change |
| Are these two rows the same real package? | grinder identity, folded | `EquipmentStorage::findPackageByGrinderIdentityStatic` |
| What is this grinder's dial resolution? | grinder model | `grinderWideNumericSettings`, `grinderWideRpms` |

The second still merges accidental forks — one real package split across two rows by a re-typed
model string — and is not in tension with the first once both say which question they answer.

The third is why "scope everything by package" is wrong. Grind step size must equal the Grind
quick-select widget's `grindStepForGrinder()` on the same screen, so it stays grinder-wide.
`adviceScope_stepSizeStaysGrinderWide()` fails if a future change crosses that line.

## Risks / Trade-offs

**A formatted value in SQL.** Safe for a `qint64` from our own schema and unsafe as a habit. The
type takes only `qint64` and exposes no string path, so the habit cannot spread through it.

**Scope creep into display filtering.** `buildFilterQuery` and the auto-favourite grouping filter
by whatever the user selected, which is legitimately grinder-wide. Narrowing those to the package
would be a regression, and the table above exists so the next reader does not.

**A newly forked package starts thin.** Intended, and the spec requires the no-history block that
makes it legible rather than silent. Until that block ships, the state is silent — which is the
gap `tasks.md` records.

## Verification

One invariant over all five advice-scoped selections, rather than one regression test per bug.
Against `main` it reports `4 of 5 advice-scoped selections pooled`, naming each and the settings
it leaked.

Both new tests were verified by breaking the code:

- With a pooling predicate, the calibration test reports Londinium at `rgs 5` — the midpoint of
  one basket's 2 and the other's 8. Not a broken-looking number; a confident, plausible one that
  is wrong for both baskets. That is the failure mode worth a test.
- Removing the scope from a query does not compile at all: `-Werror,-Wunused-parameter` rejects
  the now-unused parameter, so the filter can be misused but not silently dropped.

The fixture data mirrors real history: one grinder, one set of burrs, identical puck prep, same
bean, profile, dose and target. Decent 18g Ridged dials 8–10; Graph stepped 58→46mm dials
15–17.5. No overlap, five steps of clear air. Pooling yields a median of 10 because 33 of 39
shots are Decent, so the minority basket is handed a setting about 6.75 steps too fine behind a
large and reassuring sample.

## Open question on the carried requirements

The specs in `specs/` were written for PR #1852 and carried forward because the problem did not
change. One of them is worth deciding rather than inheriting.

### Should puck prep fork the comparison set?

`advisor-user-prompt` says a package "identifies grinder, basket and puck prep together, and
changing any one of them yields a different package", so a puck-prep edit empties the advisor's
history.

The measured case is a BASKET change: 8-10 against 15-17.5, no overlap, five steps. Nothing
measured says toggling RDT or a puck screen moves the dial like that, and puck prep is the
variable a user flips experimentally between shots. Forking on it produces "no prior shots with
this equipment set" at the moment the user most wants a with-versus-without comparison.

Candidate: scope on grinder + basket, name puck prep in the payload without forking. This is a
change to the requirement, not the implementation, so decide it before section 3 is built.

## Decided

- **Pre-upgrade conversations are thrown away.** A user with equipment gets fresh threads; the
  old ones are not continued. No notice, no migration, no recovery path.
- **A prompt rule is mitigation, not a guarantee.** "Do not cite an absent shot" reduces the
  fabrication rate and cannot enforce it. The enforceable half is the no-history block, which
  removes the vacuum the model was filling.
- **Bucket 0 pools all pre-package shots.** They have no recorded gear, so this is the best
  available answer, not a compromise worth engineering around.
