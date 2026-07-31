# Unit Testing

## Framework

Qt Test (QTest) — ships with Qt, no external dependencies, integrates with CMake's `ctest`.

## Building and Running

> **Assistants: use the Qt Creator MCP, not the commands below.** `mcp__qtcreator__run_tests`
> (`scope: "all"`, or `scope: "named"` for one test) builds and runs, and returns a pass/fail
> summary; `mcp__qtcreator__build` is the build half. The `cmake`/`ctest`/`./tests/tst_*` lines
> in this file are reference for **humans and CI**. Never run them from a shell — including for
> the full pre-PR suite, a single target, or after an MCP call times out. If the MCP path is
> blocked, stop and ask. See the Building section of the root `CLAUDE.md`.

Tests are **auto-enabled in Debug builds** (single-config generators like Ninja/Make) and **in Linux CI releases**. Multi-config generators (Visual Studio, Xcode) require `-DBUILD_TESTS=ON` explicitly since `CMAKE_BUILD_TYPE` is empty at configure time.

```bash
# Debug build — tests included automatically
cmake -G Ninja -DCMAKE_PREFIX_PATH=~/Qt/6.11.1/macos -DCMAKE_BUILD_TYPE=Debug ..

# Release build — tests off by default, opt-in with:
cmake -DBUILD_TESTS=ON -G Ninja -DCMAKE_PREFIX_PATH=~/Qt/6.11.1/macos -DCMAKE_BUILD_TYPE=Release ..

# Run all tests — in parallel (the suite is parallel-safe; see below)
ctest --output-on-failure -j$(nproc) --repeat until-pass:3   # macOS: -j$(sysctl -n hw.ncpu)

# Run a specific test
./tests/tst_sav
./tests/tst_saw
```

Override with `-DBUILD_TESTS=OFF` (Debug) or `-DBUILD_TESTS=ON` (Release) as needed.

### Parallel runs

Always run with `-j` — the full suite drops from ~140 s to ~30 s. It is parallel-safe: every settings handle is an `AppSettings`, which under `DECENZA_TESTING` resolves to `Settings::testQSettingsPath()` (a PID-scoped `IniFormat` temp file, see `appsettings.cpp`), so no two test processes share an on-disk store and the suite never mutates the developer's real preferences.

When seeding raw pre-construction settings state in a test, construct an `AppSettings` exactly like production code does — never a bare `QSettings`, which escapes the isolation and writes to the developer's real store. `tst_appsettings` enforces this across `src/`.

`--repeat until-pass:3` covers three tests that can miss a timing window under heavy CPU contention when many run at once. The retry re-runs only a failed test; a genuine regression fails all three attempts. In Qt Creator's CTest settings you can add the same flag, or just re-run a lone flaky result.

- `tst_settling` — feeds samples at real `qWait` intervals and asserts plateau detection over time windows.
- `tst_decentscalewifi`.
- `tst_coffeebags::settingsDyeKeepFieldsStillResolvesTheDoseRung` — observed failing twice in five full runs (July 2026), always this one function, always passing in isolation and on retry. It fails via `failOnWarning`, not an assertion: the `bagReady` handler in `settings_dye.cpp:56` warns "active bag N not found - clearing selection" when the signal arrives with an empty map, which under load can happen because the async bag read races the test's raw-connection seed. **This is a real race in the test setup, not just slowness** — the mechanism is written down here so whoever fixes it does not have to rediscover it. Until then the retry covers it.

### Running under UBSan

Debug builds instrument automatically (see below). To run an explicit instrumented suite the way nightly CI does:

```bash
# Separate build dir — sanitized objects don't mix with your normal build
mkdir build-ubsan && cd build-ubsan
cmake -G Ninja -DCMAKE_PREFIX_PATH=~/Qt/6.11.1/macos -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_TESTS=ON -DENABLE_UBSAN=ON ..
ninja

# halt_on_error=1 is essential: without it UBSan prints the diagnostic and the
# test still exits 0, so a green run can hide findings.
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
  ctest --output-on-failure -j$(sysctl -n hw.ncpu) --repeat until-pass:3
```

**Debug builds instrument themselves.** A Debug configure turns on ASan *and* UBSan on every desktop platform including macOS (Android is excluded — its ASan needs `wrap.sh` packaging this project does not do). UBSan runs in **recovering** mode there: it prints a diagnostic and the app keeps going, so you are told about undefined behaviour without losing your session.

**LeakSanitizer is Linux-only — your local run cannot detect leaks.** This is worth stating plainly because the natural inference from "ASan is on locally" is wrong, and it has already been drawn:

```
$ ASAN_OPTIONS=detect_leaks=1 ./tests/tst_mcptools_write
==11353==AddressSanitizer: detect_leaks is not supported on this platform.
```

The runtime refuses the option; there is no flag that turns it on. On macOS, ASan covers use-after-free, heap-buffer-overflow, stack-use-after-return and double-free — a leak is invisible to it. So "84/84 passed under ASan" on a Mac means *no memory errors*, never *no leaks*.

