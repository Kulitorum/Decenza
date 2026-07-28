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
  exempt by design (`qml/designer/DE1AppStubs.qml`, believed to be read by Qt Design Studio —
  since deleted, see 3.13).
- [x] 1.9 / 1.3 Gate wired into CI as a step on `nightly-sanitizers.yml`, not a new workflow.
  - **It costs ~8 seconds and no extra build.** `.rcc/qmllint/Decenza.rsp` is written by CMake at
    CONFIGURE time, and `Decenza.qmltypes` plus the module qmldir come from the `Decenza` target,
    which `ninja build_tests` already builds because `tst_qmlregistration` depends on it. That
    dependency (added earlier in this change) is load-bearing for the cost: without it the step
    would have to build the app itself.
  - **It also makes the workflow's own comment stale**, and that is recorded there: `build_tests`
    was measured at 524 steps vs `all`'s 1238 when that comment was written; it is now 1321 vs
    1369, because of the same dependency.
  - ubsan leg only — the lint reads QML sources and generated type info, which no sanitizer flag
    touches, so the asan leg could only report the identical answer twice.
  - The step FAILS rather than skips when `qmllint_check` is absent. CMake omits that target when
    it cannot find qmllint next to Qt's bin/, and a gate that silently does not exist is the
    failure this gate was written to end. Whether `install-qt-action` ships qmllint on Linux was
    never confirmed by anyone; this step is what answers it, on the first night it runs.
  - Nightly, not PR: nothing on GitHub builds a PR (`text-invariants.yml` runs per-PR but is
    build-free by design), so a PR-time gate would mean a Qt install and a full build on a path
    people wait on, against ~4 GB of cache headroom. The tradeoff accepted is that a regression is
    reported the morning after it merges — the same deal ASan already has.
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

- [x] 2.1 List all 39 `setContextProperty()` names with their QML warning counts and their set-sites

  30 names remain (9 migrated in section 3). Counts are unqualified-access warnings, extracted
  through `qmllint_report.py`'s own column-based parser — never by regex over the warning line,
  which is the trap recorded in task 1.1. **943 of the ~5,710 remaining `unqualified` warnings
  are attributable to these 30 names**, so section 2 in full is worth roughly a sixth of the
  category; the other ~4,767 are delegate and file-scope identifiers (`modelData` 567, `root`
  399, `model` 187, `index` 126) whose remedy is `pragma ComponentBehavior: Bound`, out of scope
  here.

  | count | files | name | | count | files | name |
  |---:|---:|---|---|---:|---:|---|
  | 236 | 26 | `DE1Device` | | 22 | 1 | `RemoteMcpAccess` |
  | 179 | 8 | `ScreensaverManager` | | 16 | 1 | `ShotDataModel` |
  | 131 | 11 | `ScaleDevice` | | 15 | 2 | `GHCSimulator` |
  | 76 | 6 | `BLEManager` | | 9 | 1 | `PreviousCrashLog` |
  | 40 | 2 | `SteamHealthTracker` | | 8 | 1 | `USBManager` |
  | 38 | 3 | `WidgetLibrary` | | 5 | 1 | `ProfileStorage` |
  | 37 | 1 | `FlowCalibrationModel` | | 5 | 1 | `CrashReporter` |
  | 29 | 2 | `LibrarySharing` | | 5 | 2 | `McpServer` |
  | 27 | 1 | `WeatherManager` | | 4 | 1 | `SteamDataModel` |
  | 24 | 2 | `Refractometer` | | 4 | 1 | `AppVersion` |
  | 23 | 4 | `BatteryManager` | | 3 | 1 | `MemoryMonitor` |
  | | | | | 2 | 2 | `UsbScaleManager` |
  | | | | | 2 | 1 | `AppVersionCode` |
  | | | | | 1 | 1 | `AutoWakeManager` |
  | | | | | 1 | 1 | `ShotHistoryExporter` |
  | | | | | 1 | 1 | `PreviousDebugLogTail` |

  **Three names scored zero because nothing reads them.** `FlowScale`, `DE1Simulator` and
  `IsDebugBuild` have no reference anywhere in `qml/` outside comments — verified by grepping
  the word and discarding comment lines, not inferred from the zero. `FlowScale` was published
  "Always available for diagnostics", `IsDebugBuild` from a `#ifdef QT_DEBUG` / `#else` pair,
  `DE1Simulator` onto the GHC engine whose single QML file never mentions it. Deleted rather
  than migrated. This is its own small argument for the change: a context property that no QML
  reads is indistinguishable from one whose call sites are all typos, because neither the
  compiler nor qmllint can see either.

