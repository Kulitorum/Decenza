# Build Performance: QML rebuilds and qmlcachegen AOT

Why some builds recompile every QML file in the module, what that costs, and what
the levers are. Also records the measured AOT coverage and the finding that
Decenza's per-frame hot path contains no QML at all — which is what decides
whether AOT is worth paying for.

**All numbers below were measured on 2026-07-27 at commit `99f8f8f1`**, macOS
Debug build, Qt 6.11.1. They are a snapshot, not a contract. Every one is
re-derivable with the commands in "How to re-derive" at the bottom — do that
rather than trusting these figures after the QML tree has moved.

## The short version

- Editing a `.qml` file rebuilds **one** compilation unit, not all of them.
- A build recompiles **all ~215** QML units if and only if `Decenza.qmltypes` is
  rewritten, which happens when the *content* of `qt6decenza_metatypes.json`
  changes — i.e. when moc metadata on one of the ~117 `Q_OBJECT`/`Q_GADGET`
  classes changes.
- That costs ~55 s wall and ~12 min CPU, dominated by compiling generated C++,
  not by qmlcachegen itself.
- Splitting the QML into several modules does **not** fix this. The invalidation
  comes from the C++ type registry, which every QML module would import.

## Why a C++ change rebuilds every QML file

The dependency chain, all of it visible in `build.ninja`:

```
any Q_OBJECT header/source change
  → AUTOMOC re-runs
  → meta_types/qt6decenza_metatypes.json          (moc --collect-json)
  → Decenza/Decenza.qmltypes                      (qmltyperegistrar)
  → every .rcc/qmlcache/**/X_qml.cpp              (qmlcachegen, ~215 of them)
  → every corresponding .o                        (the expensive part)
  → relink
```

Each qmlcachegen edge declares the module-wide registry as an input:

```
build .rcc/qmlcache/Decenza_qml/components/layout/items/ClockItem_qml.cpp ... :
    CUSTOM_COMMAND
    qmlcachegen
    qml/components/layout/items/ClockItem.qml     ← its own source
    .qt/rcc/Decenza_raw_qml_0.qrc                 ← module-wide
    Decenza/Decenza.qmltypes                      ← module-wide
    Decenza/qmldir                                ← module-wide
```

**`Decenza.qmltypes` is not generated from QML files.** Its edge takes exactly two
project inputs — `qmltypes/Decenza_foreign_types.txt` and
`meta_types/qt6decenza_metatypes.json` — both pure C++ moc output. No `.qml` file
is an input to it, so editing one cannot invalidate the registry.

This is worth stating explicitly because the intuitive diagnosis is the opposite
one ("Qt regenerates the type registry when any QML file in the module changes"),
and acting on it leads to splitting the QML into modules — a large change that
would not have helped.

### The invalidation is load-bearing, not waste

`Decenza.qmltypes` is exactly the set of type facts all ~215 compiled QML units
were compiled *against*. When it genuinely changes, recompiling them is
correctness. The goal is to reduce how often it changes, not to sever the edge.

### `restat` prunes the chain when content is unchanged

Both the metatypes and qmltypes edges carry `restat = 1`. If moc regenerates the
JSON but the content is identical, ninja compares mtimes after the command and
stops propagation. This is why most C++ edits do *not* trigger the full rebuild:
only metadata-visible changes (a new `Q_PROPERTY`, signal, slot, `Q_INVOKABLE`,
`Q_ENUM`) alter the JSON. Editing a function body does not.

**Consequence for diagnosis: `ninja -n` overstates badly.** A dry run cannot apply
`restat`, so it reports the whole cascade as dirty. Measured on a tree where the
real next build touched no QML at all, `ninja -n` claimed 577 dirty edges
including all 218 qmlcachegen ones. Use the ninja *log* (below) for truth, not the
dry run.

### Measured correlation

Twenty consecutive builds from one day's `.ninja_log`:

| Builds | `Decenza.qmltypes` re-ran | qmlcache edges |
|---|---|---|
| 15:42, 15:55, 15:59, 16:03 | no | 0 |
| 15:09, 16:33 | metatypes only, pruned by `restat` | 0–15 |
| 14:57, 15:51, 16:07, 16:15 | **yes** | **1076** |

Four for four. And single-QML-file edits across months of history rebuilt exactly
one unit each — `QuickRatingRow_qml.cpp`, `ConnectionStatusItem_qml.cpp`,
`RecipeComposerPage_qml.cpp`.

## Where the time goes

Two full-rebuild events, from the same log:

```
16:07  wall=58.2s   cachegen: 436 edges (98s cpu)   qml .o: 218 files (624s cpu)
16:15  wall=52.5s   cachegen: 436 edges (101s cpu)  qml .o: 218 files (649s cpu)
```

