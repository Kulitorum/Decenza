# Baseline measurements

Every claim in this change is diffed against the numbers here. Measured on `dadbb0c0`, macOS Debug
build dir `build/Qt_6_11_1_for_macOS_Debug`, Qt 6.11.1, from a **full clean rebuild** (Qt Creator
`Rebuild Project`, which cleans outputs and keeps `CMakeCache.txt` — a cache wipe would lose the
hand-set `DECENZA_MACOS_CODESIGN_IDENTITY` and `QMLLINT_SKIP_UNLINTABLE=OFF`).

## Method — read this before trusting any number here

Costs come from ninja's `.ninja_log` (start/end timestamps per edge). Getting a correct reading took
three attempts, each of which produced a plausible and wrong answer. All three failure modes are
recorded because each is easy to walk back into.

**1. Keeping the most recent entry per output.** Wrong across runs: entries accumulate from builds
under different parallelism, so durations are not comparable to each other. Gave 4905 s / 2025 s /
241 s duplicates. Superseded.

**2. Keeping outputs that still exist on disk.** Wrong after a source-list change: Qt Creator's
`Rebuild` cleans the *current* target set, but object files orphaned by removing a source from a
target's list are deleted by nobody. They sit on disk with stale log entries. This reported the
post-change tree as still having 56 duplicate objects and 233 s of duplication, including
`shotanalysis.cpp` — a file whose duplicate entry had been deleted and whose build was green.

**3. Filtering by output mtime against the rebuild's start.** Correct for liveness — 68 stale
entries dropped — and what everything below uses:

```python
cut = os.path.getmtime(snapshot_taken_just_before_the_rebuild)
live = {out: dur for out, (dur, mtime) in rows.items() if mtime >= cut}
```

**But even correct liveness does not make wall-clock totals comparable across rebuilds.** Ninja
records wall clock per edge, so machine load inflates every figure. The before and after rebuilds
here differed enough that the **app arm — which this change never touches — fell 16.8% on its own.**

So: treat `tests/` as the treatment arm and everything outside `tests/` as a **control**. The
difference between the two is the signal; the absolute totals are not. Counts (how many duplicate
object files exist) are load-independent and are the only figures quotable without a control.

## Build cost split

Deduped edges: 2,479. Total: **4,846 s cpu**.

| Slice | Cost | Share |
|---|---|---|
| App and everything not under `tests/` | 2,890 s | 60% |
| **`tests/` total** | **1,956 s** | **40%** |

Within `tests/`:

| Slice | Cost | Share of tests |
|---|---|---|
| Production sources recompiled inside test targets | 688 s | 35.2% |
| Test source translation units | 625 s | 31.9% |
| moc / autogen | 519 s | 26.5% |
| qrc (Qt resources) | 66 s | 3.4% |
| Link + timestamps | 58 s | **3.0%** |
| Other objects | 1 s | 0.1% |

Slowest five edges under `tests/`:

| Cost | Edge |
|---|---|
| 17.7 s | `tst_emojiassets` → `qrc_emoji.cpp.o` |
| 14.8 s | `decenza_testlib` → `mocs_compilation.cpp.o` |
| 13.5 s | `tst_mcpserver_session` → `src/controllers/profilemanager.cpp.o` |
| 12.6 s | `tst_mcpremoteaccess` → `src/controllers/profilemanager.cpp.o` |
| 12.5 s | `tst_mcptools_write` → `src/mcp/mcptools_write.cpp.o` |

Two of the five slowest edges are the *same source file* compiled into different targets.

## Suite size

| Date | `tst_*.cpp` files | KB |
|---|---|---|
| 2026-04-01 | 20 | 357 |
| 2026-06-01 | 52 | 1,284 |
| 2026-07-01 | 60 | 1,518 |
| 2026-07-31 | 107 | 3,080 |

107 test source files, 106 `add_decenza_test` targets, ~2,761 test functions, 67,573 lines.
Test files deleted in project history: **2**.

## Growth ratio — the September check

```
git log --since=2026-07-01 --until=2026-08-01 --numstat --format= dadbb0c0 -- '<pathspec>'
```

| Pathspec | Added | Deleted |
|---|---|---|
| `tests/*.cpp` | 33,927 | 1,543 |
| `src/*` | 53,243 | 9,921 |

**July 2026: 33,927 / 53,243 = 0.637 test lines added per source line added.**

Re-run with `--since=2026-08-01 --until=2026-09-01` to compare. Use *added* lines on both sides —
net would let a large refactor of existing test code read as growth.

## Duplicate production-source compilation

46 redundant object files, **215 s cpu**, 17 sources.