- [x] 2.2 For each of the 7 names set more than once, determine whether the sites are mutually
  exclusive startup paths or genuine runtime swaps

  **Only two are genuine swaps.** The other five were miscounted by this task's own premise —
  "set more than once" conflates three different things:

  - **`ScaleDevice` (10 sites) — genuine runtime swap.** Rotates between `&flowScale`,
    `physicalScale.get()`, `usbScale` and `&simulatedScale` as scales connect and drop.
  - **`Refractometer` (4 sites) — genuine runtime swap.** `nullptr` ↔ `refractometer.get()`.
  - **`DE1Device` and `GHCSimulator` — not swaps.** The second site publishes the *same object*
    to a *second engine* (`ghcEngine`, the GHC simulator window). A `QML_SINGLETON` with a
    published static instance covers both engines by construction, so these are simpler as
    singletons than as context properties, not harder.
  - **`IsDebugBuild` — not a swap.** Two `#ifdef` arms, mutually exclusive at compile time. Now
    deleted as unused (2.1).
  - **`Settings`, `TemperatureDisplay` — already migrated** in section 3; stale entries in the
    original task text.

- [x] 2.3 Confirm every object's lifetime outlives `QQmlApplicationEngine engine`

  **Three do not, and migrating them as written would reintroduce a crash this codebase has
  already had.** `engine` is declared at `src/main.cpp:1958`. Declared *after* it, and therefore
  destroyed *before* it: `ghcSimulator` (3367), `flowCalibrationModel` (3456), and
  `de1SimulatorPtr` (inside the `DECENZA_SIMULATOR` block).

  `main.cpp`'s own teardown comment records why this matters — a refractometer teardown crash
  traced to exactly this ordering, "the device was declared AFTER the engine, so it died first,
  while bindings reading it were still live". What makes the current arrangement safe is a
  property of context properties specifically: **QML drops a context property when its object
  emits `destroyed()`**, with the C++ side holding a `QPointer` that self-nulls at the same
  moment.

  A `QML_SINGLETON` backed by a `static` raw instance pointer has no equivalent. Nothing nulls
  it, and nothing tells QML the object is gone. So for these three the migration is not a
  registration change but a declaration move — hoist above `engine` first, or the singleton is
  strictly less safe than the context property it replaces. `FlowCalibrationModel` (37 warnings,
  1 file) is the only one of the three worth the move on lint grounds; `GHCSimulator` is 15
  warnings in a debug-only window and `DE1Simulator` is now deleted.

- [x] 2.4 Split the names into two lists: fixed-instance (migrate directly) and runtime-swapped

  - **Migrate directly (20 objects, 720 warnings):** `DE1Device`, `ScreensaverManager`,
    `BLEManager`, `SteamHealthTracker`, `WidgetLibrary`, `LibrarySharing`, `WeatherManager`,
    `BatteryManager`, `RemoteMcpAccess`, `ShotDataModel`, `USBManager`, `ProfileStorage`,
    `CrashReporter`, `McpServer`, `SteamDataModel`, `MemoryMonitor`, `UsbScaleManager`,
    `AutoWakeManager`, `ShotHistoryExporter`, `GHCSimulator`.
  - **Needs a façade — runtime-swapped (2 names, 155 warnings):** `ScaleDevice`, `Refractometer`.
    A singleton exposing `Q_PROPERTY(... NOTIFY)` that the swap sites write, so QML binds to the
    façade and the backend moves underneath it.
  - **Needs a declaration move first (1 of the above, plus the façade work):**
    `FlowCalibrationModel` — see 2.3.
  - **Needs a value-holder singleton (4 names, 16 warnings):** `AppVersion`, `AppVersionCode`,
    `PreviousCrashLog`, `PreviousDebugLogTail` are plain `QString`/`int`, not QObjects, so this
    group needs somewhere to put them. Note it renames the QML call sites, which the object
    migrations do not — 16 warnings is a thin return for a rename, so this is the one group where
    doing nothing is defensible.
    **Superseded by 3b.4:** the planned answer here was one small `AppInfo` holder, and that was
    wrong. Three of the four values already had a registered owner (`UpdateChecker`,
    `CrashReporter`), which the planning pass never checked for. Read 3b.4, not this line.
  - **Deleted, not migrated (3 names, 0 warnings):** `FlowScale`, `DE1Simulator`, `IsDebugBuild`.

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
  the module and stays out — and has since been deleted outright (3.13). Missing this produced `ReferenceError: Settings is not defined`
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
- [x] 3.5 `MainController` — unlocked 9 files (66 → 75 clean), `unqualified` 7,251 → 6,335 (−916;
  design.md predicted 879 for this name, so the anchor was stale — the same stale-anchor note as
  3.4. The number quoted here was originally 6,337/−914, read off an intermediate run before the
  dead announce sites were fixed and AIConversation registered; it disagreed with the baseline
  committed in the same PR. Caught in review — plausible is not the same as correct). Used the SIMPLE direct-macro shape per the maintainer's call: Qt
  recommends it for types you own, and that was the tiebreaker over the `QML_FOREIGN` wrapper.
  Three things this taught that the earlier migrations did not:
  - **Registering the parent exposes the children.** `unresolved-type` went 2 → **763** the moment
    qmllint could read MainController's property list and found 22 sub-object types unregistered.
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
- [x] 3.6 `ProfileManager` — **unlocked 5 files, not the 3 predicted** (clean 75 -> 80).
  Took the third option on `currentProfilePtr`: deleted the `Q_PROPERTY`, kept the C++ accessor.
  A `Q_GADGET` on `Profile` would have meant annotating ~50 accessors to expose something no
  caller wants — nothing in `qml/` referenced the property, and QML could never have read a
  member through the pointer anyway.