It is not qmlcachegen — it is compiling what qmlcachegen emits: **2.0M lines** of
generated C++ across 216 files, every one containing AOT-compiled functions. Any
lever aimed at this cost should target the generated C++, not the tool.

## AOT coverage, and why it is only 40 %

```
total bindings/functions  29388
  AOT compiled            11766  (40.0%)
  skipped -> interpreter  16231  (55.2%)
  partial                  1391  ( 4.7%)
```

Grouped by root cause:

| Skips | Share | Cause |
|---|---|---|
| 4493 | 25.5 % | call to untyped JS function |
| 3351 | 19.0 % | context property |
| 2974 | 16.9 % | member on unresolved type |
| 2858 | 16.2 % | unresolved id / model role |
| 1998 | 11.3 % | other |
| 1386 | 7.9 % | (no message) |
| 562 | 3.2 % | untyped function definition |

**Untyped JS functions (29 %).** Of 1,064 `function` declarations under `qml/`,
**10** carry a return-type annotation and **9** a typed parameter. qmlcachegen
will not compile an untyped function, and will not compile a *call* to one
either — so 562 untyped definitions poison 4,493 call sites, an 8:1 cascade.

**Context properties (19 %).** `src/main.cpp` exposes 39 globals via
`setContextProperty` and registers **zero** QML singletons. qmlcachegen cannot
resolve context properties by design: they are dynamically scoped and may be
reassigned at runtime. `TranslationManager` alone accounts for 1,831 skips,
`Settings` 595, `MainController` 313.

This is only partly fixable, and the reason is in the code: `ScaleDevice` and
`Refractometer` are reassigned at 11 sites in `main.cpp` as hardware connects and
disconnects. That mutability is *why* they are context properties, and why they
cannot be typed. The stable ones (`Settings`, `TranslationManager`,
`MainController`, `AccessibilityManager`) could become singletons; the swappable
device handles realistically cannot.

**The remaining third is largely downstream.** `Could not find property "text"`
(455), `"clicked"` (451), `modelData` (241) and friends mostly follow from the two
causes above — once `Settings` is untyped, every `Settings.theme.x` under it
fails. Fixing the roots recovers part of this bucket for free.

## Whether AOT is worth it here

**The per-sample hot path contains no QML.**

```
DE1 BLE notify (~5 Hz)
  → DE1Device::parseShotSample                       C++
  → emit shotSampleReceived → ShotDataModel::addSample   C++  (QVector<QPointF>)
  → 33 ms flush timer (~30 fps)                      C++  batches 9 series
  → FastLineRenderer::setPoints / appendPoint        C++  QQuickItem
  → updatePaintNode → QSGNode                        C++  scene graph
```

`FastLineRenderer` is a `QQuickItem` with `updatePaintNode`
(`src/rendering/fastlinerenderer.h`), and `ShotDataModel` pushes into all nine
series directly in C++ (`src/models/shotdatamodel.cpp`). No binding evaluation
anywhere in that chain. The one place it used to leak into QML was already fixed —
see the comment in `shotdatamodel.cpp`: *"Emit deferred rawTimeChanged (axis
recalc) at flush rate instead of per-sample."*

The only QML that re-evaluates per sample is **32 bindings across 7 files**, all
numeric readouts on `DE1Device.pressure` / `.flow` / `.temperature` /
`.steamTemperature`. At ~5 Hz that is ~160 binding evaluations per second, and
every one of them is interpreted and always will be — `DE1Device` is a context
property, structurally out of AOT's reach.

**So the steady-state value of AOT to Decenza is close to zero.** It may still help
cold binding execution, which affects page-open latency (felt on the Android
tablet). Bytecode caching — which `--only-bytecode` retains — already covers parse
and compile cost; only *execution* of bindings would change. That is the open
question, and it is measurable (see below).

## Levers

### 1. Drop AOT — `--only-bytecode`

Removes ~85 % of the rebuild cost. Both pieces verified present in Qt 6.11.1:
`--only-bytecode` is a real `qmlcachegen` flag, and `QT_QMLCACHEGEN_ARGUMENTS` is
read by `Qt6QmlMacros.cmake`. Bytecode stays cached; AOT-compiled bindings are
lost.

**The risk is that it changes which code executes.** `Function::call`
(`qv4function.cpp`) dispatches on `kind`, and `AotCompiled` calls the generated
machine code with **no runtime fallback** to the interpreter. Qt's safety
mechanism is at compile time — qmlcachegen refuses anything it cannot prove,
which is why the skip rate is 55 %. Divergence would require a Qt codegen bug,
not a Decenza bug, and 60 % of bindings already run interpreted in the shipped
build. But it is a different path, and there is no net.