| n | Waste | Source | Consumer set |
|---|---|---|---|
| 9 | 72.6 s | `src/controllers/profilemanager.cpp` | **A** |
| 9 | 35.5 s | `src/profile/profilesavehelper.cpp` | **A** |
| 9 | 19.9 s | `src/core/profilestorage.cpp` | **A** |
| 3 | 14.9 s | `src/mcp/mcpserver.cpp` | **B** ⊂ A |
| 4 | 12.7 s | `src/core/accessibilitymanager.cpp` | **C** + `tst_accessibility_announcements` |
| 3 | 9.5 s | `src/screensaver/screensavervideomanager.cpp` | **C** ⊂ A |
| 3 | 7.8 s | `src/network/beanbaseclient.cpp` | `tst_beanbaseclient`, `tst_coffeebags`, `tst_mcptools_write` |
| 3 | 6.3 s | `src/machine/weightprocessor.cpp` | **D** |
| 3 | 5.7 s | `src/core/batterymanager.cpp` | **C** ⊂ A |
| 3 | 4.8 s | `src/machine/stepexitarbiter.cpp` | **D** |
| 2 | 6.3 s | `src/ai/dialing_blocks.cpp` | `tst_aimanager`, `tst_dialing_blocks` |
| 2 | 4.4 s | `src/core/settingsserializer.cpp` | `tst_backgroundpresets`, `tst_settings` |
| 2 | 3.3 s | `src/ai/shotanalysis.cpp` | **`decenza_testlib`**, `tst_visualizershotparse` |
| 2 | 3.2 s | `src/ai/aiprovider.cpp` | `tst_aimanager`, `tst_aiproviders` |
| 2 | 3.1 s | `src/network/webdebuglogger.cpp` | `tst_profilemanager`, `tst_webdebuglogger` |
| 2 | 2.5 s | `src/network/wifiscaleresult.cpp` | `tst_wifiscalediscovery`, `tst_wifiscaleresult` |
| 2 | 2.0 s | `src/ble/refractometers/refractometerdevice.cpp` | `tst_difluidr1`, `tst_difluidr2` |

Sets:

- **A** (9): `tst_mcpremoteaccess`, `tst_mcpserver_protocol`, `tst_mcpserver_session`,
  `tst_mcptools_presets`, `tst_mcptools_profiles`, `tst_mcptools_shots_debuglog`,
  `tst_mcptools_write`, `tst_profilemanager`, `tst_recipeeditorapppath`
- **B** (3): `tst_mcpremoteaccess`, `tst_mcpserver_protocol`, `tst_mcpserver_session`
- **C** (3): `tst_mcptools_presets`, `tst_mcptools_shots_debuglog`, `tst_mcptools_write`
- **D** (3): `tst_saw`, `tst_scalefeedliveness`, `tst_weightprocessor`

Two findings the design did not anticipate:

1. **B and C are subsets of A**, and the three set-A sources share one identical target list. Three
   libraries cover the ten largest entries — 175 s of the 215 s — not one library per source.
2. **`src/ai/shotanalysis.cpp` is already in `decenza_testlib`** and compiled a second time by
   `tst_visualizershotparse`, which already links it. That is a source-list mistake, not a case for
   a library: delete the entry. Check the rest for the same error before building anything.

## Duplicate Qt resource compilation

| n | Total | Waste | Unit |
|---|---|---|---|
| 3 | 34.9 s | **22.4 s** | `qrc_resources.cpp` |
| 1 | 17.7 s | — | `qrc_emoji.cpp` (`tst_emojiassets`) |
| 1 | 0.1 s | — | `qrc_translationscan_fixtures.cpp` |

## RESULT — after sections 2 and 3

Both rebuilds measured with the mtime filter, identical method.

| | Before | After | Δ |
|---|---|---|---|
| **Duplicate object files** | **46** (215 s) | **0** (0 s) | **−46** |
| qrc objects under `tests/` | 14 / 54 s | 12 / 28 s | −26 s |
| `tests/` (treatment) | 1,956 s | 1,568 s | −19.8% |
| Outside `tests/` (control) | 2,876 s | 2,447 s | −14.9% |
| Total | 4,832 s | 4,015 s | −16.9% |

**Do not quote −19.8% or −16.9%**, and do not quote the control-adjusted figure either without
this paragraph. Two methods disagree:

- **Control-adjusted:** tests fell 4.9 percentage points more than the untouched app arm →
  **≈96 s**.
- **Direct accounting** of the work actually removed: 215 s of duplicate compiles + 26 s of
  duplicate qrc → **241 s**.

They disagree because **the control is contaminated by the treatment.** Removing 46 compiles from a
parallel build reduces contention, so every *remaining* edge finishes faster — including app edges
that this change never touched. The app arm therefore absorbed part of the effect, which deflates
the difference between the arms. The same mechanism showed up earlier from the other side: after
deduplication the one surviving `profilemanager.cpp` compile took 2.82 s where nine parallel copies
had averaged 6.79 s.