- [x] 3.7 + 3.8 `MachineState` / `MachineStateType` — done together, because they are one object.
  MachineStateType existed ONLY because a context property named MachineState shadows a type of
  the same name, so the enums needed a second, invented name. A QML_SINGLETON needs no such
  split: `MachineState.Phase.X` resolves through the singleton's own metaobject. 155 call sites
  across 13 QML files rewritten, the `qmlRegisterUncreatableType` deleted. That the enums resolve
  is verified, not assumed — an unknown enum member is a `missing-property` diagnostic, and all
  155 lint clean. Unlocked 3 files (clean 80 -> 83).
- [x] 3.9 + 3.10 `MarkdownRenderer`, `EmojiAssets`, `TemperatureDisplay` — done together.
  **`qml/Theme.qml` is clean**, which is task 5.3's acceptance test. Clean 83 -> 85 (also
  ConversationOverlay.qml).
  - These three take a different registration shape from every earlier one: stateless and
    default-constructible, so there is no object `main()` owns and nothing to publish.
    `QML_SINGLETON` alone, engine-constructed and engine-owned. That also covered the GHC
    simulator's separate engine for free — it had needed its own `setContextProperty` line.
  - `TemperatureDisplayBridge` exports as `TemperatureDisplay` via `QML_NAMED_ELEMENT`. This
    broke `tst_qmlregistration`, which looked the QML name up as a class name: a .qmltypes
    Component is keyed by the C++ class name and the QML name is only in `exports`. The test now
    carries both names per row and asserts the exported NAME rather than just the Decenza URI —
    the old check would have passed a `QML_NAMED_ELEMENT` typo that registered under the wrong
    name.
- [x] 3.11 Launch the app and check the log for QML TypeErrors. Building is not evidence — and
  this task earned its wording. With the DE1 simulator on (Ctrl+D), driving the app over MCP:
  - `FlushPage: isFlushing changed to true phase= 11`. That binding is
    `MachineState.phase === MachineState.Phase.Flushing`, and phase 11 IS Flushing — so the
    migrated enum resolved through the singleton at runtime, not merely in qmllint. Navigation
    in (`currentPage=flushPage`) and back out (`phase= 4`, Ready) both fired.
  - SteamPage and HotWaterPage both rendered live and phase-driven ("Steam - Pouring",
    "Hot Water - Pouring") and returned to Idle on stop.
  - Log filtered for TypeError / ReferenceError / undefined / "asked for the singleton":
    **zero lines** across the session.
  - Not exercised: Espresso, Transport, Descaling. The mechanism is proven on three independent
    pages in both directions; those three are covered statically only.
