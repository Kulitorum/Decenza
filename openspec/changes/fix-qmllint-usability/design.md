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

**Chosen:** declare the affected classes with `QML_ELEMENT` + `QML_SINGLETON` under the `Decenza`
URI, **plus one explicit `qml_register_types_Decenza()` call in `main.cpp`**. D2 narrows *which*
— ten of them, not all 39.

**CORRECTED after implementing 3.1. The original text of this decision said "register with
`qmlRegisterSingletonInstance`", and that cannot work.** Both halves were wrong, and each failed
silently in a different way:

- **`qmlRegisterSingletonInstance` is a runtime call, so `qmltyperegistrar` never sees it.** It
  therefore cannot put the type in the module's generated `Decenza.qmltypes`, which is qmllint's
  only source of truth about C++ types — that file was literally `Module {}` before this change.
  A migration built on it would have moved zero warnings, which is the entire point of the change.
  Only the `QML_*` macros, read out of the moc output at build time, reach the linter.
- **The macros alone then fail at RUNTIME in this app**, and produce no error of any kind. Qt
  registers a module's declarative types lazily on first import, behind this guard
  (`qqmltypeloader.cpp:783`, identically `qqmlimport.cpp:920`):

      auto module = QQmlMetaType::typeModule(qmldir.typeNamespace(), import->version);
      if (!module)
          QQmlMetaType::qmlRegisterModuleTypes(qmldir.typeNamespace());
      // else: If the module already exists, the types must have been already registered

  `main.cpp` calls `qmlRegisterUncreatableType<…>("Decenza", 1, 0, …)` twenty-odd times *before*
  QML imports the module. Those create a type module for the URI, so at import time
  `typeModule()` returns non-null, the guard short-circuits, and the generated registration
  function is never invoked. Qt's comment asserts the types "must have been already registered";
  for a module that mixes runtime and declarative registrations that is false. Note the lookup
  ignores the version when the import is unversioned (`import Decenza`), so *any* pre-existing
  registration for the URI is enough to suppress it.

The explicit `qml_register_types_Decenza()` call is the idiom Qt uses for the same situation in
`qtdeclarative/tools/qml/main.cpp`. It is a **one-time** cost: it is already in place, and the
remaining nine names in group 3 need only their macros.

**What this cost, and the lesson for the rest of group 3.** The macros-only version built clean,
passed qmllint, and passed all 104 tests — while every translated string in the app was
`undefined` and the log carried 1,081 `ReferenceError: TranslationManager is not defined`. Green
build + green linter + green suite proved nothing here. Task 3.11 ("launch the app and check the
log; building is not evidence") is the only check that caught it, and it must be run for every
one of the remaining names.

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

**The "cumulative clean" column is anchored to a wrong number and the "files unlocked" column is
not.** The 52 came from a run that was OOM-killed partway and counted every file it never reached
as clean (see tasks.md 1.11); the real pre-migration figure, measured with a qmllint that can
finish the tree, is **28 of 218**. The per-name deltas were measured differently and survived
contact: `TranslationManager` was predicted to unlock 12 files and unlocked exactly 12 (28 → 40),
with `unqualified` falling 12,251 → 8,604. So read the middle column as load-bearing and the
right-hand one as **28 + the running total**, not as absolutes.

| # | Name | Files unlocked | Cumulative clean (stale anchor) |
|---|---|---:|---:|
| — | (then) | — | 52 |
| 1 | `TranslationManager` ✅ done, 28 → 40 | 12 | 64 |
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

### D2a: `Settings`' domain sub-objects carry their concrete types, and what that cost

Registering `Settings` as a singleton is only half the job. Its twelve domain sub-objects were
declared `Q_PROPERTY(QObject* mqtt ...)` so `settings.h` could forward-declare them instead of
including their headers. Resolving `Settings` made the consequence visible: qmllint could reach
`Settings` and then see nothing behind it, turning **1,079** accesses into `missing-property`
warnings. Those were never new defects — they were newly-visible blindness, and the count going
*up* on a "successful" migration is what forced this decision.

**Chosen: include the twelve headers and declare the properties with concrete types.** The
`QObject*` erasure costs four tools, not one — qmllint cannot check the property, `qmlcachegen`
cannot resolve the binding ahead of time, the QML language server cannot complete or navigate
it, and a reader of the header cannot tell what is behind `Settings.brew`. Against that, the
saving was build time, which is paid by developers and absorbed by caching, while the defects it
hides are paid by users. #1661 is what that looks like when it reaches a release.

Two alternatives were tried or considered and rejected on evidence:

- **`Q_DECLARE_OPAQUE_POINTER`** (Qt's own suggested escape hatch for an incomplete pointer type)
  compiles, satisfies qmllint, and **breaks the app**: an opaque pointer is not known to derive
  from `QObject`, so QML receives `QVariant(SettingsBrew*)` and every property and method under
  `Settings.<domain>` fails at runtime. Caught by `tst_settings::qmlChainsThroughDomainSubObjects`,
  which was written for this and now pins the behaviour permanently.
- **A separate QML-facing façade** would keep both build times and coverage, at the price of a
  second source of truth for 281 settings. This codebase already carries an explicit
  "keep the two surfaces in sync" rule for the ShotServer because that drift recurs; trading a
  linter gap for a permanent drift surface is the worse long-term deal. It is also the
  irreversible option, where typing twelve properties is not.

**Measured cost (this machine, ASan+UBSan debug, warm ccache), touching one domain header:**

| | ninja dirty set | wall clock |
|---|---:|---:|
| before (forward-declared, `QObject*`) | 310 | **25.8 s** |
| after (included, concrete types) | 439 | **60.2 s** |

Full build for reference: 122 s. **Read the wall clock, not the counts** — those are ninja's
pre-`restat` dirty set, and the real build executed 297 edges, not 439, because `restat` prunes
the chain wherever regenerated content is unchanged (see
[`BUILD_PERFORMANCE.md`](../../../docs/CLAUDE_MD/BUILD_PERFORMANCE.md)). Splitting the dirty set
by kind: 221 C++ TUs + 218 QML cache units after, against roughly 92 + 218 before. **The 218 QML
units are in both**, so they are not part of this change's cost — a domain header carries
`Q_OBJECT`, and per BUILD_PERFORMANCE.md any moc-metadata change invalidates `Decenza.qmltypes`
and with it every QML unit. The marginal cost of this decision is the **+129 C++ TUs**, ~34 s.

Two consequences worth stating, both from that doc:

- 49 of the 86 `settings.h` includers already pulled in a domain header, so the blast was already
  large. The `QObject*` trick was buying less than the rule implied.
- BUILD_PERFORMANCE.md's lever 2 — move non-QML-facing C++ out of the QML module target — notes
  the trade is free only for classes qmlcachegen cannot see, naming the `setContextProperty`
  objects. This change moves those objects the *other* way, into the registry, deliberately:
  that is what buys the linting and the AOT resolution. So it slightly widens the rebuild
  trigger, in exchange for the type information being available at all. The two are not in
  conflict; lever 2 applies to C++ that QML never touches, which these are not.

**Open, and deliberately not blocking this change:** the 60 s is worth reducing, but only by
restructuring, never by re-erasing the types. Candidates are trimming what the domain headers
themselves pull in, and making more consumers narrow (`Settings<Domain>*` rather than `Settings*`).

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