The nightly Linux ASan job (`ASAN_OPTIONS=detect_leaks=1`) is the only place leaks are caught. Its first run found two that the local suite had been passing over: `tst_decentscalewifi` (131,068 bytes / 1,372 allocations) and `tst_mcptools_write` (6,540 bytes / 70 allocations), byte-identical across all three retry attempts.

To chase a leak on macOS, use the platform tools instead: `leaks <pid>`, or `MallocStackLogging=1` for allocation stacks.

A **Release** build carries no instrumentation unless you ask. `-DENABLE_UBSAN=ON` gives the **halting** mode CI uses, where a finding aborts. `-DENABLE_ASAN=ON` does the same for memory errors, and the two combine. UBSan is not supported with MSVC (configure fails fast).

**What the instrumented build turns on beyond plain UBSan**, and why each one is there:

| Setting | Catches |
|---|---|
| `-fno-sanitize-recover=all` | Makes a finding *abort*. UBSan's default is report-and-continue, so without this a run prints undefined behaviour and still exits 0. Baked into the build so it does not depend on `UBSAN_OPTIONS` being set. |
| `local-bounds`, `float-divide-by-zero` | Array overrun on locals; division by zero in flow/pressure/ratio maths (defined as inf by IEEE 754, but a bug every time here). |
| `_LIBCPP_HARDENING_MODE` / `_GLIBCXX_ASSERTIONS` | Out-of-bounds `vector`/`QVector` `operator[]` **inside** the allocation — invisible to *both* sanitizers (ASan: memory is validly owned; UBSan: no language-level UB), and it silently returns garbage. |
| `QT_FORCE_ASSERTS` | Keeps `Q_ASSERT` live. The sanitizer job is a Release-type build, where assertions would otherwise compile to nothing in the very run built to check invariants. |

The `integer` group (unsigned overflow, implicit conversions) is deliberately **not** enabled: that is legal C++ which CRC and hashing code wraps on purpose, so it would report intent as a defect.

Two exclusions are forced rather than chosen, and both were found by a Linux/GCC build failing on a tree that was clean on macOS:

- **`vptr` is off** (`-fno-sanitize=vptr`). It instruments downcasts through a polymorphic hierarchy and needs the target's typeinfo at link time; `decentscalewifi.cpp` downcasts `QObjectPrivate*` to `QWebSocketPrivate*`, and Qt does not export typeinfo for private classes on Linux. The cost: bad-downcast detection is off everywhere, including for that downcast, whose validity rests on a convention the code's own comment says to re-verify on each Qt upgrade.
- **Optional checks are probed, not assumed.** `local-bounds` is clang-only and GCC rejects the entire compile. `CMakeLists.txt` uses `check_cxx_compiler_flag` so unsupported checks are dropped per-toolchain — which also covers iOS, Android and Windows without predicting their compilers. If you add a check, add it to that probe list rather than to the flag string.

**ThreadSanitizer does not work here.** Qt ships uninstrumented (`nm -u` on QtCore shows zero `__tsan` symbols), so TSan cannot see the mutexes inside Qt's event queue that establish happens-before between a `Qt::QueuedConnection` poster and its worker. A trial run produced 10,194 reports, 94% of them through Qt's queued-connection machinery — every *correct* cross-thread handoff reads as a race. Making it usable means rebuilding Qt with `-fsanitize=thread`. Don't wire it to CI without that.

**The `sanitizer_canary` test.** Registered only under `ENABLE_UBSAN`, it commits deliberate signed overflow and fails unless the sanitizer both kills the process *and* prints a diagnostic. It exists because a silently-unapplied sanitizer produces exactly the same green suite as a clean codebase — so without it, a gate that has rotted into a no-op is invisible by construction. If it fails, the instrumentation is not reaching the compile/link line; fix that before trusting any other green result.

### CI

**There is no pull-request CI gate** — the suite is run locally before opening a PR, and that is the gate. `nightly-sanitizers.yml` runs the suite nightly on `main` under UBSan and ASan as two independent Linux builds. On **tag push**, `linux-release.yml` builds and runs all tests (uninstrumented) before packaging the AppImage. Other platform workflows do not run the suite.

See `docs/CLAUDE_MD/CI_CD.md` for why there is no PR gate: the detectors found no pre-existing defects on their first run across eight months of code, so they were moved off the critical path of every push.

## Architecture

### Test Structure

Each test file is a standalone executable following Qt Test conventions:

```
tests/
├── CMakeLists.txt              # Test build configuration
├── mocks/
│   └── MockScaleDevice.h       # Concrete ScaleDevice for testing
├── tst_sav.cpp                 # SAV (stop-at-volume) tests
└── tst_saw.cpp                 # SAW (stop-at-weight) tests
```

### Testability Pattern: `friend class` with `DECENZA_TESTING`

Production classes use `#ifdef DECENZA_TESTING` to grant test classes direct access to private members:

```cpp
// In production header (e.g., machinestate.h)
class MachineState : public QObject {
    // ... public/private interface ...

#ifdef DECENZA_TESTING
    friend class tst_SAV;
#endif
};
```

Test executables compile with `-DDECENZA_TESTING` (set in `tests/CMakeLists.txt`). The production build never defines this symbol, so the friend declarations are invisible.