- [x] 3.14 Review round on PR #1678. Findings verified before acting, all of them real:
  - **`qml/Theme.qml` still guarded on `typeof EmojiAssets === "undefined"`** and its warning told
    the maintainer to `setContextProperty("EmojiAssets", ...)`. Unreachable now, and the advice
    was actively dangerous: a context property of that name SHADOWS the singleton, which is #1661
    in the very file #1661 happened in. Guard and warning deleted, with a note saying not to
    reinstate them.
  - `src/profile/temperaturedisplay.h` class comment still said "registered as the
    TemperatureDisplay context property", contradicted eight lines lower by its own macros.
  - `scripts/qmllint_report.py` still exempted `qml/designer/DE1AppStubs.qml` from the
    in-the-module check, citing a `de1-qt.qmlproject` that 3.13 deleted.
  - `de1-qt.qmlproject.qtds` was missed by 3.13 — the scaffold deletion was incomplete.
  - `docs/accessibility/phase4.md` and `phase5.md` handed out 17 `MachineStateType.Phase.*`
    samples for the #736 implementation plan. Anyone following them would have written a
    ReferenceError.
  - "Stateless" was the wrong word for `EmojiAssets` — it has a lazily-built ~4,000-entry cache.
    The conclusion (a second engine instance is harmless) survives; the reasoning was wrong.
- [x] 3.15 Close the last honour-system gap in `tst_qmlregistration`. Both singleton tests were
  hand-written row lists, so a future singleton whose row nobody added was invisible: build green,
  qmltypes green, qmllint green, bindings null at runtime. Replaced with one test that DERIVES the
  set by scanning `src/**/*.h` for `QML_SINGLETON`, resolving `QML_FOREIGN`/`QML_NAMED_ELEMENT` to
  the registry and QML names, and requiring a publish call IFF the header declares
  `static void setQmlInstance(` — asserting the negative direction too, which no hand list could.
  - Added `qmlOnlyNamesPhaseEnumeratorsThatExist`, because this change made those 153 sites newly
    instance-dependent (see the note in `machinestate.h`). Rename an enumerator and leave QML
    naming the old one and the comparison is silently false forever.
  - **Both verified by breaking them.** Removing `MachineState::setQmlInstance()` from main.cpp
    and renaming one QML enumerator to `FlushingTypo` compiled clean with ZERO warnings, and both
    tests failed with the right message. That clean build is the whole argument for these tests.
  - The parser was wrong on first run and its own sanity guard said so
    (`parsed only 0 Phase enumerators; the parser is broken, not the code`) rather than passing
    vacuously — the lesson from the six mis-measurements in 1.11, applied.
- [x] 3.13 Delete the dead Qt Design Studio scaffold: `qml/Decenza/{qmldir,plugins.qmltypes}`,
  `de1-qt.qrc`, `de1-qt.qmlproject`, `qml/designer/DE1AppStubs.qml`. Found while migrating
  MachineState — `plugins.qmltypes` still listed `MachineStateType` after this change deleted it.
  - It was not merely unused, it was **broken since #338** (the Decenza rename). The qmldir
    declares `module Decenza` while every type inside `plugins.qmltypes` exports under `DE1App/`:
    the directory was renamed, its contents were not, so Design Studio would resolve
    `Decenza/DE1Device` against an export named `DE1App/DE1Device` and find nothing. Nobody can
    have opened this and had it work.
  - `de1-qt.qrc` is the only thing that referenced the qmldir, and `CMakeLists.txt` does not
    reference `de1-qt.qrc` (it lists only `resources/*.qrc`). Both it and `de1-qt.qmlproject`
    also name a `qtquickcontrols2.conf` that no longer exists.
  - `DE1AppStubs.qml` is never instantiated and is not in the `qt_add_qml_module` file list. It
    had also gone stale in a way that would mislead rather than help: it stubs `settings.<prop>`
    flat, when Settings has had 12 domain sub-objects for some time, and puts
    `availableProfiles` on MainController, where it no longer lives.
  - This is exactly the drift the change is about, in a second form: a hand-maintained parallel
    declaration of the same module that nothing verifies. If Design Studio support is wanted
    later, the generated `Decenza.qmltypes` is now good enough to be the real answer, and this
    scaffold would not have been a starting point.
- [x] 3.12 Move each unlocked file onto the clean list in the same commit that unlocks it —
  done for 3.6, 3.7/3.8 and 3.9/3.10; the baseline was regenerated with the patched qmllint in
  each commit, and each run confirmed no file left the clean list and no ceiling rose.

## 3b. Second migration batch — the four that needed no façade

Follows the 14 names migrated in PR #1680. Picked as the set where the blocker was mechanical
rather than a design question: 103 of the 283 warnings still outstanding, no forwarding façade, no
new lifetime concept.