The honest statement is that the clean-build saving is **between 96 s and 241 s** and wall-clock
measurement cannot separate the two. What is not in doubt, because they are counts rather than
timings:

- **Duplicate object files: 46 → 0.**
- Distinct qrc objects compiled under `tests/`: 14 → 12.
- The incremental case below: **77.0 s → 12.2 s.**

Suite after: **110 passed, 0 failed, 0 warnings.**

### Duplicates remaining### Duplicates remaining

**None.** `scripts/check_test_source_duplication.py` reports zero across all 106 test targets, and
the `.ninja_log` audit agrees. The check is wired into `text-invariants.yml` so it stays that way.

### Libraries added

| Library | Sources | Consumers | Removed |
|---|---|---|---|
| `decenza_profilelib` | profilemanager, profilesavehelper, profilestorage | 9 | 128 s |
| `decenza_appserviceslib` | screensavervideomanager, batterymanager | 3 | 15.2 s |
| `decenza_a11ylib` | accessibilitymanager | 4 | 12.7 s |
| `decenza_weightlib` | weightprocessor, stepexitarbiter | 3 | 11.1 s |
| `decenza_beanbaselib` | beanbaseclient | 3 | 7.8 s |
| `decenza_mcpserverlib` | mcpserver | 3 | 11.6 s |
| `decenza_aiproviderlib` | aiprovider | 2 | 3.4 s |
| `decenza_dialingblockslib` | dialing_blocks | 2 | 6.1 s |
| `decenza_settingsserializerlib` | settingsserializer | 2 | 3.8 s |
| `decenza_webdebugloggerlib` | webdebuglogger | 2 | 2.9 s |
| `decenza_wifiscaleresultlib` | wifiscaleresult | 2 | 2.2 s |
| `decenza_refractometerlib` | refractometerdevice | 2 | 1.7 s |
| `decenza_testresources` (OBJECT) | resources.qrc | 3 | 26 s |

Suite after: **110 passed, 0 failed, 0 warnings.**

## Incremental baseline

`touch src/controllers/profilemanager.cpp`, then a normal incremental build. 70 edges rebuilt,
11.7 s wall clock, **92.3 s cpu total** — of which **77.0 s is attributable to `tests/`** and 15.3 s
to the app (its own compile of the same file, plus its link).

The test-attributable 77.0 s:

| | Count | Cost | Mean |
|---|---|---|---|
| `profilemanager.cpp` compiled into test targets | 9 | 61.1 s | **6.79 s** |
| Test executable relinks | 9 | 12.8 s | **1.42 s** |
| autogen / other | — | ~3.1 s | — |

Supporting figures from the clean rebuild, needed for the comparison below:

- Pure test-executable link edges: 107, total 56.8 s, **mean 0.53 s** (min 0.19, max 1.31)
- `libdecenza_testlib.a` archive: 0.32 s

Note the two link means disagree — 1.42 s for these nine targets against a 0.53 s all-target mean,
which is above the clean build's per-target maximum of 1.31 s. The nine are among the heaviest
targets in the suite (the MCP and profile binaries), and an incremental link also runs steps a
clean-build link does not. Treat 0.53 s as the honest figure for *typical* targets and 1.42 s as the
figure for *these* targets; the comparison below uses each where it applies.

### What the three options cost on this edit

| | Today | Into `decenza_testlib` | Into a narrow library |
|---|---|---|---|
| Compiles | 9 × 6.79 = **61.1 s** | 1 × 6.79 = 6.8 s | 1 × 6.79 = 6.8 s |
| Archive | — | 0.32 s | 0.32 s |
| Relinks | 9 × 1.42 = 12.8 s | **107 × 0.53 = 56.8 s** | 9 × 1.42 = 12.8 s |
| autogen / other | 3.1 s | 3.1 s | 3.1 s |
| **Total** | **77.0 s** (measured) | **~67.0 s** | **~23.0 s** |

**The narrow library wins by 54 s on this edit; `decenza_testlib` wins by 10 s.** The design
predicted 78 s / 67 s / 13 s — the first two were accurate and the third was optimistic, because it
used the 0.53 s all-target link mean where these nine targets actually cost 1.42 s each.

The conclusion is unchanged and now measured rather than argued: deduplicating into the
universally-linked library recovers a seventh of what a narrow one does, and its relink fan-out
grows with every test target added while the narrow one's does not.

**To reproduce after the restructuring:** `touch src/controllers/profilemanager.cpp`, build, and
diff `.ninja_log` against a snapshot taken immediately before. Expect ~23 s test-attributable, one
compile, nine relinks.
