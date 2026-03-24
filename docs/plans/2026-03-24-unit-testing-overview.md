# Unit Testing Plan: Bug-Driven Priority Coverage

> **For Claude:** Work through each phase document sequentially. Each phase is a separate file. Build and run `ctest --output-on-failure` after each test file. All expected values must be derived from de1app Tcl source code as the authoritative reference. All tests must be fully automated — no manual verification steps.

**Goal:** Add ~210 automated unit tests across 8 test files, prioritized by bug density and code churn from the last 45 days (87 PRs, 82 issues, 63% bug-fix rate).

**Source of truth:** de1app codebase (`/Users/jeffreyh/Documents/GitHub/de1app` on Mac, `C:\code\de1app` on Windows) for all behavioral expectations. Tests must derive expected values independently from de1app Tcl source code (binary.tcl, profile.tcl, machine.tcl, plugin.tcl), NOT from reading Decenza's C++ source. This catches real divergences like the D-Flow fill exit formula bug found in Phase 1. Only behavior/protocol tests reference de1app — UI is Decenza's own design.

## Data-Driven Priority

| Area | Bug-Fix PRs | Fix Rate | Critical/High Bugs | Existing Tests |
|------|:-----------:|:--------:|:-------------------:|:--------------:|
| Machine State / SAW / SAV | 10 | 75-80% | 5 | tst_sav, tst_saw (55 tests) |
| Profile System | 5 | 83% | 2 | None |
| BLE Protocol / Codec | 6 | 52% | 4 | None |
| Settings | — | 45% | 0 | None |
| MCP Tools | 4 | 36% | 1 | None |
| Database / History | 2 | 64% | 0 | None |

## Phase Documents

| Phase | File | Tests | Focus |
|-------|------|:-----:|-------|
| 1 | [Phase 1: Pure Logic](2026-03-24-unit-testing-phase1.md) | ~90 | BinaryCodec + Profile (zero mocks, highest bug coverage per effort) |
| 2 | [Phase 2: Machine State + Frame Serialization](2026-03-24-unit-testing-phase2.md) | ~55 | MachineState gaps + ProfileFrame codec |
| 3 | [Phase 3: Settings & Recipe Validation](2026-03-24-unit-testing-phase3.md) | ~40 | RecipeParams + Settings persistence |
| 4 | [Phase 4: Integration Tests](2026-03-24-unit-testing-phase4.md) | ~25 | RecipeGenerator vs de1app snapshots + ShotSettings wire format |

## Test Infrastructure

**Framework:** Qt Test (QTest) with `-DBUILD_TESTS=ON`

**Patterns (established in tst_sav/tst_saw):**
- `QTEST_GUILESS_MAIN` — headless, no GUI
- `QSignalSpy` — signal verification
- Data-driven `_data()` methods — parametrized test cases
- `friend class` via `#ifdef DECENZA_TESTING` — private member access
- `TestFixture` structs — wired object graphs
- `MockScaleDevice` inheritance — mock via subclass
- Standalone executables per test — compile only needed sources

**New infrastructure needed:**
- `tests/mocks/MockTransport.h` — captures BLE writes for tst_shotsettings (Phase 4)
- `friend class` additions in machinestate.h, de1device.h (Phase 2, 4)
- `CODEC_SOURCES` group in tests/CMakeLists.txt (Phase 1)

## Verification

After each test file:
1. Configure: `cmake -DBUILD_TESTS=ON`
2. Run: `ctest --output-on-failure`
3. All tests pass with 0 failures

After Phase 1 profile tests:
- Cross-check: `python scripts/compare_profiles.py` to verify built-in profiles match de1app TCL sources

## Bugs Found

None so far. Phase 1 confirmed Decenza matches upstream de1app D-Flow plugin (Damian-AU/D_Flow_Espresso_Profile). Note: the local de1app clone's D-Flow submodule was stale — always check upstream repos via the `.gitmodules` URLs.

## Lessons Learned

- D-Flow and A-Flow are **submodules** in de1app, not in the main repo. Always fetch from upstream (github.com/Damian-AU/D_Flow_Espresso_Profile, github.com/Jan3kJ/A_Flow) instead of the local clone.
- D-Flow/A-Flow work by **editing existing frames**, not regenerating from scratch. The `update_D-Flow` proc modifies frames in-place. Decenza's RecipeGenerator generates fresh frames but uses the same formulas.

## Test Count Summary

| Test File | Tests | Phase |
|-----------|:-----:|:-----:|
| tst_binarycodec | 61 | 1 |
| tst_profile | 69 | 1 |
| tst_machinestate | ~25 | 2 |
| tst_profileframe | ~30 | 2 |
| tst_recipeparams | ~20 | 3 |
| tst_settings | ~20 | 3 |
| tst_recipegenerator | ~15 | 4 |
| tst_shotsettings | ~10 | 4 |
| **New total** | **~210** | |
| + existing (sav+saw) | ~55 | |
| **Grand total** | **~265** | |
