## 1. Baseline and gate

**Every count quoted anywhere in this change was measured wrongly three separate times before
the gate could be built. Read task 1.1 before trusting a number in these documents.**

- [x] 1.1 Record the current qmllint counts in a script (`scripts/qmllint_report.py`). Three
  methodology errors, each found only because the next one contradicted the last:
  1. **Reading warnings by regex.** `grep -A1 '[unqualified]' | grep -oE '[A-Z]\w+'` counts every
     capitalised word in the context line, not the flagged token. It reported `Theme` 1,443 times
     in a run where `Theme` was never once flagged. The script recovers the identifier from the
     source line at the reported *column* instead.
  2. **Linting against a stale build directory.** qmllint resolves `Decenza` types through the
     build's own copies of the QML, so the counts describe whatever was last compiled. Same
     command, before and after a rebuild: 11,565 unqualified / 52 clean, then 2,795 / 103. The
     script now refuses to run against a stale build, comparing file *content* — an earlier mtime
     version false-positived on a freshly built tree, because `copy_if_different` leaves old
     timestamps and `git pull` bumps source ones.
  3. **Hand-assembled qmllint arguments.** `qt_add_qml_module` generates
     `<build>/.rcc/qmllint/Decenza.rsp` carrying `--bare`, four `-I` paths (including Qt's own
     `qml` directory) and four `--resource` `.qrc` files. Both figures above were produced with a
     single `-I <build>` and none of the rest, so neither describes how this module is really
     linted. The script now invokes Qt's response file, which is generated from the module
     definition and therefore cannot disagree with it.
- [x] 1.2 Add `qmllint_check` and `qmllint_report` targets to `CMakeLists.txt`. Deliberately not
  `ALL` and not a dependency *of* `Decenza`: the run takes minutes and a gate people route around
  is not a gate. Both depend *on* `Decenza`, so the script's refusal to lint a stale build becomes
  a rebuild rather than an error message.
- [ ] 1.3 Decide the CI trigger. **The lint itself is not the cost — the full tree takes 8.1
  seconds.** Every larger figure this change previously quoted (2 minutes, then 7–10) was one
  pathological file, not the corpus; see 1.11. What remains expensive is the *build* the lint
  depends on: it needs the generated qmldir, the type registrations and the response file, so it
  cannot be a build-free script job like `text-invariants.yml`. Cache headroom is tight; that,
  not the 8 seconds, is what to weigh.

  Likeliest host when this is picked up: a step on `nightly-sanitizers.yml`, which already builds
  the app on `ubuntu-24.04` and takes 12–16 min that nobody waits on, so it needs no new cache
  entry. Not decided, and deliberately not built yet — CI is working, and the gate is useful
  locally before it is useful there. Whoever wires it should confirm rather than assume that
  `install-qt-action` puts a `qmllint` in Qt's `bin/` on Linux.

