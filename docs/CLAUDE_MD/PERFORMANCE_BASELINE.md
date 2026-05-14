# Charts Migration Performance Baseline

Measurement protocol for the `migrate-charting-to-qt-graphs` change. Numbers recorded here drive the P.5 decision at the end of Stage 1: continue to Stages 2–3 now on the Qt 6.11 Quick Shapes backend, or pause until Qt 6.12 GA and flip `useCanvasPainter: true` on the migrated views.

The protocol is identical for the **pre-migration** Qt Charts build and the **post-migration** Qt Graphs build. The win (or null result) is the delta between the two runs on the same hardware.

## Primary device

| | |
|---|---|
| Tablet | Samsung SM-X210 (Decent tablet) |
| Android | API level shipped on the device — do not update mid-protocol |
| App build | Release configuration, `qmake`/CMake `Release`, signed APK from the same commit |
| Power | Plugged in via the DE1 USB-C charger; screen at 100 % brightness, auto-rotation locked to landscape |
| Background apps | Force-stop everything other than Decenza before each run |

Comparison points: a Windows dev machine and macOS run from the same commit. Tablet numbers are authoritative; desktop numbers only flag gross regressions that the tablet would also see — but for the P.5 directional decision, a clean Charts-vs-Graphs delta on a single Mac is enough to tell whether the new backend is helping or hurting per-frame CPU.

### Measuring on macOS (no DE1)

Without a real machine to pair, drive the live-shot graph from the in-app simulator (Settings → Developer / Demo Mode, or whichever flag your branch uses to feed canned pressure/flow/temperature/weight curves). The absolute frame timings on a desktop GPU are not comparable to the tablet, but the **Charts-vs-Graphs delta on the same Mac, same commit-pair, same simulator profile** is a valid directional signal.

- Build Release in Qt Creator (`Release` build config). Debug builds dominate the numbers with Qt-internal asserts and overwhelm the renderer cost we care about.
- Launch from Terminal so `QSG_RENDER_TIMING=1` is in the environment, e.g.:
  ```
  QSG_RENDER_TIMING=1 ./build-Decenza-Desktop_Qt_6_11_1_clang_kit-Release/Decenza.app/Contents/MacOS/Decenza
  ```
  Scene-graph timing lines stream to stderr — `tee` them to a file for offline diffing.
- Run on the Mac's internal display (no external monitor) at a fixed refresh rate so the 60 Hz frame budget is constant between runs.
- Memory comparison: macOS Activity Monitor → "Memory" column reading after 50 history rows have been opened. Not as precise as `dumpsys meminfo`, but adequate for the directional check.
- Skip the "Memory Graphics line" row in the result template (Android-only). Add a single "Mac RSS (MB)" row for the directional check.

## Four metrics

### 1. Live shot FPS during a 30-second pour

The most important number. Measures the cost of redrawing pressure / flow / temperature / weight series at the live update rate during the densest extraction phase.

**Setup**
- `export QSG_RENDER_TIMING=1` before launching the app (Android: `adb shell setprop debug.qt.qsg_render_timing 1` then restart the process)
- Pair a real DE1 (not the simulator) — simulator throttles output and produces unrepresentative timings
- Use the `decenza-default` profile so the pour is reproducible (long pressure-ramp + sustained 9 bar plateau)
- Place a real puck in the portafilter — a blind shot has different timing because the pressure curve saturates earlier

**Procedure**
1. Open the shot graph page (`ShotGraph` in espresso mode)
2. Start the shot; let the preinfusion phase pass
3. Begin a 30-second wall-clock window when pressure first crosses 6 bar
4. End the window 30 s later
5. Save the run; pull `adb logcat -d` and grep for `qt.scenegraph.time.renderer` lines

**Record**
- Mean frame time (ms) across the window
- 95th-percentile frame time (ms)
- Frame-drop count: number of frames slower than 16.7 ms
- Subjective: any visible stutter? Note phase if so.

### 2. Shot history scroll FPS

Stresses many `HistoryShotGraph` instances rendered into list rows simultaneously. The list virtualises but each visible row owns a full graph.