- [x] 3b.1 `SteamHealthTracker` (40 warnings, 2 files) — the largest of the four, and the one with
  a wrinkle. It already carried `QML_NAMED_ELEMENT(SteamHealthTrackerType)` + `QML_UNCREATABLE` in
  its own header, and the `…Type` suffix existed **only** because a context property named
  `SteamHealthTracker` resolves ahead of a type of the same name. The singleton removes the
  collision rather than routing around it, exactly as `MachineState` did when `MachineStateType`
  went away: no context property remains, and QML reads the enums off the singleton as
  `SteamHealthTracker.EstablishingAfterReset`. Registration moved to `contextsingletons_qml.h`, so
  the class header drops its `<QtQml/...>` include and is clean for the test targets again.
  - The removed header comment claimed *"tst_qmlregistration asserts the EXPORT name for exactly
    this reason; do not tidy this to QML_ELEMENT"*. **It does not** — nothing under `tests/` ever
    referenced `SteamHealthTrackerType`, so the rename that comment warned against would have gone
    green. The guard is now structural instead: the singleton and the enums share one name, so
    losing the registration breaks the property reads and the enum reads together and loudly.
- [x] 3b.2 `FlowCalibrationModel` (37, 1 file) — declaration hoisted above `engine` per the
  lifetime rule; only the declaration moved, because `mainController.shotHistory()` is not ready
  that early, so the three setters stay where they were.
- [x] 3b.3 `ProfileStorage` (5) and `McpServer` (5) — plain migrations, identical to the 14.
  Neither had any QML registration; the earlier note calling them "already registered, would
  conflict" was wrong, and only `SteamHealthTracker` was ever in that shape.
- [x] 3b.4 `AppVersion`, `AppVersionCode`, `PreviousCrashLog`, `PreviousDebugLogTail` (16) — four
  loose values, given to the objects that already owned them. **The first attempt invented an
  `AppInfo` singleton to hold all four; review deleted it, and that was the right call** (3b.11):
  - `AppVersion` / `AppVersionCode` → `MainController.updateChecker.currentVersion` /
    `.currentVersionCode`, which already existed, already read the same `VERSION_STRING` and
    `versionCode()`, were already `CONSTANT` and registered, and were already used in the same
    file that displayed the context-property versions.
  - `PreviousCrashLog` / `PreviousDebugLogTail` → `CrashReporter`, where QML already goes to
    submit them, so `CrashReportDialog` now reads and submits through one object. Both are
    `CONSTANT`: they describe a run that has already ended. Note the asymmetry — `CrashReporter`
    does not own the log's lifecycle; `MainController::clearCrashLog()` deletes the file
    `CrashHandler` wrote.
- [x] 3b.5 Dead registration removed: `qmlRegisterUncreatableType<DE1Device>(… "DE1DeviceType")`.
  It existed for the same shadowing reason as the others, `DE1Device` became a singleton in
  PR #1680, and nothing in `qml/` or `tests/` referenced the name.
- [x] 3b.6 `decenzaPublishedSingleton()` **stays in `contextsingletons_qml.h`**. It was briefly
  extracted to `src/core/qmlsingletonpublish.h` when `AppInfo` would have been a second caller;
  deleting `AppInfo` left the extraction with one caller and no justification, so it folded back.
  The point it was extracted for still holds and is recorded there: three hand-written copies of
  that logic previously diverged, so a fourth was not the move.
- [x] 3b.7 **`GHCSimulator` (15) deliberately NOT in this batch**, and not for the lifetime reason
  the old note gave. Its declaration sits inside `#if (Q_OS_WIN || Q_OS_MACOS) && QT_DEBUG`, so on
  every other build the instance legitimately does not exist. QML is already safe with that —
  `main.qml:861` truthy-guards the name and `:868` yields `null` either way — but
  `decenzaPublishedSingleton()` treats "asked for before main() published it" as always a defect
  and would `qCritical` on every Android, iOS and Linux launch. Registering it needs an explicit
  optional-singleton path in the helper. That is a real decision, not an oversight, and it is not
  worth taking for 15 warnings.

- [x] 3b.8 **Two real defects surfaced by the migration, both in `FlowCalibrationPage.qml`** — the
  one file whose singleton this batch added, and neither reachable before, because a context
  property has no type for qmllint to check a member against:
  - `FlowCalibrationModel?.errorMessage?.length` — `errorMessage` is a `QString`, so the inner `?.`
    is redundant optional chaining. The outer one still short-circuits.
  - `(FlowCalibrationModel?.multiplier ?? 1.0).toFixed(2)` — qmllint models the `??` result as
    `QJSPrimitiveValue`, which has no `toFixed`. Wrapped in `Number()`, which keeps the defensive
    default and gives the expression a type.