**The toggle is asymmetric, which decides the shape of the fix.** An AOT-enabled
binary can be run *as if* bytecode-only (below); a `--only-bytecode` binary has no
AOT code to switch back on. Since Decenza has no QML test harness — QML is
validated by manually running the Debug build — making Debug bytecode-only means
the only validation QML gets never exercises the path 40 % of bindings take in
the shipped app. Prefer an opt-in cache variable over defaulting it on for Debug,
unless the A/B below shows AOT buys nothing, in which case turning it off
*everywhere including Release* is better than either: same speed win, and dev and
prod stop diverging.

### 2. Move non-QML-facing C++ out of the qml module target

Semantically inert — it changes which target's moc metadata feeds the registry,
with no effect on generated code or runtime behaviour. Reduces how *often* the
rebuild fires, and helps Release and CI too, not just Debug.

Reach is narrower than it first looks, because of the load-bearing point above:
removing a class from the registry to stop it triggering rebuilds also removes it
from qmlcachegen's view, pushing its bindings from AOT to interpreted. The trade
is free only for classes qmlcachegen already cannot see — the 39
`setContextProperty` objects, which resolve dynamically through `QMetaObject` at
runtime. The skip table above is the evidence that their type info earns nothing.

The two levers are orthogonal (frequency × cost) and compose.

## How to A/B AOT at runtime

No rebuild needed — the AOT code is already in the binary and the engine can be
told to ignore it.

| | |
|---|---|
| **Variable** | `QML_DISK_CACHE` |
| **Value** | `aot-bytecode,qmlc` |

`DiskCache::Enabled = AotByteCode | AotNative | QmlcRead | QmlcWrite`
(`qv4engine_p.h`), and the option string is parsed up from `Disabled`, so that
value is exactly the default minus `AotNative`. Arm A is no variable; arm B adds
it. JIT and bytecode caching are identical across both.

Set it in Qt Creator under **Projects → Run → Environment** — the *Run*
environment, not Build.

**Do not use `QV4_FORCE_INTERPRETER` for this.** It also disables the JIT
(`s_jitCallCountThreshold = INT_MAX`, `qv4engine.cpp`), so it measures AOT+JIT vs
pure interpreter and overstates AOT's value.

**Launch with Run, not Start Debugging.** `diskCacheOptions()` returns `Disabled`
outright when `debugger()` is non-null, so attaching the QML debugger disables AOT
on its own and both arms collapse to the same thing. (`QT_QML_DEBUG` in
`CMakeLists.txt` only links the enabler in; it does not attach anything.)

**Metric:** `src/main.cpp` has always-on startup checkpoints. The AOT-sensitive
window is the gap between `checkpoint("Context properties & type registration")`
and `checkpoint("QML objectCreated")`. Take several runs per arm — the first
populates the disk cache and will skew.

**Caveat:** Debug builds auto-enable ASan (`CMakeLists.txt`, the
`ENABLE_ASAN OR (... CMAKE_BUILD_TYPE STREQUAL "Debug" ...)` guard), which inflates
both arms and adds noise. If the difference looks small and marginal, redo it on
RelWithDebInfo before concluding AOT does not matter.

A typo'd option is not silently ignored — `Ignoring unknown option to
QML_DISK_CACHE:` appears in the application output.

## How to re-derive

From the build directory. `ninja` is at `Qt/Tools/Ninja/ninja` (see
`CMAKE_MAKE_PROGRAM` in `CMakeCache.txt`); note that building is otherwise done
through the Qt Creator MCP, and `ninja -n` here is a graph query that compiles
nothing.

**What a given file's cachegen depends on:**
```bash
grep "^build .rcc/qmlcache/.*/ClockItem_qml.cpp" build.ninja
```

**What actually rebuilt in past builds** — group `.ninja_log` by mtime gaps; each
group is one build. Fields are tab-separated `start end mtime output hash`, mtime
in nanoseconds. Count entries containing `rcc/qmlcache` per group and correlate
against whether `Decenza.qmltypes` appears.

**Why something is dirty right now** (remember: overstates, no `restat`):
```bash
ninja -n -d explain 2>&1 | grep "is dirty" | sort | uniq -c | sort -rn
```

**AOT coverage** — aggregate `codegenResult` across
`.rcc/qmlcache/**/*.aotstats` (JSON; `0` = compiled, `2` = skipped, with a
`message` giving the reason). Deduplicate by `filepath`: each stats file embeds
entries for more than one module file.

**Typed-function count:**
```bash
grep -rhoE '^\s*function\s+\w+\s*\([^)]*\)\s*:\s*\w+' qml --include='*.qml' | wc -l
```

## Related

- `TESTING.md` — why there is no QML test harness, which is what makes the
  Debug-vs-Release AOT divergence matter.
- `SETTINGS.md` — the `Settings` façade split, done for the same class of
  recompile-blast reason on the C++ side.
- `PERFORMANCE_BASELINE.md` — runtime rendering measurement protocol. Different
  axis: that one is frames, this one is builds.