**Why this pattern:**
- No refactoring of production code needed
- Tests can set private state directly (e.g., `state.m_pourVolume = 40.0`)
- Standard Qt project pattern
- Zero runtime overhead in production

### Mock Strategy

| Class | Mock Approach | Why |
|-------|---------------|-----|
| `ScaleDevice` | `MockScaleDevice` inherits abstract base | Already has virtual methods — clean inheritance |
| `Settings` | Real `Settings` with public setters | All needed methods have public setters already |
| `DE1Device` | `friend class` access to private `m_state`/`m_subState` | Not abstract, but tests need to control state |
| `WeightProcessor` | Tested directly — injectable `setWallClock()` for fake-clock tests | Clean public interface; fake clock avoids 77s of `QTest::qWait()` |

### Signal Verification

Use `QSignalSpy` to verify signal emissions:

```cpp
QSignalSpy spy(&machineState, &MachineState::targetVolumeReached);
// ... trigger the condition ...
QCOMPARE(spy.count(), 1);
```

### Data-Driven Tests

Use Qt Test's data-driven pattern to test across profile types:

```cpp
void myTest_data() {
    QTest::addColumn<QString>("profileType");
    QTest::newRow("basic pressure") << "settings_2a";
    QTest::newRow("basic flow")     << "settings_2b";
    QTest::newRow("advanced")       << "settings_2c";
    QTest::newRow("advanced+lim")   << "settings_2c2";
}

void myTest() {
    QFETCH(QString, profileType);
    // Test logic using profileType
}
```

## Test Coverage: SAV (Stop-at-Volume)

Located in `tests/tst_sav.cpp`. Tests `MachineState::checkStopAtVolume()` and `checkStopAtVolumeHotWater()`.

### Espresso SAV Matrix

| Condition | 2a | 2b | 2c | 2c2 |
|-----------|----|----|----|----|
| Fires at `pourVolume >= target` (no scale) | Yes | Yes | Yes | Yes |
| Disabled when `targetVolume == 0` | Yes | Yes | Yes | Yes |
| Blocked before tare completes | Yes | Yes | Yes | Yes |
| Fires only once | Yes | Yes | Yes | Yes |
| No lag compensation (raw comparison) | Yes | Yes | Yes | Yes |
| Skipped when scale configured | **Yes** | **Yes** | No | No |
| Active when no scale configured | Yes | Yes | Yes | Yes |
| Skipped by `ignoreVolumeWithScale` + scale | Yes | Yes | Yes | Yes |
| Active when `ignoreVolumeWithScale` + no scale | Yes | Yes | Yes | Yes |

### Hot Water SAV

- 250 ml safety net when scale configured
- `waterVolume` setting target when no scale
- Tare guard required
- Fires only once

### Volume Bucketing

- Preinfusion substate → preinfusion volume
- Pouring substate → pour volume
- Phase-based (DE1 substate), matching de1app

## Test Coverage: SAW (Stop-at-Weight)

Located in `tests/tst_saw.cpp`. Tests `WeightProcessor::processWeight()`.

### Core SAW Behavior

| Condition | Expected |
|-----------|----------|
| Ignores first 5 seconds of extraction | No trigger before 5s |
| Waits for preinfusion frame guard | No trigger while frame < preinfuseFrameCount |
| Requires flow rate >= 0.5 ml/s | No trigger with constant weight |
| Disabled when `targetWeight == 0` | No trigger |
| Fires `stopNow` and `sawTriggered` signals | Verified via QSignalSpy |

### Per-Frame Weight Exit

| Condition | Expected |
|-----------|----------|
| Fires `skipFrame` when weight >= exitWeight | Signal emitted with frame number |
| Fires only once per frame | No duplicate signals |

### Preinfusion Frame Guard by Profile Type

| preinfuseFrameCount | Behavior |
|---------------------|----------|
| 0 | SAW can fire from frame 0 onward (after 5s) |
| 2 | SAW blocked until frame 2 |
| 3 | SAW blocked until frame 3 |

## Known Coverage Gaps

Areas where bugs have shipped undetected due to missing test coverage:

### QML binding correctness (highest priority)

