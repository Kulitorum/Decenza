# Unit Testing

## Framework

Qt Test (QTest) — ships with Qt, no external dependencies, integrates with CMake's `ctest`.

## Building and Running

Tests are **auto-enabled in Debug builds** and **in Linux CI releases**. No flag needed for normal development.

```bash
# Debug build — tests included automatically
cmake -G Ninja -DCMAKE_PREFIX_PATH=~/Qt/6.10.2/macos -DCMAKE_BUILD_TYPE=Debug ..

# Release build — tests off by default, opt-in with:
cmake -DBUILD_TESTS=ON -G Ninja -DCMAKE_PREFIX_PATH=~/Qt/6.10.2/macos ..

# Run all tests
ctest --output-on-failure

# Run a specific test
./tests/tst_sav
./tests/tst_saw
```

Override with `-DBUILD_TESTS=OFF` (Debug) or `-DBUILD_TESTS=ON` (Release) as needed.

### CI

The Linux release workflow (`linux-release.yml`) builds and runs all tests before packaging the AppImage. Tests run on every tag push (releases and pre-releases) and manual workflow dispatch.

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
| `WeightProcessor` | Tested directly — no mocking needed | Clean public interface (`configure`, `processWeight`) |

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

## Conventions

- Test class names: `tst_FeatureName` (Qt convention)
- One test file per logical feature area
- Use `_data()` suffix for data-driven test data functions
- Use `QSignalSpy` for signal verification, never raw signal counters
- Test executables compile their own source files — no shared test library
- Keep mock classes minimal — implement only what tests need