- [x] 3b.9 **Corrected the baseline PR #1680 shipped**, which recorded three ceilings below what the
  tree produces and had the nightly's ubsan leg red at the `QML diagnostics gate` step. Full
  account in `bugs-found.md`; the short version is that it was generated against a build predating
  part of that PR's own C++ changes, so qmllint resolved less deeply and counted fewer warnings —
  methodology error #2 from task 1.1, committed again in the change that documents it. CI's stock
  qmllint and the patched one agree exactly on the true numbers, which also settles that a
  patched-binary baseline is enforceable by a stock one.
- [x] 3b.10 Measured result: gate passes, clean list **89 -> 90** of 218.
  `SettingsCalibrationTab.qml` 40 -> 1, `FlowCalibrationPage.qml` 39 -> 2, `qml/main.qml` 159 ->
  141, `RecipeWizardPage.qml` 159 -> 151, `SettingsAITab.qml` 37 -> 35, and
  `SettingsUpdateTab.qml` onto the clean list.

- [x] 3b.11 **Review round on PR #1683.** Five agents; every finding verified against the code
  before acting on it, and three were acted on:
  - **`AppInfo` deleted** (see 3b.4). Three of its four values already had a registered owner, and
    `AppVersion` duplicating `UpdateChecker::currentVersion` is precisely the two-sources-of-truth
    drift this change exists to remove. The batch ends up adding no new types rather than two.
  - **Three false comments, all written in this batch's first commit.** Recorded because the rate
    matters more than any one of them: `appinfo.h` named `CrashReporter` as what clears the crash
    log (it is `MainController::clearCrashLog()`); `main.cpp` called the three storage `…Type`
    registrations context-property workarounds (no such context property ever existed —
    `git log -S` — and the claim contradicted its own next paragraph); and `main.cpp`'s teardown
    comment still described `FlowCalibrationModel` as declared after `engine` and protected by
    QML dropping a context property, both false since the hoist. That last one is the
    documentation for the exact use-after-free hazard this batch navigates, so a later reader
    could have moved the declaration back down on its authority. **Three of the four comment
    defects found across #1680 and #1683 were confident, specific, and wrong.**
  - **The enum-contract test that the deleted comment had falsely claimed existed.**
    `SettingsCalibrationTab.qml` compares against `SteamHealthTracker.EstablishingAfterReset`; a
    renamed enumerator makes that `=== undefined` — silently false, wrong wording forever, no
    error and no log. Written generally rather than for one name: every
    `Singleton.UpperCaseMember` access in `qml/` must resolve in `Decenza.qmltypes`. It covers the
    unscoped form that `qmlOnlyNamesPhaseEnumeratorsThatExist` structurally cannot, there being no
    enum name in the expression to anchor on. Verified by negative control — injecting a renamed
    enumerator fails it, reverting passes. Passing alone would only have proved it ran.
  - Declined, with reasons: a `try`/`catch` around `Component.onCompleted` (real observation — a
    null singleton throws where a missing context property was falsy — but the only route to null
    is deleting a publish line `tst_qmlregistration` asserts exists); `qFatal` in the publish
    helper (contradicts its deliberate "checked, not asserted" design, which exists because
    `Q_ASSERT` compiles out of Release); and stripping `?.` from `FlowCalibrationPage` for
    call-site consistency (churn with no defect behind it).

- [x] 3b.12 **Closed the two holes that let the bad baseline through**, rather than only fixing the
  numbers it produced. Full account in `bugs-found.md`. Both were checked by one-time manual
  negative control — **neither has an automated test**, which is a real gap beside
  `tst_qmlregistration.cpp` and is recorded as such rather than glossed.
  - `check_registry_fresh()` — the existing staleness check compares QML against the build's copy
    and is structurally blind to the C++ side. `Decenza.qmltypes` is generated by
    `qmltyperegistrar`, so a changed registration leaves every QML file byte-identical while the
    registry is a generation behind, and the run measures against types that no longer match.
    The new check asserts every `QML_SINGLETON` under `src/` is in the registry's exports.
  - `--allow-ceiling-rise` — `--update-baseline` used to lock in an improvement and relax the gate
    with the same keystroke and identical output. A rise now needs the flag, the refusal names
    each file with before/after, and permitting one prints what was raised. Leaving the clean list
    counts as a rise from zero.
  - The general lesson, which is the reason this is recorded at length: **a ratchet that only
    measures one direction will accept a bad measurement pointing the other way, indefinitely.**
    "The tree improved" and "the measurement under-reported" produced identical evidence here, and
    only one of the two was ever considered.

