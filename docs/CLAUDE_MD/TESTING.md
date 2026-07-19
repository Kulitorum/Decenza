# Unit Testing

## Framework

Qt Test (QTest) — ships with Qt, no external dependencies, integrates with CMake's `ctest`.

## Building and Running

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

Always run with `-j` — the full suite drops from ~140 s to ~30 s. It is parallel-safe: every settings class isolates its `QSettings` under `DECENZA_TESTING` via `Settings::testQSettingsPath()` (a PID-scoped `IniFormat` temp file, see `settings.h`), so no two test processes share an on-disk store and the suite never mutates the developer's real `("DecentEspresso", "DE1Qt")` preferences.

When seeding raw pre-construction settings state in a test, write through the **same** isolated store — `QSettings(Settings::testQSettingsPath(), QSettings::IniFormat)` — never `QSettings("DecentEspresso", "DE1Qt")` directly, or the seed lands in a scope nothing reads (and pollutes the real store).

`--repeat until-pass:3` covers two clock-cadence tests — `tst_settling` (feeds samples at real `qWait` intervals and asserts plateau detection over time windows) and `tst_decentscalewifi` — that can occasionally miss a timing window under heavy CPU contention when many tests run at once. The retry re-runs only a failed test; a genuine regression fails all three attempts. In Qt Creator's CTest settings you can add the same flag, or just re-run a lone flaky result.

### Running under UBSan (what pre-merge CI does)

The pre-merge CI job runs the suite instrumented with UndefinedBehaviorSanitizer. To reproduce locally:

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

`ENABLE_UBSAN` is off by default and never auto-enabled, so routine builds are uninstrumented. It is not supported with MSVC (configure fails fast). ASan (`-DENABLE_ASAN=ON`, auto-on for non-Apple Debug builds) works the same way for memory errors; the two can be combined (`-DENABLE_ASAN=ON -DENABLE_UBSAN=ON`).

**What the instrumented build turns on beyond plain UBSan**, and why each one is there:

| Setting | Catches |
|---|---|
| `-fno-sanitize-recover=all` | Makes a finding *abort*. UBSan's default is report-and-continue, so without this a run prints undefined behaviour and still exits 0. Baked into the build so it does not depend on `UBSAN_OPTIONS` being set. |
| `local-bounds`, `float-divide-by-zero` | Array overrun on locals; division by zero in flow/pressure/ratio maths (defined as inf by IEEE 754, but a bug every time here). |
| `_LIBCPP_HARDENING_MODE` / `_GLIBCXX_ASSERTIONS` | Out-of-bounds `vector`/`QVector` `operator[]` **inside** the allocation — invisible to *both* sanitizers (ASan: memory is validly owned; UBSan: no language-level UB), and it silently returns garbage. |
| `QT_FORCE_ASSERTS` | Keeps `Q_ASSERT` live. The sanitizer job is a Release-type build, where assertions would otherwise compile to nothing in the very run built to check invariants. |

The `integer` group (unsigned overflow, implicit conversions) is deliberately **not** enabled: that is legal C++ which CRC and hashing code wraps on purpose, so it would report intent as a defect.

Two exclusions are forced rather than chosen, and both were found by the pre-merge job failing on Linux against a tree that was clean on macOS:

- **`vptr` is off** (`-fno-sanitize=vptr`). It instruments downcasts through a polymorphic hierarchy and needs the target's typeinfo at link time; `decentscalewifi.cpp` downcasts `QObjectPrivate*` to `QWebSocketPrivate*`, and Qt does not export typeinfo for private classes on Linux. The cost: bad-downcast detection is off everywhere, including for that downcast, whose validity rests on a convention the code's own comment says to re-verify on each Qt upgrade.
- **Optional checks are probed, not assumed.** `local-bounds` is clang-only and GCC rejects the entire compile. `CMakeLists.txt` uses `check_cxx_compiler_flag` so unsupported checks are dropped per-toolchain — which also covers iOS, Android and Windows without predicting their compilers. If you add a check, add it to that probe list rather than to the flag string.

**ThreadSanitizer does not work here.** Qt ships uninstrumented (`nm -u` on QtCore shows zero `__tsan` symbols), so TSan cannot see the mutexes inside Qt's event queue that establish happens-before between a `Qt::QueuedConnection` poster and its worker. A trial run produced 10,194 reports, 94% of them through Qt's queued-connection machinery — every *correct* cross-thread handoff reads as a race. Making it usable means rebuilding Qt with `-fsanitize=thread`. Don't wire it to CI without that.

**The `sanitizer_canary` test.** Registered only under `ENABLE_UBSAN`, it commits deliberate signed overflow and fails unless the sanitizer both kills the process *and* prints a diagnostic. It exists because a silently-unapplied sanitizer produces exactly the same green suite as a clean codebase — so without it, a gate that has rotted into a no-op is invisible by construction. If it fails, the instrumentation is not reaching the compile/link line; fix that before trusting any other green result.

### CI

Every **pull request** builds Linux x64 and runs the full suite under UBSan via `pre-merge.yml` (see `docs/CLAUDE_MD/CI_CD.md` → "Pre-merge verification"). On **tag push**, the Linux release workflow (`linux-release.yml`) builds and runs all tests (uninstrumented) before packaging the AppImage. Other platform workflows (Windows, macOS, Android, iOS) do not currently run the test suite.

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

## Adding New Tests

1. Create `tests/tst_yourtest.cpp` with `QTEST_GUILESS_MAIN(tst_YourTest)` and `#include "tst_yourtest.moc"`
2. Add to `tests/CMakeLists.txt` using `add_decenza_test(tst_yourtest tst_yourtest.cpp ...source files...)`
3. If accessing private members, add `friend class tst_YourTest;` behind `#ifdef DECENZA_TESTING` in the production header
4. Run `ctest` to verify

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
