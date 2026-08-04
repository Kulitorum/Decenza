
# Decenza

Qt/C++ cross-platform controller for the Decent Espresso DE1 machine with BLE connectivity.

## User Manual

The end-user manual lives in the GitHub wiki at https://github.com/Kulitorum/Decenza/wiki/Manual. Consult it when working on user-visible behaviour to confirm documented expectations or the official wording for features. The wiki is a separate git repo (`Kulitorum/Decenza.wiki.git`) — clone it locally if you need to edit a manual page.

**When adding or changing a user-visible feature, update the wiki manual as part of that work** — add a task for it in the change's `tasks.md` (or do it directly for small fixes). Don't leave it as an afterthought; a shipped feature with no manual entry is incomplete.

## Reference Documents

Detailed documentation lives in `docs/CLAUDE_MD/`. Read these when working in the relevant domain:

| Document | When to read |
|----------|-------------|
| `PROJECT_STRUCTURE.md` | Map of `src/`, `qml/`, `resources/`, signal/slot flow, profile pipeline |
| `CI_CD.md` | Release process, GitHub Actions workflows, version bumping |
| `PLATFORM_BUILD.md` | CLI build commands (Windows/macOS/iOS), Windows installer, Android signing, tablet quirks |
| `RECIPE_PROFILES.md` | Recipe Editor, D-Flow/A-Flow/Pressure/Flow types, frame generation, JSON format, stop limits, profile_sync tool |
| `RECIPES.md` | Drink recipes (add-recipes): data model, recipe-owned grind, steam block, single activation path, promote-from-shot, MCP/web surfaces. NOT the profile Recipe Editor — that is `RECIPE_PROFILES.md` |
| `TESTING.md` | Test framework, mock strategy, adding new tests, **`shot_eval` harness + regression corpus** |
| `BLE_PROTOCOL.md` | BLE UUIDs, retry mechanism, shot debug logging, battery/steam control |
| `VISUALIZER.md` | DYE metadata, profile import/export, ProfileSaveHelper, filename generation |
| `DATA_MIGRATION.md` | Device-to-device transfer architecture and REST endpoints |
| `STEAM_CALIBRATION.md` | Postmortem on the removed steam calibration feature |
| `CUP_FILL_VIEW.md` | CupFillView layer stack, GPU shaders, updating cup images |
| `EMOJI_SYSTEM.md` | Twemoji SVG rendering, adding/switching emoji sets, **where emoji earn their place and where they do not** |
| `ACCESSIBILITY.md` | TalkBack/VoiceOver rules, focus order, anti-patterns, implementation plan |
| `AUTO_FLOW_CALIBRATION.md` | Auto flow calibration algorithm, batched median updates, windowing, convergence |
| `SAW_LEARNING.md` | Per-(profile, scale) stop-at-weight learning |
| `FIRMWARE_UPDATE.md` | DE1 firmware update flow, source URL, validation rules, failure modes |
| `MCP_SERVER.md` | Full MCP tool list, access levels, architecture, data conventions |
| `AI_ADVISOR.md` | AI dialing assistant design |
| `BEAN_BASE.md` | Loffee Labs Bean Base integration: API quirks (whole-word search, tier-gated fields, 429 classification), snapshot-not-reference rule, lock-follows-the-data UI, Visualizer canonical-id architecture |
| `SETTINGS.md` | Settings architecture: 12 domain sub-objects, how to add properties/domains, QML access pattern, build-blast rules |
| `QML_GOTCHAS.md` | QML bug-prone patterns with code samples (font conflict, reserved names, IME drop, etc.); **how to expose C++ to QML** (always compile-time, never `setContextProperty`/runtime `qmlRegister*`); **how to read the qmllint gate** |
| `QML_NAVIGATION.md` | StackView page navigation, phase-change handler, operation-page conventions |
| `SHOTSERVER.md` | ShotServer file split, async community endpoints, JS `fetch()` rules |
| `WIDGET_SNAPSHOT.md` | iOS/Android Home Screen widget: snapshot JSON schema, transport, phase-label table, display/staleness rules |
| `LOGGING.md` | **Adding any log line to a registered subsystem, or a new subsystem.** Marker grammar, the three tiers and how audience (not importance) picks one, the helper headers, the four failure modes that keep recurring, what a session marker asserts and why trimming may not write one, how to retrieve a subsystem's story from a submitted log and over MCP, and the per-PR gate that enforces it |
| `BUILD_PERFORMANCE.md` | Why a C++ change recompiles every QML file, what it costs, qmlcachegen AOT coverage and whether it earns its keep |