No tests verify that QML files resolve property names and method calls to the expected C++ objects. During the ProfileManager extraction (PR #562), three QML bugs shipped past the full test suite:
- `MainController.previousProfileName()` — method removed from MainController, QML silently returned `undefined`
- `MainController.currentProfile` — never was a QML property (should be `currentProfileName`), always `undefined`
- `typeof MainController` guards checking wrong object after data source moved to ProfileManager

A QML binding smoke test that instantiates pages and verifies key property bindings resolve to non-undefined values would catch this class of bug.

### MCP resource responses

`tst_mcptools_profiles` and `tst_mcptools_write` test MCP *tools* but not MCP *resources* (`decenza://profiles/active`, `decenza://profiles/list`, etc.). A bug where the `"filename"` field returned a display title instead of a filename was undetectable.

### MainController recipe activation flows

No test instantiates `MainController` (it needs devices, storage, and settings wired together), so the recipe-activation behaviors that live there are manual-verification-only (fix-recipe-grind-integrity): the same-id re-activation short-circuit (re-pushing the in-memory cache with a deliberately EMPTY bag map — an applied bag map there would write stale/blank bean identity through to the bag row), bean-less activation clearing the active bag, the tea grind-stamp skip (`DrinkTypes::hasGrind`, pinned in `tst_drinktypes`), and the profile-title gate that keeps a profile switch from stamping zeroed yield/temp into the outgoing active recipe. Changes to `activateRecipe`/`applyActivatedRecipe`/the stamp watchers need manual verification on a device.

The same limitation covers **which values reach the Home Screen widget's last-shot tile** (#1658). `MainController::shotPersisted` is emitted from a lambda bound to `ShotHistoryStorage::shotSaved`, inside `onShotEnded()`, past the aborted-shot gate — reaching it needs a ready `ShotHistoryStorage`, a `ShotTimingController` with a non-zero `extractionDuration()`, a `ProfileManager` and `settings->dye()`. The consuming side is a lambda inside `main()`, untestable by construction. `tst_settling::stopTimeStaysSentinelWithoutSaw` characterizes the `ShotDataModel::stopTime() == -1` trap that caused the bug, but it passes on either side of the fix — it documents the premise, not the wiring. A source-text assertion over `main.cpp` was considered and rejected as rot-prone. The live backstop is `MachineStatusSnapshot::setLastShot`'s throttled `qWarning`, which puts any reintroduced sentinel in the debug log the issue template collects.

### ShotHistoryStorage async methods

`tst_dbmigration` exercises schema migration and some query paths, but does not cover all async methods. `requestShotsFiltered()` had a missing destroyed-flag guard (use-after-free risk) that was only found by code review, not tests.

### Flow calibration delegation

`applyFlowCalibration()` existed as duplicate implementations in both MainController and ProfileManager. Since both were identical, no test could detect the divergence risk — but if either were updated independently, behavior would silently differ.

## MCP Integration Tests

`scripts/test_mcp.sh` runs ~200 tests against a live MCP server, covering protocol compliance, tool discovery, all read/write tools, resources, rate limiting, session management, settings parity, and input validation.

```bash
# App must be running with MCP enabled. Non-interactive mode skips the access-level gating test.
SKIP_INTERACTIVE=1 bash scripts/test_mcp.sh localhost:8888
```

Run this after any changes to `src/mcp/`, `src/controllers/profilemanager.cpp`, or `src/network/shotserver*.cpp`.

## What actually finds bugs here

Written after the recipe-editor parity effort, because the yield was measured
rather than assumed and the answer was not what was expected going in.

That effort produced ~440 committed fixture files and eleven findings, then a
code review found **six more defects while the suite sat at 101/101 green**.
Attributing every bug to what caught it:

| Found by | Bugs |
|---|---|
| **Differential oracle** — running de1app's real Tcl and diffing | the edit matrix's findings, WIRE-1 |
| **Transcribing the upstream source** and asserting against real fixtures | AF-1…AF-6, DF-1…DF-5 |
| **Pulling one actual shot** | the DE1Simulator zero-length-frame bug |
| **Reading code, history, comments and fixtures** (review) | every defect in the second round |
| **A 120-profile randomised corpus** | none |
| **Extending a byte-parity corpus from 8 profiles to 89** | none |

Three lessons, in descending order of how much they cost to learn:

**1. A test you invent asserts what you already believe. A differential test
asserts what the other system does.** Everything of value here came from
executing de1app's own procs — `prep`, `update_*`, `de1_packed_shot` — and
diffing. Hand-written expectations found nothing and were wrong seven times.

**2. Scale is not coverage.** The byte-diff *method* found WIRE-1 with eight
profiles. Going to 89, and generating 120 more, found nothing further. Before
adding a corpus, ask what shape of defect it catches that a handful of cases
does not — and if the answer is "more of the same", it is insurance, not
detection. Insurance is legitimate; just argue it on risk and say so.

**3. The failure mode to fear is a test that cannot fail.** Seven appeared in one
effort. Every one passed because the FIXTURE could not distinguish:

- asserting `pourFlow > 0` when the struct default is 2.0
- a two-frame fixture for an editor that indexes frames 0/1/2
- asserting a value that is also the generator's own hardcoded literal
- a no-op-save test that passed only because a fabricated recipe block made the
  save short-circuit before touching the frames
- stock fixtures whose values already equal the constants under test, so
  `before == after` either way
- asserting pressure fields when the broken field was temperature
- calling a one-shot migration whose "already ran" flag was set, so the call did
  nothing

**So: verify the test fails.** Revert the fix, watch it go red, restore. It takes
two minutes, it is the only evidence that a green test means anything, and it
caught the seventh case above *after* the first six had already taught the
lesson. If reverting the fix leaves the test green, the test is decoration.

Corollary: when a test needs a fixture, prefer a real artefact — a stock `.tcl`,
a recorded shot — over one you construct. A constructed fixture tends to be built
from the same assumptions as the code, which is precisely what stops it
discriminating.

## Adding New Tests

**A test costs BUILD time, not run time. Budget accordingly.** The whole suite runs
in ~31 s and an individual test is 50-80 ms — that is noise. What is not noise is that
`tests/` is **40% of a clean build** (1956 s of 4846 s cpu, measured from `.ninja_log`
on macOS Debug), and that the suite doubled in July 2026 alone — 60 files to 107.

**Where that 1956 s actually goes.** This matters because the intuitive answer is wrong
and this section used to give it:

| Slice | Cost | Share | Scales with |
|---|---|---|---|
| Production sources recompiled inside test targets | 688 s | 35.2% | the source list you pass `add_decenza_test` |
| Test source TUs | 625 s | 31.9% | **how much test code exists** |
| moc / autogen | 519 s | 26.5% | both |
| qrc (Qt resources) | 66 s | 3.4% | resource-linking targets |
| Link + timestamps | 58 s | **3.0%** | target count |

Two consequences, both the opposite of what "a compile *and a link*" suggests:

- **Link is 3.0%**, mean 0.53 s per target. Target count is the smallest lever there is.
- **Relocating test code saves nothing on the test code itself.** Moving 208 test
  functions from a new file into `tst_profilemanager.cpp` pays the same 17.6 s compile
  under a different name. Consolidating targets saves link cost and lets targets share a
  source list — real, but small. The only thing proportional to the problem is writing
  less test code.

So, in order:

1. **Add to an EXISTING target whose fixtures already fit.** This is the default and
   it is nearly free — one TU recompiles, and only when you touch it. Look for a
   binary that already links what you need and already has the setup: `tst_dbmigration`
   (schema/migration chain), `tst_dialing_blocks` (DB-backed reads, and it copies a
   prebuilt schema template per test instead of re-running migrations), `tst_coffeebags`,
   `tst_equipment`, `tst_recipestorage`.
2. **Only create a new target when no existing one fits** — a genuinely new subsystem,
   or a link footprint the existing binaries do not have. Creating one to get a tidier
   filename is not a reason; it buys a name and costs a compile+link on every build.
   If you do: `tests/tst_yourtest.cpp` with `QTEST_GUILESS_MAIN(tst_YourTest)` and
   `#include "tst_yourtest.moc"`, then `add_decenza_test(...)` in `tests/CMakeLists.txt`.
   **Watch the source list more than the target.** A new target with three sources is
   cheap; adding one more production `.cpp` to an existing target's list to reach the
   code you want is what actually costs — see "Shared sources go in a narrow library".
3. If accessing private members, add `friend class tst_YourTest;` behind
   `#ifdef DECENZA_TESTING` in the production header
4. Run `mcp__qtcreator__run_tests` to verify (`scope: "named"`, `names: ["tst_YourTest"]`;
   if a freshly added target isn't in the Autotest model yet, just retry — it re-parses)

### A test has to catch something the suite does not

Say what defect shape this test detects that no existing test detects. If you cannot
name one, add the assertion to the test that already covers that shape instead of
writing a new one.

"More cases of the same shape" is **insurance, not detection**. It may well be worth
adding — but argue it on risk and call it insurance, rather than presenting it as
closing a coverage gap. The byte-parity corpus above is the worked example: the method
found WIRE-1 with eight profiles, and going to 89 and then 120 more found nothing
further.

**One invariant, one place.** If an invariant is already asserted somewhere, extend that
assertion rather than adding a parallel one. Two tests asserting the same invariant over
different fixtures are one test parameterised over both fixtures — the second copy is
free to drift, and nothing fails when it does.

This is the same reasoning as the "centralize anything produced at more than one site"
rule in the root `CLAUDE.md`, applied to assertions.

### A test has to be able to fail

Before you keep a test, **break the code it covers and watch it go red.** This is not
ceremony; it is the only thing that distinguishes a test from a comment that compiles.
Three shapes that pass forever and protect nothing, all shipped here:

- **Tautological.** `grindStepAgreesWithDialingContext` asserted that the widget and the
  AI payload return the same number — after a refactor made both call the same function.
  `f(x) == f(x)`. Deleted.
- **Asserting the universal failure value.** A function whose error path, not-ready path
  and no-data path all return `0` cannot be tested by asserting `0`. Pair it with a case
  in the same database that returns a real value, so the assertion proves the query ran.
- **The predicate is the fix.** A poll loop whose condition calls the getter re-issues the
  work the bug was about not re-issuing, so it passes on both branches. Read once after
  quiescing instead.

When a test would only pass, say so and don't write it. Deleting a test that cannot fail
is a strict improvement — it removes build cost and a false sense of coverage at once.

### Shared sources go in a NARROW library, never in `decenza_testlib`

When two or more test targets need the same production source, compile it once into an
intermediate library linked by **exactly those targets**. Do not reach for
`decenza_testlib` to deduplicate — it is linked by every test target, and that trade is
worse than the duplication it fixes.

Measured — `touch src/controllers/profilemanager.cpp`, rebuild, cpu from the `.ninja_log`
diff, `tests/`-attributable only. Nine consumers; 0.53 s mean link across all 106 targets that link it:

| | Today (9 duplicate compiles) | Into `decenza_testlib` | Into a narrow library |
|---|---|---|---|
| Clean build | 72.6 s wasted | 0 s | 0 s |
| Touch `profilemanager.cpp` | **77.0 s** | ~67 s | **12.2 s** |

Widening the shared library trades a compile fan-out of 9 for a **link fan-out of 106**
and comes out marginally worse on incremental builds, which is the case a developer
actually waits on. A narrow library wins on both.

So: an intermediate library's consumer set is never wider than the set of targets that
need it. Before adding a source to any target's list, check whether another target
already compiles it — and check whether `decenza_testlib` already carries it, which is
how `tst_visualizershotparse` ended up compiling `shotanalysis.cpp` a second time for
no reason.

This rule is **enforced**, not advisory. `scripts/check_test_source_duplication.py` reads
`tests/CMakeLists.txt` as text and fails on two things: a production source listed by two
or more test targets, and a source listed by a target that already links a library
compiling it. It runs per-PR in `text-invariants.yml` (build-free, so it fits that job's
rule), and it is why the tree can have **zero** duplicates rather than a slowly-refilling
exception list.

Two consequences for anyone editing `tests/CMakeLists.txt`:

- **Keep every `add_library()` and its source list literal.** The check reads this file as
  text, so a library name assembled in a `foreach()` is invisible to it. A generated
  version was written during the cleanup and reverted for exactly this reason.
- **Do not hoist a moc'd header into a library unless every user of it links that
  library.** `${MCP_MOC_HEADERS}` was tried in `decenza_mcpserverlib` and produced 24
  duplicate symbols, because targets that do not link that library moc the same headers
  themselves. A moc'd header belongs to exactly one compilation unit per link.
- **A generated `qrc` goes in an OBJECT library, never STATIC.** Nothing references a
  symbol in a generated resource `.cpp` — it is reached only through its static
  initialiser — so a linker has no reason to pull it out of an archive and the resource
  silently fails to register. `decenza_testresources` is `OBJECT` for that reason.

### Prefer fixtures that amortise

`tst_dialing_blocks` builds the schema **once** in `initTestCase()` and copies the file
per test (a few ms) rather than running `createTables()` + the migration chain each time
(~300 ms × 37 call sites). If you are adding DB-backed tests, use a binary with that
shape rather than paying the chain per test.

## Handling Warnings

**Every `qWarning()` emitted during a test run must either be fixed at the source or explicitly marked as expected.** A noisy test suite hides real regressions: once you get used to seeing 50 WARN lines in green output, the 51st one — which is actually a new bug — blends in. Treat warnings as failures-in-waiting.

**This is enforced, not just convention.** Every test class calls `QTest::failOnWarning()` in its `init()`, so an *unexpected* `qWarning`/`qCritical` during a test function **fails that test** — even under `ctest -j` or Qt Creator's CTest runner, which otherwise hide passing-test stderr. Warnings marked expected via `QTest::ignoreMessage()` are consumed before the check and do **not** fail. **New test classes must add `void init() { QTest::failOnWarning(); }`** (or prepend the call to an existing `init()`); without it the class silently opts out of the guard. Do **not** rely on `QT_FATAL_WARNINGS` — it aborts on `ignoreMessage()`-expected warnings too.

There are three legitimate outcomes for any warning fired during a test:

### 1. It's the behaviour under test — mark it expected per-test

Use `QTest::ignoreMessage()` at the top of the test function, **before** the action that triggers the warning. The test fails if the warning doesn't fire or if any other warning fires.

```cpp
void uploadFailsWhenFrameAckIsDropped() {
    // ... setup ...

    // The failure path emits a qWarning describing the mismatch — that's the
    // behaviour we want to verify is still happening, so mark it as expected
    // rather than letting it show up as noise.
    QTest::ignoreMessage(QtWarningMsg,
        QRegularExpression("profile upload FAILED — frame sequence mismatch"));

    device.uploadProfile(makeSimpleProfile());
    // ... rest of test ...
}
```

Prefer `QRegularExpression` over exact-match strings so the test isn't brittle against formatting tweaks.

### 2. It's a test-harness artifact across many tests — filter at fixture scope

Use the `ScopedWarningFilter` RAII guard declared in `tests/mocks/McpTestFixture.h` for warnings that fire during fixture construction/destruction (e.g. `~DE1Device` surfacing an in-flight upload as failed when `MockTransport` never ACKed the writes). Declare the filter **before** the member whose destructor emits the warning so the filter outlives it:

```cpp
struct McpTestFixture {
    // ... earlier members ...
    // Declared before `device` so the filter outlives ~DE1Device.
    ScopedWarningFilter uploadFilter{"profile upload FAILED — (BLE disconnect during upload|superseded by a new upload|command queue cleared during upload)"};
    DE1Device device;
    // ... later members ...
};
```

Keep the regex **narrow** — list only the specific reasons that are expected. If the pattern is too broad, it will swallow genuinely unexpected warnings too. Add a comment explaining why each suppressed variant is expected.

### 3. It's a real bug in the code or test — fix it

Examples of "fix it" outcomes:
- The code emits a warning for a state the test didn't intend to exercise → fix the test setup
- The warning reveals a real race/order-of-operations bug in production code → fix the production code
- The warning message is wrong or misleading → fix the log text

When in doubt, fix it rather than suppress it. The goal of a passing test run is silence on stderr.

### What not to do

- **Do not globally suppress warnings** (e.g. a `qInstallMessageHandler` that drops everything at `QtWarningMsg`). That defeats the purpose.
- **Do not suppress by substring-matching `"failed"` or `"error"`** — that's too broad and will hide genuine regressions.
- **Do not amend an existing `ScopedWarningFilter` regex to make a new warning go away without adding a comment explaining the scenario.**

## Recipe-editor parity gate — `tst_recipeeditorparity`

Checks Decenza's D-Flow and A-Flow implementations against the **upstream de1app plugins that
define them** (`Damian-AU/D_Flow_Espresso_Profile`, `Jan3kJ/A_Flow`), across frame generation,
parameter extraction from frames, round-trip stability, both A-Flow frame layouts, and editor
coverage.

**Oracle discipline — the rule that makes this suite worth anything.** Every expected value traces
to a plugin proc or to a profile the plugin itself ships. **Nothing** is derived from Decenza's own
code or from its built-in JSONs; those are the subject, not the reference. Where the two disagree,
the plugin is right by definition. The transcribed rules with line citations live in
`openspec/changes/verify-recipe-editor-parity/reference.md`.

The suite cannot run Tcl, so rules are transcribed — which is its weak point, since a transcription
error yields a test that passes against the wrong oracle. It is checked two ways: against the
plugin source, and against the plugin's own stock profiles, which are those rules already executed.
A rule that disagrees with the shipped profiles is suspect regardless of how it reads.

**Fixtures — where they come from matters.**

| dir | contents | role |
|---|---|---|
| `tests/data/de1app_profiles/A-Flow____*.tcl` | the plugin's five stock profiles, 9 frames | **the oracle** |
| `tests/data/dflow_plugin_profiles/` | D-Flow's three, extracted from `plugin.tcl` | the oracle |
| `tests/data/aflow_legacy_profiles/` | one 6-frame profile from de1app's stale snapshot | **legacy case only** |

de1app's `de1plus/profiles/` carries four A-Flow profiles at **6** frames and is missing
`default-light`; the plugin ships all five at **9** (de1app issue #350). Verifying against the
stale copy would produce a suite that passes against the wrong source, so the suite asserts a
9-frame count at load. The 6-frame layout is still covered — as the *legacy* branch of
`set_profile_index`, never as the reference.

D-Flow ships no `.tcl` at all; regenerate its fixtures with
`python3 tools/extract_dflow_profiles.py` after any plugin bump, and re-check the pinned commits
recorded in the suite header — a transcribed rule goes stale silently.

**Known findings are `QEXPECT_FAIL`, never relaxed assertions.** Confirmed defects stay expressed
as failing checks carrying their finding id (`DF-1`, `AF-6`, …) so the gate records reality rather
than the behaviour of the day. If you fix one, delete its `QEXPECT_FAIL` — do not weaken the check,
and **do not delete the assertion with it**: `everyFindingIdIsStillAccountedFor` requires every id
to remain referenced, because removing the check retires a finding by making the gate stop looking.
All thirteen are now repaired (DF-3 excepted — see below); their dispositions are in
`openspec/changes/verify-recipe-editor-parity/findings.md`.

DF-3 is the one allowed divergence, and it is not a defect: `update_D-Flow` genuinely derives
`filling(exit_pressure_over)` from the soak pressure, and de1app rewrites it on the user's first
edit too. It is allowed **by name**, with `D-Flow / La Pavoni` — whose authored value already equals
the derived one — asserted as an exact fixed point, so the allowance cannot mask a drifting rule.

### The three gates, and what each one is for

| Gate | Question | Oracle | Regenerate |
|---|---|---|---|
| **Edit matrix** — `editMatrixMatchesDe1app` (99 cases) | every plugin parameter × every stock profile, one edit, through `ProfileManager`'s `Q_INVOKABLE`s | the plugins' own `prep` + `update_*`, extracted verbatim and evaluated | `python3 tools/gen_edit_matrix.py <de1plus-dir>` |
| **Compound edit** — `compoundEditMatchesDe1app` (8) | two successive saves, so the second `prep` re-derives from the frames the first wrote | same, one `prep`→`update` cycle per pair | same script |
| **Byte parity** — `everyDe1appProfilePacksIdentically`, `everyDe1appProfileSurvivesASaveCycle` (89 each) | do all de1app stock profiles reach the machine as identical bytes, on load and after a save cycle | de1app's real `de1_packed_shot` | `python3 tools/gen_de1app_pack_corpus.py <de1plus-dir>` |

The byte gates are the **regression guard for everything outside the two recipe editors.** About 80
of those 89 profiles are advanced, pressure or flow profiles that no recipe-editor test touches, yet
they pass through the same load and save code — so a change to `Profile::toJsonObject()` or the
frame encoders shows up there and nowhere else. The save-cycle variant exists because the plain one
loads and packs without ever writing, which would miss a serialization regression entirely.

The pack oracle runs de1app's **real load path** for simple profiles: a `settings_2a`/`2b` profile's
stored `advanced_shot` is a stale by-product, and de1app rebuilds the frames from the scalars via
`pressure_to_advanced_list` / `flow_to_advanced_list` before packing. `profile_vars` and
`machine.tcl`'s default `::settings` block are extracted verbatim rather than transcribed, because a
copied field list drifts on a de1app bump and the drift is invisible.

**A golden is never hand-adjusted to match Decenza.** If one looks wrong, re-read the oracle; if the
oracle is right, Decenza changes. Regenerating after a plugin bump must leave every data row
unchanged unless the plugin itself changed.

These gates are the worked example behind [What actually finds bugs
here](#what-actually-finds-bugs-here) — read that before adding a corpus of your own. The short
version: the differential oracle earned its keep; the fixture volume did not.

## Shot Analysis Regression Tool (shot_eval)

`tools/shot_eval/` is a CLI harness for exercising the real `ShotAnalysis` heuristics (channeling, grind direction, pour truncation) against a corpus of shot data. Links the production `src/ai/shotanalysis.cpp` and `src/ai/conductance.cpp` directly — changes to the live detector automatically flow through. Use it whenever you touch a detection heuristic to see how verdicts shift across a known set of shots.

### Building

Built as part of the desktop-only `tools/` block in the root `CMakeLists.txt`. Target name: `shot_eval`. No special flags needed — it's compiled alongside the main app.

```bash
cmake --build <build-dir> --target shot_eval
```

### Running

```bash
# Single shot
./shot_eval shot.json

# Directory of shots
./shot_eval ~/shot_corpus/

# Glob / multiple paths
./shot_eval visualizer_public/*.json

# Machine-readable output for diffing
./shot_eval --json ~/shot_corpus/ > results.json
```

Accepts two JSON shapes:

| Format | Where it comes from | Shape |
|---|---|---|
| **Upload / local export** | `~/Library/Application Support/DecentEspresso/Decenza/profiles/history/*.json` (when `Settings::exportShotsToFile` is on); same payload Decenza uploads to visualizer.coffee | Nested `pressure: { pressure, goal }`, `flow: { flow, goal }`, top-level `elapsed[]`, `profile.steps[]` |
| **Visualizer download** | `https://visualizer.coffee/api/shots/<uuid>/download` after visualizer transforms the uploaded payload | Flat `data: { espresso_pressure, espresso_flow, espresso_pressure_goal, ... }` + `timeframe[]` |

The tool autodetects based on the root-level keys. Both include enough goal data for mode-aware phase inference.

### Workflow for validating algorithm changes

1. Maintain a local `~/shot_corpus/` with representative shots from your own history and a few public visualizer shots covering profile families you care about (lever, flat-pressure, flow-mode, blooming, turbo, etc.).
2. Before a change: `./shot_eval ~/shot_corpus/*.json > before.txt`.
3. Apply the change, rebuild shot_eval.
4. After the change: `./shot_eval ~/shot_corpus/*.json > after.txt`.
5. `diff before.txt after.txt` — any verdict flips should be intentional; surprises indicate a regression.

### Regression corpus — `tests/data/shots/`

A 12-shot golden set lives in the repo with a `manifest.json` listing expected verdicts per shot. Each shot targets a specific detector path — lever-ramp false-positive suppression, flat-pressure happy path, end-skip guard, grind-direction firing, catastrophic puck failure, Blooming expected-transient, etc.

Runs automatically as a CTest entry:

```bash
ctest -R shot_corpus_regression
```

Which is equivalent to:

```bash
./shot_eval --validate tests/data/shots/manifest.json
```

The command reads the manifest, runs each shot through the full production detector path (including `shouldSkipChannelingCheck`, beverage-type short-circuits, and mode-aware masking), compares verdicts against the expected values in the manifest, and returns non-zero on any mismatch. Add new shots to the corpus whenever you fix a detector bug so future refactors can't silently reintroduce it.

### Adding shots to the corpus

1. **Pick something that exercises a path** not already covered — e.g. a new false-positive you fixed, a new true-positive class, a profile family the existing corpus doesn't hit.
2. **Strip personal metadata** if copying from your local export directory. Redact `meta.bean.brand`, `meta.bean.type`, `meta.shot.barista`, `meta.shot.notes`, `meta.shot.uuid`. Public visualizer shots can be copied as-is.
3. **Run the shot through `shot_eval`** to capture its current verdicts.
4. **Add an entry to `manifest.json`** with a `description` (why this shot matters) and an `expect` block with only the invariants you want to enforce — leave fields out of `expect` if you don't care about asserting them.

Manifest entry format:
```json
{
  "file": "my_new_shot.json",
  "description": "Short explanation of why this shot matters and what path it exercises",
  "expect": {
    "channeling": "None",          // or "Transient" / "Sustained"
    "grindIssue": false,           // optional
    "pourTruncated": false         // optional
  }
}
```

### Conductance math and the `src/ai/conductance.h` boundary

Both `ShotDataModel` (live per-sample) and `shot_eval` (batch offline) share `src/ai/conductance.{h,cpp}` for the conductance + dC/dt formulas. A change there automatically flows to both — don't duplicate the math.

## Conventions

- Test class names: `tst_FeatureName` (Qt convention)
- One test file per logical feature area
- Use `_data()` suffix for data-driven test data functions
- Use `QSignalSpy` for signal verification, never raw signal counters
- Test executables compile their own source files — no shared test library
- Keep mock classes minimal — implement only what tests need
