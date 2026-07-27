## Context

`qmllint` currently reports **11,565 `unqualified` warnings** across 212 QML files. Measured
precisely (parsing each warning's column to recover the flagged token):

| Cause | Count | Share |
|---|---:|---:|
| Registered context property (39 names) | 7,167 | 61% |
| C++-registered QML type (27 names) | 124 | 1% |
| Delegate / file-scope identifiers (`modelData`, `root`, `index`, `model`, …) | 4,274 | 37% |

Three names — `TranslationManager` (3,459), `Settings` (1,335), `MainController` (879) — are half
the total on their own. Other categories, where real defects live, are invisible underneath:
310 `missing-property`, 37 `unused-imports`, 27 `import`, 25 `index`, and single instances of
`incompatible-type`, `equality-type-coercion` and `unresolved-type`.

There is no qmllint target in `CMakeLists.txt`, no `.qmllint.ini`, and no CI step. The tool is run
by hand. That is how [#1661](https://github.com/Kulitorum/Decenza/pull/1661) shipped in 2.0.1: a
`ReferenceError` in `Theme.qml` that qmllint *did* report, as 1 of 88 identical warnings in that
file, the other 87 being false positives from context properties.

Favourable starting conditions: **199 of 212 QML files already `import Decenza`**, and the
generated `qmldir` already declares the module's own types (`singleton Theme 1.0 qml/Theme.qml`),
which resolve correctly today. The exposed C++ objects are stack locals in `main()` declared
before `QQmlApplicationEngine engine` (line 1958), so they already outlive the engine.

## Goals / Non-Goals

**Goals:**
- Make a genuine undeclared-identifier error visible in qmllint output without reading past
  thousands of false positives.
- Put the check somewhere it can fail a build, not only a developer's terminal.
- Keep the `unqualified` category enforced — it is the only detector for the failure mode that
  motivated this work.

**Non-Goals:**
- Clearing the 4,274 delegate/scope warnings. Different cause, different remedy
  (`pragma ComponentBehavior: Bound`), and doing both at once makes neither reviewable. Recorded
  and counted here; scheduled separately.
- Changing any QML call site's *expression*. `Settings.theme.x` stays exactly that.
- Adopting qmllint's auto-fix, or reformatting QML.

## Decisions

### D1: Migrate context properties to QML singletons, rather than teaching the linter about them

**Chosen:** register the affected objects with `qmlRegisterSingletonInstance` under the `Decenza`
URI. D2 narrows *which* — ten of them, not all 39.

Alternatives considered:

- **Hand-written `.qmltypes` stub describing the context properties.** Rejected: a second
  declaration of the same truth, maintained by hand, that goes stale silently — and it would fix
  only qmllint, leaving `qmlcachegen` and the language server blind.
- **A wrapper script that filters known context-property names out of qmllint output.** Rejected
  for the same staleness reason, plus it makes the gate's correctness depend on a regex over tool
  output. It would also hide a *genuine* typo of a context-property name — `Setttings.x` filtered
  as noise is precisely the bug class this change exists to catch.
- **`// qmllint disable unqualified`.** Rejected outright; see the spec requirement forbidding it.

The migration is cheap at the call sites because the QML-visible name does not change: 199 of 212
files already import the module, so 13 files gain an import and the expressions are untouched.
It also removes a real runtime cost — context-property lookups walk the context chain and defeat
`qmlcachegen`'s compile-time resolution, whereas singletons are resolved statically.

### D2: Scope by files unlocked, not by call-site count

Ranking the registrations greedily by how many files each one takes to zero unqualified warnings
gives a sharply different answer from ranking them by usage:

| # | Name | Files unlocked | Cumulative clean |
|---|---|---:|---:|
| — | (today) | — | 52 |
| 1 | `TranslationManager` | 12 | 64 |
| 2 | `Settings` | 15 | 79 |
| 3 | `AccessibilityManager` | 8 | 87 |
| 4 | `MainController` | 9 | 96 |
| 5 | `ProfileManager` | 3 | 99 |
| 6–9 | `MachineState`, `MachineStateType`, `MarkdownRenderer` | 1 each | 102 |
| 10 | `EmojiAssets` + `TemperatureDisplay` (pair) | 1 | 103 |

**The remaining 56 names buy exactly one further file.** Migrating them is churn priced as
progress. This is the single most valuable measurement in the change and it inverts the original
plan, which batched by call-site count and would have spent most of its effort on names that
unlock nothing.

Note the last row: greedy single-name ranking stops at 102 because it can only see names that
*alone* complete a file. `Theme.qml` is blocked by three (`Settings`, `EmojiAssets`,
`TemperatureDisplay`) and so never surfaces. It has to be added deliberately — and it is the file
whose silent `ReferenceError` shipped in 2.0.1, which makes it the acceptance test for the whole
change rather than an afterthought.

### D3: `ScaleDevice` and `Refractometer` are deferred, not solved

These two are **re-pointed at runtime**: `setContextProperty("ScaleDevice", …)` appears 10 times
and `Refractometer` 4 times, swapping as scales connect, disconnect, and fall back to `FlowScale`.
A registered singleton instance cannot be swapped, so they cannot migrate by changing the
registration call.

**Chosen: defer them.** Under D2's measurement they unlock zero files — every file that uses them
is dirty for other reasons — so the façade work would be the highest-risk item in the change while
moving the number that matters by nothing. They keep per-file ceilings like any other dirty file.

The façade remains the right eventual answer, and it carries a real win: re-assigning a context
property dirties every binding in the root context, so each scale connect/disconnect currently
triggers an app-wide re-evaluation. That is a reason to do it someday, not a reason to do it
inside a change about linter signal.

Recorded so a later reader does not rediscover the idea and assume it was overlooked.

**The other multiply-set names** (`DE1Device`, `Settings`, `TemperatureDisplay`,
`IsDebugBuild`, `GHCSimulator`, each set twice) are believed to be mutually exclusive startup
paths, not runtime swaps. Each must be *verified* before migrating, not assumed — a name that
turns out to be swapped and is migrated anyway produces a QML object frozen on the wrong backend,
which fails silently.

### D4: Enforcement modelled on `compiler-diagnostics`, not invented

The existing `compiler-diagnostics` capability already settled this argument for C++: flags on
from day one, uncleared classes carried as an explicit `-Wno-<name>` block that only shrinks, no
CI count baseline. The QML gate takes the same shape so there is one idea in the codebase rather
than two. Each exemption entry carries its current occurrence count, so the block reads as a
backlog with sizes rather than a list of names.

### D5: `unqualified` is enforced per file, because the category device does not work for it

An earlier draft of this design had a hole worth recording, because it is the kind that survives
review: it required both that `unqualified` never be exempted *and* that the gate be green from
day one. Those cannot both hold — 4,274 unqualified warnings survive the migration, so the gate
would have been permanently red, which is not a gate.

The measurement that resolves it: after the D2 migration, **103 of 212 files reach zero**
unqualified warnings. The residue is concentrated, not diffuse — 108 files, with the top ten (the large editor
and review pages, `ProfileEditorPage` at 309, `PostShotReviewPage` at 303, `SteamPage` at 202)
holding roughly half of it.

**Chosen:** a clean list of files held at zero, plus recorded per-file ceilings for the rest. Both
move one way only. This is `compiler-diagnostics`' shrinking-backlog idea keyed on file instead of
category, which is the only form available when the category itself cannot be carved out.

What this buys immediately: `Theme.qml` is on the clean list from day one — its 87 current
warnings are all context properties and all disappear. The bug that motivated this change would
have failed the gate. So would a repeat of it in any of the other 103 clean files, which include
`Theme.qml`, `main.qml`'s components, and every file added from now on.

Rejected: a single tree-wide count baseline. It lets a regression in a clean file hide behind a
fix elsewhere, which is exactly the accounting `compiler-diagnostics` refuses.

### D6: Stage the migration one name at a time

One commit per batch of related registrations, each with the app launched and exercised. The
failure mode being guarded against is specific: a name that fails to register resolves to
`undefined` in QML and throws only when the binding evaluates — the same silent, delayed shape as
the bug that started this. A single 39-name commit would make bisecting that impossible.

Order: leaf objects with small QML surfaces first (`EmojiAssets`, `AppVersion`, `IsDebugBuild`),
then the high-traffic ones (`TranslationManager`, `Settings`, `MainController`), then the two
façades last, since they carry the most risk.

## Risks / Trade-offs

- **A migrated name silently resolves to `undefined`** → after each batch, run the app and check
  the log for QML TypeErrors, not just that it builds. This is the bug class the change exists to
  prevent; reintroducing it during the fix would be an unusually poor outcome.
- **The `ScaleDevice` façade drifts from the backend's property surface** → derive the façade's
  properties from the `ScaleDevice` base class rather than from what QML happens to read today, so
  a property added to the base and not forwarded is a compile-time gap, not a runtime `undefined`.
- **Scale swap loses a binding update** → the façade must re-emit every forwarded property's NOTIFY
  on swap. A missed signal shows as a stale reading after reconnect, which looks like a scale bug
  and would be diagnosed in the wrong subsystem for a long time.
- **`TranslationManager` is 3,459 call sites and `translate` is a `Q_PROPERTY` holding a callable**
  (per `CLAUDE.md`, after an earlier fix) → verify a language change still re-evaluates bindings
  after migration; `tests/tst_translationreactivity.cpp` is the existing guard and must stay green.
- **The gate lands red on the 4,274 scope warnings** → resolved by D5's per-file model, not by
  exempting the category. They become recorded per-file ceilings with `pragma ComponentBehavior:
  Bound` named as the remedy.
- **Per-file ceilings rot into permanent budgets** → the 108 dirty files are the visible backlog
  and a file only ever moves toward the clean list. If a year passes with the list unchanged, that
  is the signal the scope work needs scheduling, not that the ceilings were the wrong device.
- **CI time** → a full-tree qmllint run exceeded two minutes locally. Measure before choosing
  between "every push" and a narrower trigger; a gate slow enough to be routed around is not a
  gate.

## Migration Plan

1. Add the qmllint target and the exemption block sized to today's counts. Gate is live and green
   from the first commit.
2. Migrate registrations in batches (D4 order), removing exemption entries as counts reach zero.
3. Build the two façades; migrate `ScaleDevice` and `Refractometer`.
4. Add the import to the 13 files lacking it.
5. Update `CLAUDE.md` and `docs/CLAUDE_MD/QML_GOTCHAS.md`, whose current qmllint instruction
   describes a command whose output is unreadable.

**Rollback:** each batch is one commit and independently revertable; the gate's exemption block is
the only shared state, and re-adding an entry restores the prior threshold.

## Open Questions

- Does the `ScaleDevice` façade belong in `src/scale/` alongside the base class, or beside the
  other QML-facing adapters? Affects nothing functional; decide when the file is created.
- Should the gate run on every push or only on PR branches? Depends on the measured runtime from
  the risk above.
- `MachineStateType` (122 warnings) is a `qmlRegisterUncreatableType` enum holder, not a context
  property. Check whether it resolves once the module import is present in the 13 files, or needs
  its own treatment.