- [ ] 1.11 **`qml/components/layout/items/CustomItem.qml` cannot be linted by stock qmllint
  6.11.1**, and that is a Qt bug, not a problem with the file.
  `QQmlJSTypeResolver::merge(QQmlJSRegisterContent, QQmlJSRegisterContent)` recurses into itself
  twice — once per `mergeScopes()` call in its return statement — allocating a pool conversion at
  every node, so the call tree is 2^depth. The `a == b` early return is the only bound and does
  not fire while scopes keep differing. Measured on that one 613-line file: a 313 GB physical
  footprint, growing ~30 GB/min, SIGKILLed by the OOM killer after 622.8s having emitted 36 of
  its 122 warnings. It also drove the whole machine into swap.
  - Fix written and verified: memoize `merge()` on the `(a, b)` pair
    (`~/Development/GitHub/qtdeclarative`, branch `fix/qmljs-merge-memoize`, cut from v6.11.1
    which matches the installed Qt byte-for-byte). CustomItem then lints in 0.95s at 0.05 GB,
    and the full tree in 8.1s.
  - Evidence the patch changes speed and not results: per-file warning counts match the
    unpatched run on **167 of 168** files it had reached, the sole difference being CustomItem
    itself (36 partial → 122 complete). Independently, restructuring the QML instead — splitting
    the oversized `substituteVariables` into two functions — also yields exactly 122.
  - Still open: whether sharing a conversion between callers is safe. `createConversion`
    allocates a fresh pool entry per call today; if any consumer depends on identity rather than
    value, this is a behaviour change wearing a performance change's clothing. qtdeclarative's
    `tst_qmllint` is the check.
  - `tst_qmllint`: **742 passed, 0 failed** against the patched build. That is the check the
    speedup cannot give — it says the memoization does not change analysis results.
  - **Do NOT install the patched framework over `~/Qt/.../lib/QtQmlCompiler.framework`.** Tried,
    and it breaks the Decenza build outright: `qmlimportscanner` is signed by The Qt Company
    (Team `A5GTH44LYL`) and macOS refuses to map a locally-built framework into it — *"mapping
    process and mapped file (non-platform) have different Team IDs"* — so CMake cannot configure.
    Nothing to do with the patch; pure code signing. The patched `qmllint` must be run from its
    own build tree, where it is ad-hoc signed and its `@rpath` finds its own QtQmlCompiler.
    Point the gate at it with `-DQMLLINT_EXECUTABLE=<qtdeclarative>/bin/qmllint`.
  - Stock qmllint does not merely lint worse — it never finishes. The script therefore has a
    `--timeout` (default 600s) that fails with a message naming this bug and the remedy, because
    the symptom in CI would otherwise be an unexplained hang on an unrelated change.
  - **Submitted upstream**: [qtdeclarative/+/755657](https://codereview.qt-project.org/c/qt/qtdeclarative/+/755657),
    `Pick-to: 6.12 6.11`.
  - **How CI gets a patched qmllint: it does not.** Building one per CI run (2m44s) or caching
    5.5 MB of artifacts, both needing a rebuild on every Qt bump, is a large standing cost to
    check *one file*. CI runs stock with `--skip-unlintable`, which drops exactly the files named
    in `UNLINTABLE_BY_TOOL_BUG` and lints the other 217 in 8.3s. Developers with the patched
    binary lint all 218; the baseline holds CustomItem's real 118 either way, so nothing about
    the recorded numbers depends on which side ran.
  - **"But it worked for months" — it never did, and nothing regressed.** The natural hypothesis,
    that earlier runs survived because they passed fewer flags, is **disproven**: stock times out
    on this file with a bare `-I <build>` too. What actually happened is that no earlier run
    completed *and said so*. Before the exit-status check, a file qmllint never reached printed no
    warnings and therefore counted as **clean** — the "103 clean files" from an early run was the
    shape of a truncated run, not a measurement. Everything before that was per-file linting of
    files being edited, which never included this one. Hence 122 diagnostics in it that nobody had
    ever seen. Filed here because the plausible explanation was wrong and the wrong one is the
    memorable one.
- [x] 1.4 Add the category exemption block. It is hand-edited in the script and
  `--update-baseline` never writes it — an earlier version regenerated it from the current run,
  which would have silently blessed a diagnostic category nobody had ever seen the next time
  anyone refreshed the baseline. An unknown category now fails on first occurrence, with no count
  comparison. `unqualified` is not in the block and is not eligible for it.
- [x] 1.5 Generate the per-file `unqualified` state: a clean list of files at zero (held at zero)
  and recorded ceilings for the rest. Measured PRE-migration, with a patched qmllint so all 218
  files are real numbers: 28 clean, 190 ceilings, 12,251 `unqualified`.
- [x] 1.6 New QML files default to the clean list, so new code cannot start with a budget
- [x] 1.7 Record the scope backlog's remedy (`pragma ComponentBehavior: Bound` plus required
  properties on delegates) in the generated baseline itself, so someone opening that file learns
  why those counts exist and what clears them without hunting for this change
- [x] 1.8 Detect QML on disk that is missing from `qt_add_qml_module` — such a file is neither
  linted nor bundled, `CLAUDE.md` names forgetting the entry as a known footgun, and without this
  it would have counted as clean, so the gate would have rewarded the mistake. One file is
  exempt by design (`qml/designer/DE1AppStubs.qml`, read by Qt Design Studio).
