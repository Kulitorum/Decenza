## The goal

**Ship fewer QML bugs. A clean qmllint run is the instrument, not the objective.**

Every decision in this change is judged against that, and the two do come apart. Anything that
makes a warning disappear without making a defect *findable* is a regression dressed as progress:
suppression, an exemption entry that stops being examined, a per-file ceiling raised to fit
today's number, or a registration that quiets the linter while the type stays unresolvable at
runtime. If the count improves and the app's real failure modes are no less likely to reach a
user, the change has failed on its own terms.

The corollary, which has already cost this change real time: **do it the way Qt intends, not the
way that reaches a green number soonest.** Where the codebase took a shortcut that traded
verifiability for convenience, the job here is to undo it, not to build on top of it. Two worked
examples from this change, both of which passed the build, the linter and the full test suite
while the app was broken:

- Registering a singleton so qmllint resolves the name, without the runtime registration
  actually taking effect — 1,081 `ReferenceError`s and every translated string `undefined`.
- Declaring `Settings`' domain sub-objects with `Q_DECLARE_OPAQUE_POINTER` so the linter could
  see through them — QML then received `QVariant(SettingsBrew*)` instead of an object and every
  `Settings.<domain>.<prop>` failed at runtime.

Both looked like wins by the count. Neither was. **Green build + green linter + green suite is
not evidence** for this class of work; running the app is (task 3.11), and where a behaviour is
load-bearing it gets a test that pins it (`tst_settings::qmlChainsThroughDomainSubObjects`).

Because shipped bugs are the measure, what this work actually turns up is tracked in
[`bugs-found.md`](bugs-found.md) — confirmed defects, upstream Qt defects, things checked and
found fine, and things observed but not yet diagnosed. That ledger, not the warning count, is
what this change should be judged on at the end.

## Why

QML's worst failure mode is that an undeclared identifier compiles clean and throws only when the
binding is first evaluated — possibly months later, on a screen nobody opened during review.
`qmllint` is the only tool that catches this class before a user does, and on this codebase its
output is unusable: **11,565 `unqualified` warnings**, of which **63% are not defects at all** but
identifiers the tool structurally cannot see.

This is not theoretical. [#1661](https://github.com/Kulitorum/Decenza/pull/1661) fixed a
`ReferenceError: LastShotChartSource is not defined` in `Theme.qml` that shipped in **2.0.1** and
silently disabled the shot-chart background's scrim for every user who chose it. qmllint had
flagged that exact line — as one of 88 identical `Unqualified access` warnings in that one file,
the other 87 being false positives. The signal was present and indistinguishable from the noise.

The noise has a single dominant cause. `main.cpp` publishes **39 objects via
`setContextProperty()`** (`Settings`, `TranslationManager`, `MainController`, …). Context
properties are injected into the root context at runtime, so no static tool can know they exist —
Qt's own documentation discourages them for exactly this reason, and additionally because they
defeat `qmlcachegen`'s compile-time name resolution and force a context-chain walk on every
lookup. Three names alone (`TranslationManager`, `Settings`, `MainController`) account for 5,673
warnings.

There is currently **no qmllint gate anywhere** — not in CMake, not in CI, and there is no
`.qmllint.ini`. The tool is run ad hoc, by hand, against output nobody can read.

## What Changes

- **Migrate ten registrations to QML singletons** under the `Decenza` URI, so qmllint (and
  `qmlcachegen`, and the QML language server) can resolve them. Chosen over suppressing the
  category: names and call sites are unchanged, so the affected QML lines need no edit.
  Measured, these ten take the count of files with zero unqualified warnings from 52 to 103 of
  212 — and **the other 56 registered names buy exactly one further file between them**, so they
  are out of scope. An earlier draft proposed migrating all 39 context properties; that was
  scoped by call-site count rather than by what it actually buys.
- **Add a qmllint gate to the build and to CI**, modelled on the existing `compiler-diagnostics`
  contract: enforcement on from day one, with the not-yet-cleared categories carried as an
  explicit, visible exemption list that only ever shrinks.
- **Never suppress the `unqualified` category.** It is the category that would have caught #1661.
  The exemption list covers categories, and `unqualified` is not eligible for it.
- **Record the residue honestly.** After the migration roughly 4,274 `unqualified` warnings remain
  from delegate and file-scope identifiers (`modelData`, `root`, `index`, `model`), which are a
  different problem with a different fix (`pragma ComponentBehavior: Bound`). This change does not
  pretend to solve those; it scopes them and leaves them measurable.

## Capabilities

### New Capabilities
- `qml-diagnostics`: which QML static-analysis diagnostics the project enforces, how the
  not-yet-cleared categories are carried as a shrinking exemption list, and the requirement that
  QML-visible C++ objects be statically resolvable. The QML-side sibling of
  `compiler-diagnostics`, which governs the same question for C++ and deliberately says nothing
  about QML.

### Modified Capabilities
<!-- None. `compiler-diagnostics` is scoped to compiler flags and its requirements are unchanged;
     `build-config` gains no new requirement (the qmllint target is an implementation detail of
     the new capability). -->

## Impact

- **`src/main.cpp`** — 39 `setContextProperty()` calls become singleton registrations. This is the
  risky part: a name that fails to register resolves to `undefined` at runtime rather than
  failing loudly, which is the same silent-failure shape as #1661. Staged and verified per batch.
- **`qml/`** — no identifier changes; some files gain `import Decenza` (31 of 33 pages already
  have it).
- **`CMakeLists.txt`** — a qmllint target plus the exemption block.
- **`.github/workflows/`** — the gate runs where it can fail a build rather than only on a
  developer's machine.
- **Two names cannot migrate as-is, and are deferred.** `ScaleDevice` (set 10 times) and
  `Refractometer` (4) are *re-pointed at runtime* as devices connect and disconnect — a singleton
  instance cannot be swapped, so each would need a forwarding façade with signal re-emission and
  hardware testing. They unlock **zero** files, because every file using them is dirty for other
  reasons. Out of scope; they keep per-file ceilings.
- **Object lifetime** — the exposed objects are stack locals in `main()` declared before
  `QQmlApplicationEngine engine` (line 1958), so they already outlive the engine, which is what
  singleton registration requires. Registration must still happen before `engine.load()`.
- **`docs/CLAUDE_MD/QML_GOTCHAS.md`** and the root `CLAUDE.md` — the qmllint instruction currently
  tells a reader to run a command whose output is 11,565 lines of mostly noise.