**Setup**
- Database must contain ≥200 shots. Use a backup loaded via the data-migration flow if the device is fresh.
- Cold start the app — do not run this immediately after the live-pour measurement (GPU/CPU thermal state matters)

**Procedure**
1. Navigate to Shot History
2. Flick from the top to the bottom at a constant pace, ~1 page per second
3. Then flick back to the top
4. Repeat three times

**Record**
- Mean frame time (ms) over the scroll
- Any visible blank/placeholder rows during scroll (yes/no)
- Subjective smoothness on a 1–5 scale

### 3. Graph first-paint latency

How long from "show the graph page" to "graph is fully drawn with axes, legend, and series visible". Affects perceived snappiness when switching pages.

**Procedure**
1. From the main screen, tap the entry that opens the live shot graph
2. Stopwatch (or trace) the time until the goal curve and axes are fully painted
3. Repeat five times, drop the first (cache warm-up), report the mean of four

A `Performance.now()`-style timestamp inside the QML `Component.onCompleted` of the graph view is acceptable instead of a stopwatch — log it and read from `adb logcat`.

### 4. Memory footprint with 50 shots open

Indirect proxy for renderer allocations. Qt Graphs uses scene-graph nodes; Qt Charts uses a `QGraphicsView`. Their resident-memory profiles differ.

**Setup**
- Cold start
- Open Shot History
- Scroll until 50 rows have been instantiated and torn down (use the visible-rows counter if available, or estimate ~5 swipes)

**Record** (`adb shell dumpsys meminfo io.github.kulitorum.decenza_de1 | head -40`)
- Total PSS (kB)
- Java heap (kB)
- Native heap (kB)
- Graphics (kB) — this is the line most likely to change between backends

## Two runs, one commit each

Run the full protocol twice from the same hardware state:

1. **Pre-migration**: `git checkout <commit immediately before PR #1144>` → rebuild release → install → measure
2. **Post-migration**: `git checkout main` (or the latest Stage 1 commit) → rebuild release → install → measure

Do not reuse old measurements from before the Qt 6.11.1 upgrade — that upgrade itself shifted the cup-fill cost and the comparison would be muddied.

## Result template

Copy this block into the Stage 1 PR description (or into a comment on the migration tracking issue) when the runs complete:

```
Hardware: Samsung SM-X210, Decenza <git-sha>, Qt 6.11.1
Profile: decenza-default, real DE1, real puck

| Metric                          | Qt Charts (pre) | Qt Graphs (post) | Δ           |
| ------------------------------- | --------------- | ---------------- | ----------- |
| Live shot mean frame time (ms)  |                 |                  |             |
| Live shot p95 frame time (ms)   |                 |                  |             |
| Live shot dropped frames (n)    |                 |                  |             |
| History scroll mean (ms)        |                 |                  |             |
| History scroll subjective (1-5) |                 |                  |             |
| First-paint latency (ms)        |                 |                  |             |
| Memory Graphics line (kB)       |                 |                  |             |
| Memory Native heap (kB)         |                 |                  |             |
```

## Decision rule (P.5)

Defined in `openspec/changes/migrate-charting-to-qt-graphs/tasks.md` §P.5. Repeated here for convenience:

- **Measurable CPU drop / FPS improvement** on the live-shot metric → schedule Stages 2 + 3 immediately on the 6.11 Quick Shapes backend.
- **Neutral or worse** → pause Stages 2 + 3 until Qt 6.12 GA (2026-09-22). After Decenza upgrades to 6.12, flip `useCanvasPainter: true` on `FlowCalibrationPage`, re-measure, then re-decide.

A "measurable" win on the tablet is a clear sustained delta on the live-shot mean frame time at a minimum, not a noisy single-millisecond difference. The bridge components and the migration pattern carry forward unchanged either way.

## Re-running after Qt 6.12

When Decenza upgrades to Qt 6.12 (separate `upgrade-qt-6-12` change) and the `useCanvasPainter: true` flip lands on each migrated `GraphsView` (tracked in `charts-qt-6-12-polish`), re-run the protocol against the latest migrated graph. Append the results to the table; do not overwrite the 6.11 row — both rows are useful for future decisions.