- [ ] 1.9 Wire the gate into CI so it runs on a branch push, not only on a release tag
- [x] 1.10 Verify the gate is green at HEAD; that it goes red when an undeclared identifier is
  added to a CLEAN file; and that it goes red when a dirty file's count increases. All three
  confirmed, and the clean-file probe re-confirmed on the `--skip-unlintable` path so that
  skipping cannot quietly disarm the gate (`ColorSwatch.qml: 1 unqualified warning(s) in a file
  that had none`, exit 1). Probes reverted.
- [x] 1.12 **The CMake targets had never run.** `--target qmllint_check` invokes
  `${Python3_EXECUTABLE}`, which on macOS is Xcode's **python3.9**, and the script used PEP 604
  `Path | None` annotations — 3.10+ syntax that 3.9 evaluates eagerly and dies on at import,
  before argparse. Every verification of 1.1–1.10 had been run by hand with the shell's
  python3.14, so the failure was invisible. CI would not have caught it either: ubuntu-24.04
  ships 3.12. Fixed with `from __future__ import annotations`; the whole gate re-verified under
  3.9. **Verify a target by running the target, not the command you believe it runs** — this is
  the seventh way a plausible-looking result here turned out to be measuring something else.
- [x] 1.13 Let a released qmllint run the gate: `--skip-unlintable` drops the files in
  `UNLINTABLE_BY_TOOL_BUG` (one, with its Gerrit link as the expiry condition) and lints the
  other 217 in 8.3s. It prints what it skipped on stdout, refuses `--update-baseline` outright,
  and suppresses every "count fell, lock it in" line — a partial run must never ratchet the
  baseline down. Removing a file can only weaken the gate, never fire it. The CMake option
  `QMLLINT_SKIP_UNLINTABLE` defaults **ON**, because the default has to be the one that does not
  hang for ten minutes; it cannot be inferred from `QMLLINT_EXECUTABLE`, since pointing at
  another binary says nothing about whether it is patched.

## 2. Classify the registrations

- [ ] 2.1 List all 39 `setContextProperty()` names with their QML warning counts and their set-sites
- [ ] 2.2 For each of the 7 names set more than once (`ScaleDevice` 10, `Refractometer` 4, `DE1Device`, `Settings`, `TemperatureDisplay`, `IsDebugBuild`, `GHCSimulator` 2 each), determine whether the sites are mutually exclusive startup paths or genuine runtime swaps — verify, do not assume; a swapped name migrated as a fixed instance freezes on the wrong backend and fails silently
- [ ] 2.3 Confirm every object's lifetime outlives `QQmlApplicationEngine engine` (declared at `src/main.cpp:1958`) and that registration can be ordered before `engine.load()`
- [ ] 2.4 Split the names into two lists: fixed-instance (migrate directly) and runtime-swapped (needs a façade)

## 3. Migrate the ten names that matter

Ordered by files unlocked, greedily, not by call-site count. Measured: these ten take the clean
list from 52 to 103 of 212 files. **The other 56 registered names buy exactly one further file
between them** — so they are not in this change, and neither are the façades (group 4).

One commit per name, each verified before the next. The failure mode being guarded against is a
name that fails to register, resolves to `undefined` in QML, and throws only when its binding
evaluates — the same silent, delayed shape as the bug this change exists to prevent.

- [x] 3.1 `TranslationManager` — 3,668 QML references, unlocked exactly the 12 files predicted:
  **28 → 40 clean, `unqualified` 12,251 → 8,604**, `missing-property` 323 → 318, `import` 22 → 21.
  Baseline and both category ceilings lowered in the same commit.
  - The mechanism is **`QML_ELEMENT` + `QML_SINGLETON` + an explicit `qml_register_types_Decenza()`
    call**, not `qmlRegisterSingletonInstance` as design D1 originally said. D1 has been corrected
    with the Qt source that proves it; read it before starting 3.3, because the same trap is
    waiting for every remaining name.
  - `TranslationManager` is not engine-constructed (main.cpp owns it and wires it into eight
    subsystems before the engine exists), so it publishes the instance via `setQmlInstance()` and
    a static `create()` hands it back with `CppOwnership` pinned.
  - The explicit registration call is a **one-time** cost now paid; 3.4–3.10 need only their macros.
  - **This passed build, qmllint AND all 104 tests while completely broken** — 1,081
    `ReferenceError: TranslationManager is not defined`, every translated string `undefined`.
    Only 3.11 caught it. Do not skip 3.11 for any subsequent name.
