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
- [ ] 3.3 `Settings` — 1,335 sites, unlocks 15 files (→ 79). Verify all 7 domain sub-objects still resolve as `Settings.<domain>.<prop>`
- [ ] 3.4 `AccessibilityManager` — unlocks 8 files (→ 87)
- [ ] 3.5 `MainController` — 879 sites, unlocks 9 files (→ 96)
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

- [ ] 5.1 Add `import Decenza` to the 13 QML files that lack it
- [ ] 5.2 Re-run the report from 1.1 and confirm the clean list reached 103 of 212 files
- [ ] 5.3 Confirm `qml/Theme.qml` is on the clean list — that file is the specific regression this change exists to prevent, and it is the acceptance test for the whole change
- [ ] 5.4 Triage the categories the noise was hiding — 310 `missing-property`, 27 `import`, 25 `index`, and the singleton `incompatible-type` / `equality-type-coercion` / `unresolved-type` findings — and fix or exempt each explicitly
- [ ] 5.5 Full test suite green via `mcp__qtcreator__run_tests` (scope `all`)
- [ ] 5.6 Update the qmllint instruction in `CLAUDE.md` and `docs/CLAUDE_MD/QML_GOTCHAS.md`, which currently point at a command whose output is unreadable
- [ ] 5.7 Record in `QML_GOTCHAS.md` that new C++ objects exposed to QML are registered as singletons, never via `setContextProperty()`
- [ ] 5.8 Archive this change with `openspec archive fix-qmllint-usability` as the last commit on the branch