Read [`docs/SHOT_REVIEW.md`](https://github.com/Kulitorum/Decenza/blob/main/docs/SHOT_REVIEW.md) when working on the post-shot review / shot detail pages, the five quality-badge detectors (pour truncated, channeling, grind issue, temperature unstable, skip-first-frame), the Shot Summary dialog, badge persistence, or `src/ai/shotanalysis.{h,cpp}`. It is the source of truth for detector internals, gate semantics, and the recompute-on-load contract; keep it in sync when changing any of the above.

## Development Environment

- **ADB path**: `/c/Users/Micro/AppData/Local/Android/Sdk/platform-tools/adb.exe`
- **Uninstall app**: `adb uninstall io.github.kulitorum.decenza_de1`
- **WiFi debugging**: `192.168.1.212:5555` (reconnect: `adb connect 192.168.1.212:5555`). The DHCP lease can rotate — if reconnect fails, plug in USB and run `adb shell ip route | grep wlan` to read the current IP, then `adb tcpip 5555` + `adb connect <ip>:5555`.
- **Qt version**: 6.11.1
- **Qt path**: `C:/Qt/6.11.1/msvc2022_64`
- **Qt sources**: `~/Qt/6.11.1/Src` (macOS) — the full source tree for the exact version we build against. **Read it instead of guessing at Qt behaviour, and cite file-and-line in any comment that asserts what Qt does.** An un-sourced claim in a comment gets believed and then licenses wrong code: both bugs below were written as settled fact first and found by opening the source much later.
  - Error handling is not inferable from the enum names — several `errno` values collapse onto one `QAbstractSocket::SocketError`, per platform, and `connectToHost()` with an IP literal can emit `errorOccurred` **synchronously** (`qtbase/src/network/socket/qnativesocketengine_unix.cpp`, `qabstractsocket.cpp`).
  - Neither is the object model — a registered singleton with no instance is **truthy**, not `undefined` (`qtdeclarative/src/qml/jsruntime/qv4qmlcontext.cpp:229`). See `QML_GOTCHAS.md`.
- **qtbase checkout**: `~/Development/GitHub/qtbase` — separate git clone with the Gerrit remote, for upstream patches. Not the copy to read for reference (it sits on whatever contribution branch is in flight); use `~/Qt/6.11.1/Src` for that.
- **C++ standard**: C++17
- **de1app source**: `C:\code\de1app` (Windows) or `/Users/jeffreyh/Development/GitHub/de1app` (macOS) — original Tcl/Tk DE1 app for reference
- **IMPORTANT**: Use relative paths (e.g., `src/main.cpp`) instead of absolute paths (e.g., `C:\CODE\de1-qt\src\main.cpp`) to avoid "Error: UNKNOWN: unknown error, open" when editing files

## Building

**An assistant builds and runs tests through the Qt Creator MCP tools — `mcp__qtcreator__build` and `mcp__qtcreator__run_tests` — and through nothing else.** Not `cmake --build`, not `ctest`, not a `./tests/tst_*` binary, from any shell. That holds for the full pre-PR suite, for a single target, and when an MCP call times out (the call's wait can abort while Qt Creator keeps building — poll `get_build_status`, don't shell out). If the MCP path is blocked — wrong startup project, app holding the binary, tool unavailable — **stop and ask**. Qt Creator is also ~50× faster than a CLI build, and it is the environment the maintainer is watching while you work.

The `cmake`/`ctest` invocations in `docs/CLAUDE_MD/TESTING.md` and `docs/CLAUDE_MD/PLATFORM_BUILD.md` are **reference for humans and CI**, not instructions for an assistant. Run their equivalent through the MCP.

**No CI job builds or tests a pull request *automatically* — run the full suite locally before opening one.** That is the default gate, via `mcp__qtcreator__run_tests` (scope `all`). Nothing on GitHub fires on its own to catch a compile error or a failing test before merge.

**But CI can be pointed at a branch on demand, and for the test suite that is `linux-release.yml`.** It configures `-DBUILD_TESTS=ON` and runs `ctest --output-on-failure --repeat until-pass:3`, then reports any test that only passed on retry. It is `workflow_dispatch`, so it runs only when dispatched:

```bash
gh workflow run linux-release.yml --ref <branch> -f upload_to_release=false
```

Read "no PR CI" as "nothing runs unless you ask", not "there is no way to get CI to build and test this branch" — the second reading is wrong, and it is how this file was previously read into telling a contributor the suite could only be run locally. The other release workflows dispatch the same way and are the right call for platform-specific changes; `linux-release.yml` is the one that runs tests. Note what a Linux run does **not** cover: `ENABLE_TSNET` is OFF there, so anything reached only through `cmake/tsnet.cmake` is not exercised, and platform-guarded code is compiled only by that platform's own workflow.

That is narrower than "there is no PR CI", which this file used to say and which is wrong: `text-invariants.yml` **does** run on `pull_request`, filtered to the surfaces in its own `paths:` list — **`src/**` among them**. (Not enumerated here: the two copies of that list in this repo's docs were each already one entry short.)

**`src/**` is in that list, so a pure C++ change is gated too**, and this file used to omit it. That omission is what let a merge land red: `check_log_markers.py` rejected a new `[Equipment]` line in `maincontroller.cpp`, the PR was merged without its own run being read, and `main` went red. Nothing blocks that — it is not a required status check, by design, because requiring it would put a check on the critical path of every push. **Read the run before you merge; the gate cannot do it for you.** It is build-free by design — pure Python over the source, no Qt, no compile, seconds (the workflow header carries the measured figure) — which is exactly why it can afford to run per-PR while nothing else does. Read it as the shape a new PR-time check has to fit: if a check needs the app built, it does not belong there.

Everything else is post-merge or on demand — "on demand" meaning the `workflow_dispatch` release workflows above, which is a real option and not a euphemism for "never". `nightly-sanitizers.yml` re-runs the suite on `main` each night under UBSan and ASan. Platform-guarded code (`#ifdef Q_OS_IOS` etc.) is compiled only by the tag-push release workflows, so verify platform-specific changes with a CI test build of that platform (see `docs/CLAUDE_MD/CI_CD.md`).

**Debug builds are sanitizer-instrumented automatically** — ASan *and* UBSan on every desktop platform including macOS, so a normal local test run already reports undefined behaviour and memory errors. UBSan is in recovering mode there (it reports and continues); an explicit `-DENABLE_UBSAN=ON` gives the halting mode CI uses. Release builds are untouched.

**But LeakSanitizer does not exist on macOS, so a green local run says nothing about leaks.** `ASAN_OPTIONS=detect_leaks=1` is refused outright — `AddressSanitizer: detect_leaks is not supported on this platform`. LSan is Linux-only, so on a Mac ASan covers use-after-free, buffer overflow and double-free, and cannot see a leak at all. The nightly Linux ASan job is the only place leaks are detected; it found two the local suite had passed over for months. To chase a leak on macOS use `leaks <pid>` or `MallocStackLogging=1`, not ASan.

## Project Structure

See `docs/CLAUDE_MD/PROJECT_STRUCTURE.md` for the full source tree, signal/slot flow, scale system, machine phases, AI/MCP overview, and profile pipeline. Top-level: `src/` (C++), `qml/` (UI), `resources/`, `shaders/`, `tests/`, `docs/`, `openspec/`, `android/`, `installer/`.

## Conventions

### Design Principles
- **Never use timers as guards/workarounds.** Timers are fragile heuristics that break on slow devices and hide the real problem. Use event-based flags and conditions instead. For example, "suppress X until Y has happened" should be a boolean cleared by the Y event, not a timer. Only use timers for genuinely periodic tasks (polling, animation, heartbeats) and **UI auto-dismiss** (toasts/banners that hide after N seconds). Everything else — including debounce — should use event-based flags.
- **Keep main-thread database and disk I/O off anything a user can feel, and off anything that could interfere with making coffee.** That is what the rule protects — it is not a ban on touching `QSqlQuery` from the main thread, and reading it that way produces caches far more expensive than the thing they avoid. Judge by magnitude and by where the work sits:
  - **Thread it** when the read is unbounded or grows with history (shot lists, `debug_log`/`profile_json` blob reads, exports, backups, migrating existing rows), or when it sits on a repeating path — a binding that re-evaluates on every change, a model delegate, anything per-frame or per-sample. Use `QThread::create()` with a `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` callback; `ShotHistoryStorage::requestShot()` is the canonical pattern. For connections inside background threads always use `withTempDb()` from `src/core/dbutils.h` — unique connection naming, `busy_timeout`, `foreign_keys`, cleanup. Never hand-roll `QSqlDatabase::addDatabase()`/`removeDatabase()` where `withTempDb` fits.
  - **Inline is fine** for a small bounded read on a discrete user action. Measure it on a realistic database and on a multiple of one (a scan's cost tracks table BYTES, not row count: a page read drags the blobs along), then put the median AND the worst case in a comment at the call site so the next person inherits the evidence rather than the conclusion. **Confirm "discrete" against the QML, not against intent.** Two shapes to check, both of which shipped wrong in one PR. A `readonly property` on a component the layout keeps resident evaluates at construction and on every dependency change, not when the user opens anything — `grindStepForGrinder()` was documented as running "when the grind picker opens" while its binding re-ran on an unrelated signal across every live `GrindRowSource`, because `GrindQuickSelectItem` is a bar widget. And a binding that reads a property the user's typing rewrites runs **per keystroke**: `getDistinctBaristas()` sat inside a `suggestions:` binding that read `editBarista`, which `onTextEdited` rewrote on every character. Trace each binding's dependencies to a writer and count the evaluations before claiming a read is on a user action. The fix for the keystroke case is to hoist the query to a property refreshed at defined moments, not to thread it.
  - **While the machine is running** — shot, steam, flush, hot water — the main thread is carrying BLE traffic and live telemetry, so the budget is much tighter than at idle, but it is still a budget, not a prohibition. A few ms against a ~100 ms sample interval is not a hazard; tens of ms is. Anything that could delay a stop-at-weight or a stop command belongs on a thread regardless of its average.
  - **The tradeoff runs both ways, and the expensive side is usually the cache.** `ShotHistoryStorage`'s distinct-value cache existed to keep dialog and picker reads off the database, and it was invalidated *more often than it was read* — every shot save, delete and metadata edit wiped it and kicked a six-query refresh, to serve dialogs that might never open. Its invalidation also dropped composite keys it never refilled: a correctly-derived grind step of 0.25 lost, the re-fetch discarded mid-flight, and the picker stuck on 1.0 for the session. (That cache bug has no issue number of its own — it was found from a device log. Do not cite #1713 for it: #1713 is the equipment-identity fork, a different mechanism that happens to produce the same visible symptom.) Deleting it removed the bug rather than fixing it. Measurements live at the call sites (`queryDistinctList`, `grinderWideNumericSettings` and `grinderWideRpms` in `shothistorystorage_queries.cpp`), not here — next to the code they justify. Pay for threading or caching where a user or the machine would notice. Nowhere else.
- **A configured API key is not permission to spend on it.** Any feature that calls a paid AI provider uses the provider AND model the user selected, and nothing else. Never fall back to another provider because one errored or rate-limited, and never pick a provider because it happens to have a key — a user with OpenAI selected must not get billed on Anthropic. When the selected provider fails, stop and report which provider failed at what; a run that reports success having silently used something else is worse than a run that ends. Decenza's bulk translator did exactly this for months, hard-ordering "Claude first (best quality), then OpenAI", and the silent substitution hid a retired Anthropic model that made every Anthropic request 404 — visible only to users with no second key.
- **A test has to be able to fail, has to catch something the suite does not, and costs BUILD time rather than run time.** The suite runs in ~31 s; an individual test is 50-80 ms — noise. The cost is that `tests/` is **40% of a clean build** (1956 s of 4846 s cpu) and the suite doubled in July 2026 alone. Where that goes is not where it is usually assumed: production sources recompiled inside test targets 35.2%, test source TUs 31.9%, moc 26.5%, qrc 3.4%, **link 3.0%**. Those shares are contended wall clock from a parallel build, so read them as proportions, never as durations. **"Target count is the smallest lever" is true of LINK and false of compile** — that sentence used to stand unqualified here and it pointed at the wrong unit. Measured since: `#include <QtTest>` alone costs 1.42 s and a whole 60-line test file costs 1.45 s, so ~90% of a test TU's compile is fixed per FILE and ~10% scales with lines (~1.4 s + ~0.83 ms/line, r=0.35 over 109 TUs). A new `tst_*.cpp` costs ~1.4 s forever; a new test slot in an existing file costs milliseconds. So make a new test FILE hard to justify and a new test FUNCTION easy, and do not expect deleting tests to buy build time — an audit of all 2,515 tests found 93 mergeable into `_data()` tables and 239 under three lines, together worth under a second. The lever that did work was precompiled headers (`DECENZA_ENABLE_PCH`), 31.6% off a clean rebuild with no test deleted. Say what defect shape a new test catches that no existing test catches; if you cannot, extend the existing assertion instead, and if it is more cases of an already-covered shape, call it insurance and argue it on risk. One invariant is asserted in one place. Shared production sources go into a **narrow** intermediate library linked by exactly the targets that need them, never into the universally-linked `decenza_testlib` — that trades a compile fan-out of 9 for a link fan-out of 106 and recovers only a seventh of what a narrow library does (measured: touching `profilemanager.cpp` cost 77.0 s across nine duplicate compiles, 12.2 s after). This is **enforced per-PR** by `scripts/check_test_source_duplication.py` in `text-invariants.yml`, which is why the tree is at zero duplicates rather than carrying an exception list. Before keeping a test, break the code it covers and watch it go red: a test that cannot fail is a comment that compiles, and deleting one is a strict improvement. Full rules, the measured tables, and the three pass-forever shapes that have shipped here, are in `docs/CLAUDE_MD/TESTING.md` under "Adding New Tests".
- **Complexity has to come with a measurable win, stated in units the USER feels.** Not "33.9 ms → 1.18 ms" — *who waits for those milliseconds?* A saving on a background thread, at startup, that nothing blocks on is not a win, however large the ratio. Write the benefit as "X, which the user experiences as Y" before adding the machinery; if Y is empty, so is the case for it. This is the check the rest of the process does not do: build, tests and the PR review agents all ask whether code is CORRECT, and over-built code almost always is — a covering index nobody needed passed 110 tests and was *praised* by the review agents.
  - **Re-derive the justification when the design around it moves.** That index was justified by "a visible hitch every time the grind picker opens" — true of an on-demand read, false the moment the value became resident. The comment was never revisited, so a stale premise kept a schema migration alive through a full review. The same sentence then survived the migration's deletion and was still wrong for a *third* reason (the binding was never on picker-open at all). When you change how something is read or called, re-read the comments justifying its optimisations: they are claims with expiry dates, and a comment nobody re-derives is how a bad decision keeps getting re-approved.
  - **Needing fault injection to reach a branch is a stop sign, not a testing problem.** If the only way to exercise a path is to fake a failure that cannot otherwise occur, ask whether the path should exist before you build the harness. Writing exactly that harness is what exposed the over-build above.
- **Don't engineer around migrations. They run once, with the app stopped.** Every migration executes synchronously inside `ShotHistoryStorage::initialize()` (`runMigrations()` at `src/history/shothistorystorage.cpp:198`), which does not set `m_ready` or emit `readyChanged()` until the whole chain is done (`:243`) — so no reader, background thread or UI, can ever observe a half-migrated database. Check those two lines rather than believing this one. That means the concurrency defences a migration seems to invite are guarding nothing: don't add "is a migration running" flags, don't make readers skip or wait, don't build retry/verify machinery for a state no one can see. Just do the work and stamp the version.
  - **Gate the version bump only on things that are schema facts.** A column, a table, a data backfill — bump only if it landed, because the app would be wrong without it. A performance index is not one of those: the queries return identical results without it, so create it, log the failure if it fails, and bump anyway — gating the version on it only buys a database that fails `CREATE INDEX` once and re-runs the block on every launch forever. The stamp of `schema_version` itself stays transacted, and that one IS a correctness fact: a `DELETE` that commits without its `INSERT` leaves the table empty, and an empty `schema_version` on an already-migrated database is not recoverable — `createTables()` re-seeds it to 1 (`shothistorystorage.cpp:393`), the whole chain then re-runs against tables that already exist, several steps fail, and `initialize()` returns false. Verified by writing that case as a test and watching it strand at version 14. So keep the DELETE and the INSERT in one transaction; do not reach for a `crossedSchemaVersion()` argument, which is a different and largely foreclosed concern.
- **Settings go in their domain sub-object, not on `Settings` directly.** `Settings` is a façade owning 12 domain classes (`SettingsMqtt`, `SettingsTheme`, …). Add properties to the matching `Settings<Domain>`, or add a new sub-object; never back onto `Settings` itself. QML access is **always** `Settings.<domain>.<prop>`, never the flat `Settings.<prop>`. A new sub-object needs three edits and missing the third (`QML_FOREIGN` + `QML_UNCREATABLE` in `settings_qml.h`) resolves to `undefined` at runtime while compiling clean. Two traps that cost real time — the domain types must NOT be erased to `QObject*`, and `Q_DECLARE_OPAQUE_POINTER` is not a way around it — are in `docs/CLAUDE_MD/SETTINGS.md` with the full checklist and the measured build cost. Read it before adding a domain.
- **Centralize anything produced at more than one site — never hand-roll it per call, never copy a helper's body.** A repeated format, prefix, tag, wording, or policy is a drift opportunity: each copy is free to change alone, silently, and nothing fails when one does. Put it behind one function or macro and call that. This is not a tidiness preference, it is how the copies stay true to each other.
  - **Never copy a macro/helper body to specialize it** — alias it. `difluidr1.cpp` and `difluidr2.cpp` each hand-copied `SCALE_LOG`/`SCALE_WARN` from `scalelogging.h` instead of aliasing, so a one-line fix to the shared macro had to be found and applied in three places; the two copies were only still identical by luck. `#define R1_LOG(msg) SCALE_LOG("DiFluidR1", msg)` is the correct shape.
  - **A log/error prefix belongs in one helper, not at the call site.** `usbscalemanager.cpp` wrote `"[USB Scale] "` inline at 73 sites with no helper, and drifted exactly as predicted: at 21 of them the `qDebug` and the `emit logMessage` described the same event in *different words*. Worse, a comment was then written asserting the two were deliberately "separate", which made the drift look like a design. Prefixes also multiplied to four families (`[Scale]`, `[BLE <Driver>]`, `[USB Scale]`, `[WifiScaleDiscovery]`) so no single `grep` returned the whole story — and this subsystem is diagnosed from user-submitted logs, where a reader cannot know which family they forgot.
  - When you find yourself about to write the second copy, stop and extract. When you touch code that already has copies, collapse them in that pass — see the pre-existing-issues rule under Accessibility, which applies to all code, not just QML.

### C++
- Classes: `PascalCase`; methods/variables: `camelCase`; members: `m_` prefix; slots: `onEventName()`
- Use `Q_PROPERTY` with `NOTIFY` for bindable properties
- Use `qsizetype` (not `int`) for container sizes — `QVector::size()`, `QList::size()`, `QString::size()` etc. return `qsizetype` (64-bit on iOS/macOS). Assigning to `int` causes `-Wshorten-64-to-32` warnings.
- **Adding a log line? Read `docs/CLAUDE_MD/LOGGING.md` first — it is a gate, not advice.** `scripts/check_log_markers.py` fails the build-free PR job on a bare `qDebug` in a covered file, a marker typed by hand, or a leading `[Capitalised]` token the registry does not declare — the last only in a log call, in a file one of the glob sets covers. Three things that keep going wrong and are all in that doc:
  - **The tier is chosen by AUDIENCE, not importance.** `INFO` is the user-visible tier: the connections views default to `minLevel INFO` and none of them override it, so a user-facing line left at `DEBUG` is absent from the views entirely. (`debug_get_log` has no such default — it filters only if the caller asks — but it is normally called that way.) The recurring shape is a fault reported at `WARN` whose *resolution* sits at `DEBUG`: the reader sees only the failure half and concludes it never recovered.
  - **Which file you are in decides which rules apply.** A file wholly about one subsystem goes in `COVERED_GLOBS`; a file that merely *hosts* a subsystem's lines beside unrelated code goes in `MARKER_ONLY_GLOBS`. Both lists live in the script and are deliberately not restated in the docs. Touching a file that uses a helper and is in neither list fails the gate.
  - **Nothing blocks a merge on that gate.** It is not a required status check, so a red run is only caught by someone reading it. One was not, and `main` went red.
- A discarded `[[nodiscard]]`/`warn_unused_result` value is a build **error** (`-Werror=unused-result`). To deliberately ignore one, write `(void)call();` with a comment saying why losing that failure is tolerable — never a bare call, never a file-wide suppression. Note the compiler only enforces this for *annotated* APIs: it caught a dropped `SecRandomCopyBytes` (Apple annotates it) and said nothing about the identical dropped `RAND_bytes` (OpenSSL doesn't) — so check unannotated results yourself.

### QML
- Files: `PascalCase.qml` — new QML files **must** be added to `CMakeLists.txt` (in the `qt_add_qml_module` file list) to be included in the Qt resource system. Without this, the file won't be found at runtime.
- **New layout widgets** require registration in 3 places: (1) `CMakeLists.txt` file list, (2) `LayoutItemDelegate.qml` switch, (3) the widget catalog table (`widgetCatalogTable()` in `settings_network.cpp`) — the in-app palette, chip labels, library card, and web editor all derive from that one table. Optionally add to `LayoutCenterZone.qml` if the widget should be allowed in center/idle zones. If the widget has per-instance options, also declare its option keys (and non-text display-mode default, if any) in the readout capability schema (`readoutOptionSchema()` / `displayModeDefaults()` in the same file) — the gear indicator, the unified `ReadoutOptionsPopup`, and the web editor's option forms all derive from it. A non-text display default additionally requires the widget's item component to call `defaultDisplayModeForType()` (item components otherwise hard-code the "text" default).
- **New Custom-widget actions** go in `layoutActionTable()` in `settings_network.cpp` — one row, `{id, labelKey, label, contexts}` — plus the dispatch arm in `CustomItem.executeActionString()`. Both editors (the in-app action picker and the web layout editor) derive from that table; neither carries a list. They used to carry one each, and the copies drifted sixteen entries apart with nothing failing, which is what `tst_customwidgethtml`'s `neitherEditorHardCodesItsOwnActionList` now prevents. A catalog row with no dispatch arm is a button that does nothing — `everyCatalogActionHasADispatchArm` catches that. `inPicker=false` marks a legacy id that still needs a label but is not offered.
- IDs/properties: `camelCase`
- Use `Theme.qml` singleton for all styling — never hardcode colors, font sizes, spacing, or radii. Use `Theme.textColor`, `Theme.bodyFont`, `Theme.subtitleFont`, `Theme.spacingMedium`, `Theme.cardRadius`, etc.
- All user-visible text must be internationalized. Use `TranslationManager.translate("section.key", "Fallback text")` for property bindings and inline expressions. Use the `Tr` component for standalone visible text (`Tr { key: "section.name"; fallback: "English text" }`). For text used in properties via `Tr`, use a hidden instance: `Tr { id: trMyLabel; key: "my.key"; fallback: "Label"; visible: false }` then `text: trMyLabel.text`. Reuse existing keys like `common.button.ok` and `common.accessibility.dismissDialog` where applicable.
  - **A binding over `translate()` re-evaluates on a language change, and the plain call above is all you need.** This is worth stating because it was NOT always true: `translate` used to be a `Q_INVOKABLE`, a binding calling it recorded no dependency, and 3,248 call sites written exactly as documented here froze on whatever language was active when the page was built. It is now a `Q_PROPERTY` holding a callable, so reading `TranslationManager.translate` establishes the dependency — see `translationmanager.h`.
  - Do **not** add a `var _ = TranslationManager.translationVersion` line to new code. It is redundant now; `Tr.qml` keeps one only as a historical marker. If you find yourself reaching for it because text is stale, the mechanism is broken — `tests/tst_translationreactivity.cpp` should be failing, and that is the thing to fix.
- Use `StyledTextField` instead of `TextField` to avoid Material floating label
- `ActionButton` dims icon (50% opacity) and text (secondary color) when disabled

### Using emoji well

The app ships the complete Twemoji set (~4,000 SVGs, MIT), resolved locally. **Reach for emoji
where they make a screen easier to scan** — but never as the only carrier of meaning, never more
than one per label, and never in place of a themed SVG for toolbar/navigation chrome. Full
guidance on where they earn their place (and where they do not) is in
`docs/CLAUDE_MD/EMOJI_SYSTEM.md`.

**The one rule that is not stylistic:** render through `Theme.emojiToImage()` (for an `Image`) or
`Theme.replaceEmojiWithImg()` (for text with emoji inline). An emoji in a plain `Text` lets a
colour glyph reach the platform renderer, which **crashes the render thread on macOS**.

### QML Gotchas (one-liners — full samples in `docs/CLAUDE_MD/QML_GOTCHAS.md`)

- **Exposing a C++ type or object to QML is a macro in a header, never `setContextProperty()` and
  never a runtime `qmlRegisterType<>()`.** Both are invisible to qmllint, `qmlcachegen` and the
  language server, and a context property is indistinguishable from a typo — the #1661 defect
  class. The table of which macro to use, the two mechanical traps (include directory,
  `qt_add_qml_module` `DEPENDENCIES`) and how to read the gate are in `QML_GOTCHAS.md`.
- **A qmllint count going UP after a fix is usually the fix working** — resolving a type lets the
  linter reach expressions it previously abandoned. Diff the per-file and per-category sets
  before calling it a regression; totals alone mislead. Likewise, many `Member "x" not found on
  type "QObject"` means an erased pointer type in C++, not a mistake in the QML.

- **Never directory-import a type the module already provides.** `import Decenza` covers every file
  in `QML_FILES`; an extra `import "../components"` re-resolves the same files as plain component
  types and **shadows the singleton registration**, so `DrinkType.shortLabel` and
  `SettingsTabs.indexOf` read as missing members while being plainly declared. 106 were deleted.
- **A page never touches `pageStack` or main.qml's `root`.** It emits an `AppShell` signal and the
  shell decides. Navigation policy: replace when the MACHINE drove the change, push when the USER
  did. See `QML_NAVIGATION.md`. Status-bar widgets are tappable from their own destination, so a
  destination pushes through `pushUnlessCurrent()` rather than `pageStack.push()` directly.
- **A registered singleton with no instance is TRUTHY, not `undefined`.** `typeof X !== "undefined"
  && X` passes and the first member call throws. Qt builds the type wrapper whether or not
  `singletonInstance()` returned anything; only the member read degrades to `undefined`. Guard the
  member (`X.doThing !== undefined`) or the platform, never the name. Full sources in
  `QML_GOTCHAS.md`.
- **`pragma ComponentBehavior: Bound` breaks delegates that take injected model roles**, at runtime
  and silently. Check for `Repeater`/`delegate:` first; with none, the pragma alone is safe. With
  delegates, add `required property` to each in the same edit.
  - **The same break comes from ONE `required property` anywhere on a delegate — no pragma
    involved.** A delegate with any required property stops receiving model roles as context
    properties, so bare `modelData` / `index` / `model` go `undefined` everywhere in it,
    including nested scopes. Adding a required property to a delegate's **base type** does it
    too, at a distance: making `RepeaterDelegateItem.itemIndex` required broke `modelData` in
    three delegates in other files that had never declared a required property themselves.
    Declare every role the delegate reads, in the same edit.
  - Neither failure is visible to the compiler, to qmllint, or to the test suite — the symptom
    is `ReferenceError: modelData is not defined` in the running app. If you touch a delegate
    or its base type, open the screen.
- **When qualifying identifiers, go by qmllint's line and column, never by text search.** One name
  can be several things in a file (a function-local `var step` and a nested `property var step`),
  and only the flagged occurrences may move.
- **A nested event loop reachable from a QML signal handler is a crash, not a slowdown.**
  `processEvents()` / `exec()` below a handler delivers queued events, and one that destroys an
  object whose handler is still running makes Qt `qFatal()`
  (`qtdeclarative/src/qml/qml/qqmlengine.cpp:1370-1396`) — shipped iOS 2.0.0 aborted this way
  (#1692). Long work goes on a worker thread with results posted back queued, never pumped inline;
  a progress bar is not a reason to pump. Which posted event did it is NOT established, and the
  obvious guess (a `DeferredDelete`) is gated — see `QML_GOTCHAS.md` for the sources.
- **Font property conflict**: don't mix `font: Theme.bodyFont` with `font.bold: true` — assign sub-properties individually.
- **Reserved names in JS model data**: `name`, `parent`, `children`, `data`, `state`, `enabled`, `visible`, `width`, `height`, `x`, `y`, `z`, `focus`, `clip` collide with QML properties — use `label` etc.
- **IME last-word drop**: call `Keyboard.commit()` before reading any `TextField.text` from a button handler — otherwise the in-progress word is lost on mobile. (`Keyboard` is a compile-time singleton, `src/core/keyboard.h`. It replaced `Qt.inputMethod`, which qmllint types as a bare `QObject` — so a typo in the call was uncheckable, and its failure mode is silent.)
- **Keyboard handling**: wrap pages with text inputs in `KeyboardAwareContainer { textFields: [...] }`.
- **FINAL Qt properties**: don't redeclare `message`/`title` etc. on `Popup`/`Dialog` (Qt 6.10+) — pick a different name like `resultMessage`.
- **Numeric defaults**: use `value ?? 0.6`, not `value || 0.6` — `||` treats `0` as falsy.
- **`native` is reserved**: use `nativeName`.
- **Emoji are encouraged; non-emoji text symbols are not.** These are two different things and only one is safe:
  - **Emoji** (`☕` `⚙️` `⚠️` `🔒`) never reach the text renderer — the app ships the complete Twemoji set (~4,000 SVGs) and every emoji is rewritten to a bundled `<img>`. Metrics are identical on every platform because it is an image, not a glyph. **Use them where they earn their place** (see "Using emoji well").
  - **Non-emoji text symbols** (`→` `←` `↗` `↕` `▶` `◀` `⧉`) are ordinary font glyphs and are now **fine to use**. Decenza Sans has only 927 glyphs and none of these, so the app also bundles **Noto Sans Math** (SIL OFL) as a symbol fallback, chained after the UI family in `Theme.fontFamilies` and on the application font. Qt consults it only for codepoints the primary lacks, so symbols come from the bundle, render identically on every platform, and stay monochrome — they take the element's colour like the text around them, which is exactly what emoji cannot do.
    - Before using a symbol not already in the app, check it: `python3 scripts/check_font_glyph_coverage.py` reads both cmaps and reports anything that would still fall through to the host. If something you want is uncovered, add a second OFL face rather than reaching for an emoji.
    - **This was previously written as an absolute ban justified by #1537. That citation does not support the ban** — #1537 dropped the "fi" ligature from "Profile", a word entirely inside the bundled font, so whatever its cause, it was not a missing glyph and cannot justify a rule about fallbacks. Nothing here has ever been traced to a missing glyph. Note that #1537's actual cause is still open: `src/main.cpp` carries two candidate explanations and states plainly that they are not reconciled — do not repeat either as settled. Kept on the record so the ban is not reinstated from memory.
    - **Still avoid a bare U+FE0F on a symbol** (`▶️` rather than `▶`). That is an explicit request for colour-emoji presentation, and in a plain `Text` — which is what `AccessibleButton.text` and most labels are — it is the macOS render-thread crash path. Adding the variation selector makes a working symbol worse, not better.
    - A symbol is still not a substitute for an **icon** in chrome. `qrc:/icons/` SVGs follow `Theme.iconColor` and scale as artwork; a glyph is text that happens to look like a picture.
  - Rule of thumb: colour picture in the emoji keyboard → emoji, fine. Line-drawing symbol in your text colour → font glyph, also fine now, but confirm coverage with the script. Toolbar/navigation affordance → neither; use a themed SVG.
- **The bundled font covers Latin (incl. Extended), Greek and Cyrillic only.** In CJK, Arabic, Hebrew, Devanagari and Thai locales every glyph comes from a platform fallback, so the metric determinism the bundled font provides does **not** apply there. Layout tolerance (wrap/elide/content-driven sizing) is what keeps those UIs from clipping — never rely on a fixed width that only fits the design font.
- **`elide` is dead on `Text.RichText`**: prefer `Text.StyledText` for HTML-ish labels (elide works, and it's lighter); RichText silently disables `elide` → mid-glyph clipping.
  - **But `StyledText` cannot render CSS.** It has no `<span>` handler and never reads a `style=` attribute. The only tag whose attributes reach the character format is `<font>` (`qquickstyledtext.cpp:421-422`), and the `size` it accepts there is an HTML 1-7, not px (`:566-572`). So a `<span style="color:…; font-size:…px">` is dropped silently — text renders at the default colour and size. (`<a>`, `<img>`, `<ol>` and `<ul>` attributes *are* parsed, they just carry no styling; `<img>` is what makes `Theme.replaceEmojiWithImg` work under StyledText.) If the markup carries CSS, `Text.RichText` is the only option — and it ignores **both** `elide` and `maximumLineCount`, so bound the paint with `clip: true` and don't declare either. This is not hypothetical: it is what made the custom-widget editor's colour and S/M/L/XL rows decorative for their whole life, with the preview sharing the blindness so nothing on screen contradicted the save.
- **Measuring text in a binding**: use `FontMetrics.advanceWidth(str)`, never a mutated `TextMetrics` (`.text=`/read `.width`) — the latter self-triggers a binding loop. Mutated `TextMetrics` is only safe in an imperative Timer/handler writing a plain property. Runtime-only; a clean build won't catch it.
- **Accessibility on interactive elements**: every interactive element needs `Accessible.role`, `Accessible.name`, `Accessible.focusable: true`, and `Accessible.onPressAction`. Prefer `AccessibleButton` / `AccessibleMouseArea` over raw `Rectangle+MouseArea`. Full rules in `docs/CLAUDE_MD/ACCESSIBILITY.md`.

### MCP Tool Responses (`src/mcp/`)

MCP tool responses are consumed by LLMs which cannot reliably interpret raw numbers. Follow these conventions:

- **Never return Unix timestamps.** Use ISO 8601 with timezone: `dt.toOffsetFromUtc(dt.offsetFromUtc()).toString(Qt::ISODate)` → `"2026-03-21T11:20:41-06:00"`
- **Include units in field names.** `doseG` (grams), `pressureBar`, `temperatureC`, `flowMlPerSec`, `durationSec`, `weightG`, `targetVolumeMl`. An AI seeing `"pressure": 9.0` doesn't know bar vs PSI vs kPa.
- **Include scale in field names for bounded values.** `enjoyment0to100` instead of `enjoyment`.
- **Use human-readable strings for enums.** Machine phases, editor types, and states as strings (`"idle"`, `"pouring"`), not numeric codes.

See `docs/CLAUDE_MD/MCP_SERVER.md` for the full data conventions section.

## Subsystem Pointers

- **Profiles, JSON format, stop limits, profile_sync**: `docs/CLAUDE_MD/RECIPE_PROFILES.md`
- **QML page navigation, operation pages, phase-change handler**: `docs/CLAUDE_MD/QML_NAVIGATION.md`
- **ShotServer (split files, async community endpoints, fetch rules)**: `docs/CLAUDE_MD/SHOTSERVER.md`
  - **ShotServer pages must match the app in look AND features, not look half-finished.** When a ShotServer web page mirrors an in-app screen (e.g. `/beans`, `/recipes`, `/equipment`, shot history), design it to closely match the app's clean version — same information hierarchy, card grammar, active-item highlight, empty states, and canonical page chrome (the `<header class="header">` logo + back + burger menu, the shared embedded-page style) — AND aim for feature parity: every field and action the app offers on that screen should be reachable from the web, rather than shipping a bare demo-style subset.
  - **Reuse, don't copy.** Build shared page style/shell/JS helpers instead of re-inlining per page, and reach parity features by reusing existing backends (e.g. `BeanBaseClient`, the storage classes, the patterns proven by the MCP tools and async community endpoints) rather than re-implementing them.
  - **Keep the two surfaces in sync.** When you change an in-app page that also exists in the ShotServer (or vice-versa), update the counterpart in the same change so they don't drift. Add a task for it in the change's `tasks.md`.
- **Emoji**: always `Image { source: Theme.emojiToImage(value) }` — never `Text` for emojis. See `EMOJI_SYSTEM.md` and "Using emoji well".
- **Cup Fill View**: `docs/CLAUDE_MD/CUP_FILL_VIEW.md`
- **Data migration (device-to-device)**: `docs/CLAUDE_MD/DATA_MIGRATION.md`
- **Visualizer integration**: `docs/CLAUDE_MD/VISUALIZER.md`
- **Unit testing** (Qt Test, `friend class` access behind `#ifdef DECENZA_TESTING`, build with `-DBUILD_TESTS=ON`, `shot_eval` harness, `tests/data/shots/` regression corpus): `docs/CLAUDE_MD/TESTING.md`
- **BLE protocol**: `docs/CLAUDE_MD/BLE_PROTOCOL.md`
- **CI/CD, releases, auto-update**: `docs/CLAUDE_MD/CI_CD.md`
- **Windows installer / Android build / tablet quirks**: `docs/CLAUDE_MD/PLATFORM_BUILD.md`

## Platforms

- Desktop: Windows, macOS, Linux
- Mobile: Android (API 28+), iOS (17.0+)
- Android needs Location permission for BLE scanning

## Versioning

- **Display version** (versionName): Set in `CMakeLists.txt` line 2: `project(Decenza VERSION x.y.z)`
- **Version code** (versionCode): Stored in `versioncode.txt`. Does **not** auto-increment during local builds. CI workflows bump it on tag push, and the Android workflow commits the new value back to `main`.
- **version.h**: Auto-generated from `src/version.h.in` with VERSION_STRING macro
- **AndroidManifest.xml**: Auto-generated from `android/AndroidManifest.xml.in` by CMake at build time (gitignored). Both `versionCode` and `versionName` come from `versioncode.txt` and `CMakeLists.txt` respectively.
- **installer/version.iss**: Auto-generated from `installer/version.iss.in` by CMake at build time (gitignored).
- To release a new version: Update VERSION in CMakeLists.txt, commit, then follow the "Publishing Releases" process in `docs/CLAUDE_MD/CI_CD.md` (create release first, then push tag)

## Git Workflow

- **Standard merge: squash + delete branch.** Every PR lands on `main` as a single squashed commit, and the feature branch is deleted on the remote (and locally if you're on it). The `merge-pr` skill (`.claude/skills/merge-pr/SKILL.md`) automates this — invoke it via `/merge-pr` or whenever the user says "merge". Equivalent CLI: `gh pr merge <num> --repo Kulitorum/Decenza --squash --delete-branch`. Do not use `--merge` (true merge commit) or `--rebase` unless the user explicitly asks for them.
- **Version codes are managed by CI** — local builds use `versioncode.txt` as-is (no auto-increment). All 6 CI workflows bump the code identically on tag push. The Android workflow commits the bumped value back to `main`.
- You do **not** need to manually commit version code files — only `versioncode.txt` is tracked. `android/AndroidManifest.xml` and `installer/version.iss` are generated from `.in` templates by CMake at build time and are gitignored.

## Accessibility (TalkBack/VoiceOver)

See `docs/CLAUDE_MD/ACCESSIBILITY.md` for the full reference: component rules, focus-order requirements, anti-patterns, common mistakes checklist, and the page-by-page implementation plan for [Kulitorum/Decenza#736](https://github.com/Kulitorum/Decenza/issues/736).

**Key rule for modifying existing components**: Fix pre-existing violations in any file you touch — do not dismiss them as "pre-existing". Issues compound over time and each change is an opportunity to fix them.