- [x] 3.2 Confirm a language change still re-evaluates bindings and `tests/tst_translationreactivity.cpp` is green — `translate` is a `Q_PROPERTY` holding a callable and its reactivity has broken before. Green (104/104, including that test); the `translate` `Q_PROPERTY` is untouched by the migration.
- [x] 3.3 `Settings` — unlocked 16 files (40 → 56 clean), 12 domain sub-objects verified resolving
  as `Settings.<domain>.<prop>` in a real engine and in the running app. Note the task said 7
  domains; there are 12.
  - **Registered via `QML_FOREIGN` in a new `src/core/settings_qml.h`**, not macros on the class:
    `settings.h` is compiled by CLI tools that link no Qt::Qml (`saw_parity`), so a `<QtQml/…>`
    include there is a build break.
  - **The domain sub-objects had to be fixed too, and that was the real work.** They were declared
    `Q_PROPERTY(QObject* mqtt …)` to avoid including twelve headers. Resolving `Settings` exposed
    the cost: qmllint could reach `Settings` and see nothing behind it, so `missing-property`
    went 318 → 1,249. Those were not new defects, they were newly-visible blindness over 1,310
    call sites and 281 distinct settings — a typo like `Settings.brew.slectedFlushPreset` compiled,
    linted clean and failed at runtime. Fixed properly: concrete types, twelve includes,
    `missing-property` back to **317**. Full reasoning and the rejected alternatives are in
    design D2a.
  - **`Q_DECLARE_OPAQUE_POINTER` is a trap.** It is Qt's own suggested escape hatch for an
    incomplete pointer type, it compiles, it satisfies qmllint — and it hands QML
    `QVariant(SettingsBrew*)` instead of an object, so every property and method under
    `Settings.<domain>` fails at runtime. Caught by the new
    `tst_settings::qmlChainsThroughDomainSubObjects`, which now pins the behaviour.
  - Build cost measured both ways rather than assumed — 439 TUs / 60 s on a domain-header edit
    against 310 / 26 s before (full build 122 s). Recorded in D2a with a reduction path that does
    not involve erasing types again.
  - Docs corrected where they now said the wrong thing: CLAUDE.md's Settings rule (which forbade
    the includes) and `docs/CLAUDE_MD/SETTINGS.md`'s 8-step checklist and opening build-win claim.
- [x] 5.1 Add `import Decenza` to the QML files that lack it — done as part of 3.3 because it is
  not optional once `Settings` is a type rather than a context property: a context property is
  globally visible, a singleton needs the module imported. 12 files (`Theme.qml`, `CrtOverlay.qml`,
  `ThemedPageBackground.qml`, …); the 13th, `qml/designer/DE1AppStubs.qml`, is deliberately outside
  the module and stays out. Missing this produced `ReferenceError: Settings is not defined`
  throughout `Theme.qml` — the same shape as #1661, which was also `Theme.qml` failing to resolve
  a `Decenza` singleton.
- [x] 3.4 `AccessibilityManager` — unlocked 5 files (58 → 63 clean, unqualified 7,612 → 7,265).
  Used the SIMPLE shape — `QML_ELEMENT`/`QML_SINGLETON` directly on the class, like
  `TranslationManager` — because every target that compiles `accessibilitymanager.h` links
  Qt::Qml. `Settings` needed the `QML_FOREIGN` wrapper only because `saw_parity` compiles
  `settings.h` without Qml. **Check which case a name is before starting it**; the wrapper is
  not the default. Verified in the running app: no singleton-publish complaint, no
  ReferenceError, and TTS initialised (i.e. the object is genuinely live, not undefined).
  Predicted 8 files, delivered 5 — the shortfall is D2's stale anchor, not a miss; see the note
  on that table.