## 4. The runtime-swapped devices — DONE, after being deferred

This section said "not in this change", and the deferral was overturned at the maintainer's
direction: the goal is files that are actually CLEAN, because a file with a ceiling still hides
the next bug in it, and `ScaleDevice` was the single largest name in the tree.

**The measurement that justified deferring was correct and the conclusion was still wrong**, in a
way worth keeping. It said the façade unlocks *zero* files because the files using `ScaleDevice`
are dirty for other reasons anyway. That was true when measured. It stopped being true once
sections 3–3c and the AppShell work cleared those other reasons: by the time the façade was built
it took `SteamPage.qml` (212 warnings at the start of the change) to **clean**, plus
`ScaleWeightItem.qml` and `ScreensaverItem.qml`. A "unlocks zero files" measurement is a statement
about the current state of the other work, not a property of the task — re-measure before
re-using it as a reason.

- [x] 4.1 Both façades built: `ScaleDeviceProxy` (src/ble/) and `RefractometerProxy`
  (src/ble/refractometers/). Registered `QML_FOREIGN` under the names `ScaleDevice` and
  `Refractometer`, so all 91 QML call sites are untouched. Each holds its target in a `QPointer`,
  mirrors every property, forwards every public slot, and re-emits every signal. The eleven
  scale re-point sites and five refractometer ones in main.cpp are now `setTarget()`.
  - Deliberate: `name`/`isFlowScale`/`isSimulated` are `CONSTANT` on `ScaleDevice` and are NOT
    constant on the proxy — they are facts about *which device is attached*, which is what the
    class changes.
  - Deliberate: every slot forwarded, not the four QML calls today. A context property exposed the
    whole set; forwarding a subset would silently delete the rest from QML's reach, and the calls
    would still parse.
  - Deliberate: `weightSampleReceived` stays distinct from `weightChanged`. Collapsing them would
    reintroduce #1176/#1185 one layer above where they were fixed.
- [x] 4.2 The latent win is now taken, not left: re-assigning a context property dirties every
  binding in the root context, so each scale connect/disconnect used to trigger an app-wide
  re-evaluation. `setTarget()` emits only this object's signals.
- [x] 4.3 **Hardware-verified on the branch build**, 2026-07-28 session 65 (macOS, app started
  10:47:49 against the 10:42 binary), BOTH directions, three round trips:
  - DiFluid R2 (DFT-R102, fw V230) connected t+78 s — `refractometerProxy.setTarget(device)` —
    and disconnected t+199 s (`connectedChanged -> FALSE, reason=transport-disconnected`).
  - Decent Scale (fw 3.1.13) connect/disconnect at t+66/68, t+118/124 and t+198 — each logged as
    "Scale disconnected - switched to FlowScale" / "Scale connected - switched to physical scale",
    i.e. `scaleProxy.setTarget(&flowScale)` and `setTarget(physicalScale)`, the real re-points in
    both directions.
  - **Zero `TypeError`/`ReferenceError`/`Unable to assign` in the QML log across all of it.** Every
    WARN in the session is unrelated infrastructure (MQTT CONNACK, CoreLocation/GPS, the
    McpRemoteAccess funnel probe) plus the expected BLE `Transport disconnected` lines.
  - This is the test the deferral was waiting for, and it is the one that mattered: 91 QML call
    sites read these two names, and a proxy that failed to re-emit would have shown up as stale
    bindings or a null dereference on the very first transition.
- [ ] 4.4 Still unexercised, and honest about it: the USB scale, WiFi scale and simulated-scale
  re-point sites. Each is the same one line in the same class as the BLE path that was exercised
  six times, but none was run.

## 5. Imports and close-out

- [x] 5.1 Add `import Decenza` to the 13 QML files that lack it — done under 3.3; see the entry
  there. Note the sequel recorded as 6.2 below: three components had `import Decenza` *removed*
  as dead one commit before it became load-bearing.