- [x] 3.5 `MainController` — unlocked 9 files (66 → 75 clean), `unqualified` 7,251 → 6,337 (−914,
  exactly the predicted count). Used the SIMPLE direct-macro shape per the maintainer's call: Qt
  recommends it for types you own, and that was the tiebreaker over the `QML_FOREIGN` wrapper.
  Three things this taught that the earlier migrations did not:
  - **Registering the parent exposes the children.** `unresolved-type` went 2 → **763** the moment
    qmllint could read MainController's property list and found 21 sub-object types unregistered.
    Registering those (plus `AIConversation`) took it back to 2. Landing MainController without
    them would have meant a 763-entry exemption of known-unverifiable accesses — progress by count
    with none in findability, which the proposal names as the failure mode to avoid.
  - **A QML type needs complete parameter types.** moc must build a metatype for every pointer
    parameter of a `Q_INVOKABLE`/signal/slot on a registered class, so forward declarations that
    were fine become hard build errors. Three methods hit this; see bugs-found.md 11.
  - **The direct shape's structural cost is real and showed up here.** `fastlinerenderer.h`
    includes `<QQuickItem>`, so registering `ShotDataModel` would drag QtQuick into every target
    transitively including `maincontroller.h` — `tst_mqttclient` among them, and it does not link
    Qt6::Quick. Deferred those two to their own migration with a named exemption in
    `tst_qmlregistration` rather than spreading QtQuick across test targets.
  - Bare-basename include dirs needed for this: `src/controllers`, `src/ai`, `src/history`,
    `src/machine`, `src/models`, `src/network`, `src/profile`. Basename ambiguity re-checked after
    adding them — still none.
- [ ] 3.6 `ProfileManager` — unlocks 3 files (→ 99)
- [ ] 3.7 `MachineState` — unlocks 1 file (→ 100)
- [ ] 3.8 `MachineStateType` — unlocks 1 file (→ 101). This is a `qmlRegisterUncreatableType`, not a context property; establish why it is unresolved before changing anything
- [ ] 3.9 `MarkdownRenderer` — unlocks 1 file (→ 102)
- [ ] 3.10 `EmojiAssets` and `TemperatureDisplay` — 0 files each alone, but together with `Settings` they are what unlocks `qml/Theme.qml` (→ 103). Greedy single-name ordering misses files blocked by combinations; this pair is the one that matters, because `Theme.qml` is the file whose silent `ReferenceError` shipped in 2.0.1
- [ ] 3.11 After each of the above: launch the app and check the log for QML TypeErrors. Building is not evidence
- [ ] 3.12 Move each unlocked file onto the clean list in the same commit that unlocks it

## 4. Deferred: the runtime-swapped devices

Not in this change. `ScaleDevice` (102 warnings) and `Refractometer` (24) are re-pointed at runtime,
so each needs a forwarding façade with signal re-emission and hardware testing — the highest-risk
work in the original plan. Measurement says they unlock **zero** files, because the files using them
are dirty for other reasons anyway. They keep their per-file ceilings.

- [ ] 4.1 Record in the design that the façade work is deferred, with the measurement that justifies it, so a later reader does not rediscover the idea and assume it was overlooked
- [ ] 4.2 Note the latent win being left on the table: re-assigning a context property dirties every binding in the root context, so each scale connect/disconnect currently triggers an app-wide re-evaluation. That is a performance argument for doing this eventually — it is not an argument for doing it now

## 5. Imports and close-out

- [x] 5.1 Add `import Decenza` to the 13 QML files that lack it — done under 3.3; see the entry
  there. Note the sequel recorded as 6.2 below: three components had `import Decenza` *removed*
  as dead one commit before it became load-bearing.
- [ ] 5.2 Re-run the report from 1.1 and confirm the clean list reached 103 of 212 files
- [ ] 5.3 Confirm `qml/Theme.qml` is on the clean list — that file is the specific regression this change exists to prevent, and it is the acceptance test for the whole change
- [ ] 5.4 Triage the categories the noise was hiding — 310 `missing-property`, 27 `import`, 25 `index`, and the singleton `incompatible-type` / `equality-type-coercion` / `unresolved-type` findings — and fix or exempt each explicitly
- [ ] 5.5 Full test suite green via `mcp__qtcreator__run_tests` (scope `all`)
- [ ] 5.6 Update the qmllint instruction in `CLAUDE.md` and `docs/CLAUDE_MD/QML_GOTCHAS.md`, which currently point at a command whose output is unreadable
- [ ] 5.7 Record in `QML_GOTCHAS.md` that new C++ objects exposed to QML are registered as singletons, never via `setContextProperty()`
- [ ] 5.8 Archive this change with `openspec archive fix-qmllint-usability` as the last commit on the branch

## 6. Review round on PR #1665

Five reviewers over the branch as opened. Every item below was reproduced before it was fixed,
and the confirmed defects are in [`bugs-found.md`](bugs-found.md) entries 3–7 with their evidence.

- [x] 6.1 Fix the crash hazard, the 0×0 widget and the green-when-blind gate (`db15d2d8`)
- [x] 6.2 Restore the three accessibility imports and finish the Windows path fix (`a420a28c`).
  The interesting half is 6.2's first commit: removing `import Decenza` from
  `AccessibleMouseArea`/`AccessibleTapHandler`/`AccessibleLabel` was correct when it happened and
  wrong one commit later. **The lesson is about verification, not about imports**: every use site
  is guarded by `typeof AccessibilityManager !== "undefined"`, so the app-log check used to verify
  each migration could not have seen it. Those three files are now on the gate's clean list, i.e.
  locked at zero, which is the only check that would have caught it
- [x] 6.3 Write the `settings-architecture` spec delta. The capability still required
  `Q_PROPERTY(QObject* …)` and a 200-line `settings.h` — this change does the opposite of both,
  so without the delta the shipped spec contradicts the shipped code
- [x] 6.4 Reconcile the build-cost figures everywhere they appear. `settings.h` claimed a marginal
  blast of "37 TUs" against design.md's reconciled +129, and `SETTINGS.md` presented 439/310 as
  literal translation-unit counts when they are ninja's pre-`restat` dirty set. Wall clock is the
  number to quote: 60 s against 26 s
- [x] 6.5 Correct the stale comments: `translationmanager.h` still said calling `setJsEngine()`
  twice is harmless (it `qFatal`s), and `tst_settings.cpp` still described the reverted
  `Q_DECLARE_OPAQUE_POINTER` design as current
- [x] 6.6 Replace the vacuous `typeof === 'object'` probe in
  `tst_settings::qmlChainsThroughDomainSubObjects`. A QVariant-wrapped opaque pointer answers
  `'object'` too, so the assertion could not fail for the reason it named. Now writes through the
  sub-object and checks the value landed on the C++ instance — a write cannot survive the wrapper
- [x] 6.7 Close the category-ceiling slack. The ceilings were measured full-tree, CI runs
  `--skip-unlintable`, and the difference was a free allowance. `UNLINTABLE_CATEGORY_CONTRIBUTION`
  records what the skipped files contribute (measured: 4 `missing-property`), the skipped run
  enforces the ceilings minus that, and a complete run hard-errors if the table has gone stale
- [x] 6.8 Add `tests/tst_qmlregistration.cpp`. Nothing in the suite touched the registration path
  at all — the one test cited as guarding it publishes a context property, which is what this
  change removes. It checks the generated `Decenza.qmltypes` (the three singletons, and every
  domain sub-object *derived from `settings.h`* rather than hard-coded) plus main.cpp's explicit
  `qml_register_types_Decenza()` call and the runtime registrations that make it necessary.
  **Verified by breaking it**: deleting `SettingsThemeForeign` builds clean with zero warnings and
  fails this test with the fix named in the message
- [x] 6.9 Document why `AccessibilityManager::create()` correctly has no second-engine guard where
  `TranslationManager::create()` needs one — it holds no per-engine state
- [x] 6.10 `ComparisonDataTable.qml:210` — `Layout.preferredHeight: parent.height` asks a layout
  child for its parent layout's height. It does not loop here (the RowLayout is `anchors.fill`),
  but that is a property of the enclosing scope rather than of the line; `Layout.fillHeight`
  expresses the intent and cannot loop
- [ ] 6.11 Deferred at the maintainer's direction: `docs/CLAUDE_MD/BUILD_PERFORMANCE.md` still says
  main.cpp "registers zero QML singletons" (it now registers three). That document is a draft that
  looked at performance without correctness, and is to be revisited as a whole after the QML
  cleanup lands rather than patched line by line now