- [ ] 5.2 Re-run the report from 1.1 and confirm the clean list reached 103 of 212 files
- [ ] 5.3 Confirm `qml/Theme.qml` is on the clean list — that file is the specific regression this change exists to prevent, and it is the acceptance test for the whole change
- [ ] 5.4 Triage the categories the noise was hiding — 310 `missing-property`, 27 `import`, 25 `index`, and the singleton `incompatible-type` / `equality-type-coercion` / `unresolved-type` findings — and fix or exempt each explicitly
- [ ] 5.5 Full test suite green via `mcp__qtcreator__run_tests` (scope `all`)
- [x] 5.6 Update the qmllint instruction in `CLAUDE.md` and `docs/CLAUDE_MD/QML_GOTCHAS.md`, which currently point at a command whose output is unreadable
- [x] 5.7 Record in `QML_GOTCHAS.md` that new C++ objects exposed to QML are registered as singletons, never via `setContextProperty()`
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

## 3c. The last runtime type registrations, and the erasure they were hiding

- [x] 3c.1 Eight runtime `qmlRegisterType<>()` calls in `main.cpp` were the last of the defect class
  this change exists to remove — for CREATABLE types this time. A runtime registration never
  reaches `qmltyperegistrar`, so the type is absent from `Decenza.qmltypes` and qmllint reports
  every *use* of it as `X was not found. Did you add all imports and dependencies?` Four moved to
  `QML_ELEMENT` in their own headers: `FastLineRenderer`, `JsCanvasPainterItem`,
  `StrangeAttractorRenderer`, `DocumentFormatter`. Safe there, unlike the classes in
  `contextsingletons_qml.h`, because each already derives from a Quick type, so any target
  compiling them links Qt6::Qml anyway.
- [x] 3c.2 Required three new entries in the `target_include_directories(Decenza ...)` block —
  `src/rendering`, `src/ui`, `src/screensaver`. The generated registration file emits
  `#if __has_include(<bare-name.h>)`, so an unreachable basename makes the include expand to
  nothing and the build fails with `use of undeclared identifier` in generated code, three tools
  from the cause. That block already documented this and named the ambiguity check to run first
  (`find <dirs> -maxdepth 1 -name '*.h' -exec basename {} \; | sort | uniq -d`, empty = safe).
- [x] 3c.3 **The registration was hiding two layers of type erasure, and that is the real find.**
  Fixing it made `missing-property` RISE 322 -> 388, because 66 calls in `CupFillView.qml` became
  reachable for the first time:
  - `JsCanvasPainterItem::paint()` declared `QObject *ctx` while emitting a `JsCanvasContext*`
    with 17 `Q_INVOKABLE`s, so every `beginPath`/`lineTo`/`fill` was a member missing from
    `QObject`. qmllint was right and useless.
  - `createLinearGradient()`/`createRadialGradient()` declared `QObject*` while returning a
    `JsCanvasGradient*`, so all 41 `addColorStop` calls were the same shape one level down.
  Both now typed, and `JsCanvasContext`/`JsCanvasGradient` registered `QML_UNCREATABLE` so the
  calls are checked against the real API. Runtime-verified: the cup fill still renders.
- [x] 3c.4 `Pipe*Geometry` **also moved to compile-time registration — after a wrong call was
  caught by a question.** They were first left on `qmlRegisterType<>` on the measurement that
  compile-time "bought nothing": it cleared three `import` warnings and produced three
  `unresolved-type` ones instead. The measurement was right and the conclusion was wrong. That
  trade was not a property of qmllint; it was a missing declaration.
  - `qt_add_qml_module(Decenza ...)` listed no `DEPENDENCIES`. The import path resolves what QML
    **imports** — which is why `import QtQuick3D` in `PipesScreensaver.qml` always worked — but
    not what our own registered types **inherit**. `PipeCylinderGeometry`'s prototype is
    `QQuick3DGeometry`, and qmllint will not link a prototype across modules the module has not
    declared, even with `Quick3D.qmltypes` shipped and on the path.
  - With `DEPENDENCIES QtQuick3D` both sets clear: `import` 7 -> 4 and **no** `unresolved-type`
    warnings appear. Kept conditional on `ENABLE_QUICK3D`, because `pipegeometry.*` genuinely is
    not compiled without it and the module would be declaring a dependency it does not have.
  - Recorded because of how the error was made, not what it was: a measured trade-off was accepted
    as a property of the tool without asking why the prototype was unresolvable. The withdrawn
    host-dependence argument (all seven release workflows install `qtquick3d`) was the *second*
    wrong reason given for the same deferral.
- [x] 3c.5 Result: gate passes, clean list **90 -> 92**. `import` 23 -> 7, and both
  `incompatible-type` and `unresolved-type` cleared entirely — the former was
  `StrangeAttractorScreensaver.qml` binding `target: renderer`, unresolvable while that type was
  registered at runtime.
