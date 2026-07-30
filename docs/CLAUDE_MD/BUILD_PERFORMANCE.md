# Build Performance: QML rebuilds and qmlcachegen AOT

Why some builds recompile every QML file in the module, what that costs, and what
the levers are. Also records the measured AOT coverage and the finding that
Decenza's per-frame hot path contains no QML at all — which is what decides
whether AOT is worth paying for.

**Rebuild-cost numbers were measured on 2026-07-27 at commit `99f8f8f1`; the AOT
coverage section was re-measured on 2026-07-29** after the QML cleanup landed.
macOS Debug build, Qt 6.11.1. They are a snapshot, not a contract. Every one is
re-derivable with the commands in "How to re-derive" at the bottom — do that
rather than trusting these figures after the QML tree has moved.

The re-measurement is itself the argument for doing so: this document previously
predicted which AOT skips were fixable, and got it backwards in both directions.
See "AOT coverage".

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

## Cross-file QML cache staleness

That last sentence is also a **correctness** problem, not just a cost note. "One
QML edit rebuilds exactly one unit" is wrong whenever the edited file is one that
other files resolve types through — a singleton, or any type used by name.

The dependency list for a cachegen output names only the file's own source:

```
build .rcc/qmlcache/Decenza_qml/pages/RecipeEditorPage_qml.cpp: CUSTOM_COMMAND
    <that .qml>  <4 .qrc files>  Decenza/Decenza.qmltypes  Decenza/qmldir
```

`Decenza.qmltypes` covers the **C++** registrations. QML-declared types are resolved
by reading the other file's source, reached through `<builddir>/Decenza/qml/…`, the
copy staged by `Decenza_copy_qml` — and that target is wired as an **order-only**
dependency (`||`), which ninja deliberately does not treat as a reason to rebuild.
So editing `qml/Theme.qml` changes how all ~215 other units *should* compile and
rebuilds none of them. The stale cache persists across as many incremental builds
as you like.

This is not theoretical. Adding `: string` to `Theme.tempUnitSuffix()` produced a
build where `Theme_qml.cpp` called it as a typed QString function and every caller
still called it as `var`. It also burns investigation time in a nastier way: **you
cannot A/B a QML type annotation with an incremental build.** Two experiments on the
same day produced byte-identical generated code for "before" and "after" purely
because the staged copy already held the "after" text.

**That mixed state is NOT benign. This document said it was; running the app
disproved it.** Exercising espresso and steam on a mixed build logged

```
EspressoPage.qml:882:21:  Unable to assign [undefined] to QString
SteamGraph.qml:213:13:    Unable to assign [undefined] to QString
```

both on `text: Theme.tempUnitSuffix()`, in units last generated **before** the
annotation landed while `Theme_qml.cpp` was generated after it. That is the same
function and the same message as the original report which got annotations banned
from those seven wrappers for a release. So the mixed cache does not merely fail to
prove things — **it reproduces, exactly, the runtime failure that the ban was
founded on.** The annotations are still fine; the stale cache is the whole defect,
and it is user-visible, not cosmetic.

**It caught the fix for itself, on the day that fix merged.** Merging #1714 into
#1715 brought the newly annotated `Theme.qml`; the staged copy and the binary both
updated, the suite passed, and of 217 `.aotstats` exactly **one** was newer than
`Theme.qml` — `Theme_qml.cpp.aotstats`. Every other unit still called the seven
wrappers as `var`. Knowing about this section is not protection: an ordinary
rebuild does not clear it, and nothing in the build output says so.

### This is FIXED as of #1717 — but know what the fix is and how it can silently die

`CMakeLists.txt` now makes every QML unit depend on every `.qml` source, using
`add_custom_command(OUTPUT ... APPEND DEPENDS ...)` to add inputs to the commands
**Qt** created. Editing `Theme.qml` rebuilds all 221. No `touch` step, and the
mixed-cache failure above cannot happen.

It is coarse on purpose. The precise graph would mean reimplementing QML type
resolution, and getting *that* subtly wrong reintroduces silent staleness, which
is the thing being fixed.

It is affordable because of `restat = 1` on the cachegen edges. Measured from this
project's `.ninja_log`: qmlcachegen is **205 ms** median per file, while compiling
the `.cpp` it emits is **3387 ms** — 16.5x more. A one-file QML edit re-runs
cachegen on all 221 (seconds of wall time, in parallel), then ninja compares the
regenerated `.cpp` against the old one and prunes every unit whose output came out
byte-identical. The expensive half still only happens where the code really changed.

**The failure mode to know about:** `add_custom_command(APPEND)` against an OUTPUT
that does not match an existing command is a **silent no-op**. The path is
reconstructed from Qt's own formula (`Qt6QmlMacros.cmake:3841-3847` — the reason
the directory is `Decenza_qml/` is that the `Decenza_` target prefix is glued onto
the leading `qml/` of the relative path). If Qt ever changes that formula, the
wiring detaches, CMake still prints a healthy `wired for 221 units`, and the mixed
cache comes back. Hence:

```bash
cmake --build <builddir> --target qml_dep_wiring_check
```

`cmake/CheckQmlDepWiring.cmake` does not inspect the wiring; it exercises it. It
touches `Theme.qml`, asks ninja what is now dirty, and fails unless all 221 units
are. Run it after any Qt upgrade. A count printed by the thing being tested is not
evidence — the whole of this file's history says so.

The real fix is `qmlcachegen --depfile` upstream. The tool has no such flag today
(check `tools/qmlcachegen/qmlcachegen.cpp`; there is no dependency output of any
kind), and Qt's own `add_custom_command` has no `DEPFILE`, so the information does
not exist to be used. This is the local stand-in until it does.

## Where the time goes

Two full-rebuild events, from the same log:

```
16:07  wall=58.2s   cachegen: 436 edges (98s cpu)   qml .o: 218 files (624s cpu)
16:15  wall=52.5s   cachegen: 436 edges (101s cpu)  qml .o: 218 files (649s cpu)
```

It is not qmlcachegen — it is compiling what qmlcachegen emits: **2.0M lines** of
generated C++ across 216 files, every one containing AOT-compiled functions. Any
lever aimed at this cost should target the generated C++, not the tool.

## AOT coverage, and the two levers that moved it

**Re-measured 2026-07-29** after the QML cleanup (#1665, #1688, #1690, #1695) and
again after #1698 (`FINAL` on the settings/controller accessors, type annotations
in `Theme.qml` and `IdlePage.qml`).

```
                          post-cleanup      post-#1698      post-#1715      post-#1717
total bindings/functions       29097           29097           29087           29097
  AOT compiled            13017 (44.7%)   17631 (60.6%)   18168 (62.5%)   20459 (70.3%)
  skipped -> interpreter  14665 (50.4%)   10051 (34.5%)    9504 (32.7%)    7223 (24.8%)
  partial                  1415 ( 4.9%)    1415 ( 4.9%)    1415 ( 4.9%)    1415 ( 4.9%)
```

The post-#1717 column is the whole of that PR: the button family, `main.qml`'s
`ApplicationWindow`, and the dialogs. Its three parts are described below in that order.

#1715 rooted 31 pages at `QtQuick.Templates.Page`; **id skips went 1,464 -> 510**.

#1717 did the same for the button family — `AccessibleButton`, `StyledIconButton`,
`ActionButton`, `StyledSwitch`. Attributing every `Could not find property/signal` skip to
the element it was written on (walk back from the reported line to the enclosing `Type {`)
showed that class was not spread thin: **`AccessibleButton` alone owned 1,049 of 2,653**,
almost all of it `text:` and `onClicked:` on instances in other files. Re-rooting the four
took the whole class 2,653 -> 1,271 and cleared all four types to zero.

**But the total only moved 9,504 -> 8,820, not by the 1,353 those four accounted for.**
Both numbers are right. Resolving a type lets qmlcachegen reach *further* into bindings it
used to abandon at the first unresolvable member, where it then hits the next blocker:
`TranslationManager.translate` went **1,833 -> 2,168** in the same sweep. This is the
qmllint effect in `QML_GOTCHAS.md` ("a count going UP after a fix is usually the fix
working") showing up in the AOT stats. **Diff per-cause, never on the total** — judged on
the total alone this migration looks half as effective as it is, and the +335 looks like a
regression it introduced.

#1717 then did `main.qml`'s `ApplicationWindow` and the dialogs, which were the two classes
left. The window was a one-line change worth 583 skips: Material's `ApplicationWindow.qml`
is **three lines** whose only content is `color: Material.backgroundColor`, and `main.qml`
already set its own `color`.

The dialogs were 131 sites — 27 file roots and 104 inline `Dialog {}` blocks across 39 more
files — and they are the case where re-rooting each one directly would have been WRONG.
Unlike the buttons, the style's `Dialog.qml` contributed things that were genuinely on
screen and that no dialog in the app overrode: the grow-fade `enter`/`exit` transitions and
the modal dim. Not one of the 131 declared `enter` or `exit`. Re-rooting each at `T.Dialog`
would have meant 131 copies of both. `DecenzaDialog.qml` carries them once.

Two facts made that safe, and both were checked rather than assumed:

- **All 104 inline blocks override `background`** — established by walking each block's
  braces, not by grepping the file, because a file can contain a `background:` belonging to
  something else entirely. Had any block relied on the style's background, re-rooting would
  have rendered it invisible: the base supplies none.
- **The only `Dialog`-specific API in the whole tree is `BrewDialog`'s `onRejected`**, which
  `T.Dialog` has. That is what made one base sufficient where two looked necessary.

Three inline `Popup {}` blocks were deliberately left alone: Material's `Popup` is non-modal
with `padding: 12`, while the base carries `Dialog`'s `modal: true` and `padding: 24`, so
re-rooting them would start dimming the screen and blocking input behind them.

The post-#1715 column is measured on a **consistent** cache — all 213 units regenerated
together, after #1714's `Theme.qml` annotations. #1715's own commit message reports
18,127 / 62.32 %; that sweep predated the #1714 merge, so it is a real measurement of a
tree that no longer exists rather than an error. The 41-binding difference is those
annotations. Cite this column, not that one.

**The id-skip and hard-skip columns overlap — do not add them.** An id skip IS a hard
skip, counted again by cause, so `compiled + hard skips` is the same 27,672 before and
after and a reviewer summing all three across the two sweeps will find a phantom
954-binding discrepancy and report a measurement bug that is not there. One did.

### Re-rooting a type at Templates BREAKS `as <ControlsType>` casts elsewhere

Not a styling concern, and not visible to the compiler, qmllint or the test suite. Under a
style, `QtQuick.Controls.Page` is the style's `Page.qml` — a **composite** type — and a
composite matches only instances whose own metaobject chain contains it. Qt says so in
`qqmltypewrapper.cpp:513-516`: *"a composite type cannot be equal to a non-composite object
instance (Rectangle{} is never an instance of CustomRectangle)"*. `as` is `doInstanceof`,
and a failed **object** cast returns `null`, not `undefined` (`qv4runtime.cpp:394-406`).

So re-rooting a page at `T.Page` drops the style composite out of its chain, and every
`x as Page` **elsewhere in the tree** starts returning null against it. #1715 hit exactly
this: `main.qml`'s `shotChartOnCurrentPage` binding cast `pageStack.currentItem as Page`,
and the migration would have pinned it to `false` on every page — a silently dead feature,
in a file the migration never touched. The migration commit asserted "No file declares a
property typed Page or casts to it"; the cast was there, one grep away.

**Before re-rooting any type at Templates, grep the whole tree for `as <ThatType>` and for
`property <ThatType>` — not just the files you are editing — and rewrite the survivors to
the Templates type.** `as T.Page` is strictly more general: it is the C++ `QQuickPage`, so
it matches Templates-rooted and Controls-rooted instances alike.

### What the style's QML supplies that Templates does not

`Page` was nearly free because a Material `Page` is nearly empty. Most controls are not.
Open the style file — `qtdeclarative/src/quickcontrols/material/<Type>.qml` — and carry
over anything the migrated file does not already replace. Three things bite:

- **`implicitWidth` / `implicitHeight`.** The `Math.max(implicitBackground… , implicitContent…)`
  formula lives ONLY in the style QML. C++ computes the `implicitContentWidth` and
  `implicitBackgroundWidth` inputs but leaves the result to the style
  (`qquickcontrol.cpp:1749-1757`), so a Templates root that does not declare it is **0 wide**.
- **Insets.** Material inset buttons by 6 (all four sides for `RoundButton`, top/bottom for
  `Button`), so the background paints smaller than the control. Dropping them silently
  fattens every instance. They are **unscaled literals** in Material — keep them unscaled,
  or the look changes at every `Theme.scaled` factor except 1.
- **`contentItem`.** Only safe to omit if the file already replaces it. `ColoredIcon.qml` is
  the counter-example and is deliberately still Controls-rooted: its whole mechanism is
  Material's internal `IconLabel` doing `icon.color` tinting, and a Templates root would
  render nothing. 38 skips is not worth a blank icon.

Palette and default font are NOT in this list — those come from `QQuickTheme`, which the
style plugin registers against the C++ class, so a Templates-rooted control still gets them.

**Read the measurement warning below before trusting any number you take
yourself.** #1698 reported +1.7 points for days because every intermediate
measurement was taken against a build that had recompiled only the two edited
QML files. The other 213 units' `.aotstats` still described the *previous* state,
so the change looked local. The real figure only appeared once a `Q_OBJECT`
header edit invalidated `Decenza.qmltypes` and forced all 215 units to
regenerate. **Before believing an AOT number, check that the `.aotstats` files
are newer than your edit:**

```bash
find <build>/.rcc/qmlcache -name '*.aotstats' -newer <the file you changed> | wc -l
```

If that is not ~216, you are reading the last build, not this one.

**The untyped-function cascade is the whole story.** Annotating ~32 functions in
`Theme.qml` alone took `call to untyped JavaScript function` from **4,681 skips to
499** — because `Theme.*` is called from every file in the app, so typing one
file unblocked roughly 4,200 call sites elsewhere. This is the 8:1 ratio
described below, paying off in the direction the ratio predicted. The `FINAL`
work, by contrast, was worth 429 skips: real, but an order of magnitude smaller.

Grouped by root cause. Re-derived post-#1717 on a consistent cache, by exact
`message` string out of the `.aotstats` (7,223 hard skips; shares are of that):

| Skips | Share | `message` |
|---|---|---|
| 2253 | 31.2 % | `Type TranslationManager does not have a property translate for calling` |
| 754 | 10.4 % | `Cannot generate efficient code for call to untyped JavaScript function` |
| 598 | 8.3 % | `Could not find property "X".` |
| 524 | 7.3 % | `Functions without type annotations won't be compiled` |
| 157 | 2.2 % | `Cannot generate efficient code for storing an array in a non-sequence type` |
| 131 | 1.8 % | `...incompatible or ambiguous types: conversion to QVariant` |
| 83 | 1.1 % | `Cannot access value for name Overlay` |
| 78 | 1.1 % | `Could not find signal "X".` |
| 73 | 1.0 % | `Cannot access value for name Dialog` |
| **0** | — | **context property** (was 3351 / 19.0 %) |
| **0** | — | **`root`** (was 583 across two rows) |

What is left, in the order worth taking:

- **`TranslationManager.translate` is the single largest class at 2,253, now 31 % of all
  that remains.** It has GROWN at every step, because each Controls-composite fix lets
  qmlcachegen reach further into bindings and land on this instead. Do not touch it
  without reading `tst_translationreactivity.cpp` first — `translate` is a `Q_PROPERTY`
  holding a callable precisely so that a binding records a dependency on it, and that
  reactivity is worth more than the skips. 3,248 call sites depend on it. Treat this
  number as the floor of the program, not the next target.
- **The untyped-JS classes together are 1,278** (754 calls + 524 definitions) and are now
  the largest *actionable* group. This is annotation work with no UI surface — the same
  lever that took `Theme.qml` from 4,681 skips to 499. It is the obvious next move.
- **`Could not find property` + `signal` is down to 676 from 2,669.** What is left is the
  long tail of the same defect: `Label`, `StyledComboBox`, `ColoredIcon`, `TextArea`,
  `ScrollView`. Individually small, and `ColoredIcon` in particular must NOT be migrated
  (see "What the style's QML supplies that Templates does not").
- **`Overlay` 83 and `Dialog` 73 are new, and are the dialog migration's own residue** —
  files that still import `QtQuick.Controls` only to name `Overlay.overlay` or a
  `Dialog.CloseOnEscape` enum. Cheap to clear, but check each one: three files' imports
  became genuinely unused in #1717 and the qmllint gate caught them, which is what a
  mechanical re-rooting always leaves behind.
- To attribute a `Could not find property/signal` skip to the type that caused it, walk
  back from the reported line to the enclosing `Type {` and count by that. The message
  names the member, never the type — counting by message alone tells you `text` is a
  problem, not that `AccessibleButton` is.

Re-derive rather than hand-adjusting these rows, and only from a cache you have just
forced consistent — see the staleness section above for why that is not optional.

### AOT buys speed with binary size, and macOS has a shelf at 16 MiB

#1717 crossed it. The Debug link now prints:

```
ld: warning: __eh_frame section too large (max 16MB) to encode dwarf unwind offsets
    in compact unwind table, performance of exception handling might be affected
```

Measured on that binary — `size -m` — `__eh_frame` is **16,859,952 bytes** against a
16,777,216-byte cap: over by 82 KB, 0.5 %. AOT-compiled functions went 18,168 -> 20,459
(+12.6 %) in the same change, and every one of them is real generated C++ carrying unwind
data, so back-computing puts the previous build around 15 MB — under. This change tipped it.

**What it costs is small and specific.** macOS's compact-unwind table stores offsets INTO
`__eh_frame`; past 16 MiB the offset no longer fits, so affected functions fall back to the
unwinder parsing DWARF. That is slower only while an exception is being thrown and unwound.
Not correctness, not normal execution, and Decenza does not throw on any hot path.

**What matters is the direction.** "0 hard skips" means strictly more generated C++, so this
number only rises, and we hit a hard platform limit at 70.3 % coverage with a quarter of the
tree still interpreted. Two things worth knowing before anyone panics or acts:

- This is the **Debug** build, with ASan *and* UBSan — `__text` alone is 213 MB. Release has
  neither and is far smaller, so it very likely sits under the cap. That is expectation, not
  measurement: **nobody has checked a Release link.** Do that before treating it as a
  release-affecting problem.
- If Release ever does cross it, the lever is `-fno-asynchronous-unwind-tables` scoped to the
  generated QML units. That is a real tradeoff, not a free win, and needs its own
  justification — do not reach for it on the strength of a warning that costs nothing today.

**Untyped JS functions — was 32 %, now 5 %, and still the best lever.** Of 1,107
`function` declarations under `qml/`, **54** now carry a return-type annotation and
**57** a typed parameter (13 and 15 before #1698). qmlcachegen will not compile an
untyped function, and will not compile a *call* to one either, so definitions poison
call sites at roughly 8:1. #1698 spent that ratio deliberately: ~32 annotations in
`Theme.qml` plus 11 in `IdlePage.qml` cut the bucket from 4,681 to 499.

**The remaining untyped definitions are NOT all free to annotate — but the most
cited reason not to was false.** Seven wrappers in `Theme.qml` (`tempUnitSuffix`,
`cToDisplay`, …) carried a "DO NOT ADD TYPE ANNOTATIONS" ban for a release, on the
strength of a real runtime failure, and this document repeated it as an unexplained
qmlcachegen defect. #1714 established there was no defect: the failure was the
cross-file cache staleness described above — `Theme.qml` recompiled typed while
every caller's cached unit still called it as `var`. All seven are annotated now.
See `qml/Theme.qml:413`, which keeps the account so the ban is not reinstated from
memory. **An unexplained mechanism is a reason to keep investigating, not a reason
to write a ban into a reference doc.**

Annotations do still change runtime semantics, not just codegen: a `string` parameter
converts `undefined` to the literal `"undefined"` (`qv4jscall_p.h:337` ->
`qv4runtime.cpp:618`), which silently defeats an `if (!x) return ""` guard. Optional
parameters and guarded ones must be `var`, which coerces nothing
(`qv4jscall_p.h:331`).

**Context properties: gone.** `src/main.cpp` now contains zero `setContextProperty`
calls and 23 QML singletons (`src/core/contextsingletons_qml.h`). The entire
second-largest cause, 3,351 skips, is off the board.

**But the prediction about which ones were fixable was exactly backwards.** The
earlier draft said the swappable device handles "realistically cannot" be typed,
because `ScaleDevice` and `Refractometer` are reassigned at 11 sites as hardware
connects and disconnects, while the stable globals were the fixable ones. Measured:

| | draft prediction | after the cleanup | after #1698 |
|---|---|---|---|
| `ScaleDevice`, `Refractometer` | unfixable | **0 skips** | 0 |
| `AccessibilityManager` | fixable | **0 skips** | 0 |
| `MainController` | fixable | 313 -> 152 | **shadowable-base share fixed** |
| `Settings` | fixable | 595 -> 539 | **shadowable-base share fixed** |
| `TranslationManager` | fixable | 1831 -> **1790** | 1787 — permanent, see below |

The middle column is the state the prediction was judged against; do not read it as
current. #1698 then took the *shadowable-base* cause from 574 skips to 72 project-wide,
which is the part of `Settings`/`MainController` that was fixable at all. Their residual
skips now sit in other causes (untyped calls, unresolved ids), counted in the table above.

The swappable ones went to zero because swapping moved *behind* a stable singleton
that re-points its internals — mutability was never the obstacle, dynamic scoping
was. The two the draft was most confident about barely moved, each for a reason
that did not exist when it was written:

- **`TranslationManager` (1,787).** `Type TranslationManager does not have a
  property translate for calling`. `translate` is a `Q_PROPERTY` holding a
  callable, deliberately — that is what makes a binding re-evaluate on a language
  change, and 3,248 call sites depend on it (see `translationmanager.h` and
  `CLAUDE.md`). qmlcachegen cannot compile a call through a callable property. This
  is a **priced tradeoff, not a defect**: correct reactive translation costs 12 % of
  all AOT skips. Do not "fix" it without reading `tst_translationreactivity.cpp`.
- **`Settings` (539) and `MainController` (152) — FIXED in #1698, 574 -> 72.**
  `Cannot use shadowable base type for further lookups: Settings::theme with type
  SettingsTheme`. The 12 domain sub-objects that `CLAUDE.md` mandates
  (`Settings.<domain>.<prop>`) were non-final `QObject`-derived properties, so
  qmlcachegen refused to chain a lookup through them — a subclass could shadow the
  member. `FINAL` on the accessors is the fix (`qqmljsshadowcheck.cpp:197-198`), and
  `SETTINGS.md` now requires it on every new domain accessor. The 72 left are
  scattered across `AIManager`, `DataMigrationClient` and friends, plus two on
  `QQuickWindow` that are Qt's own.

**The remaining third is largely downstream**, as before: `Could not find property
"text"` (432), `"clicked"` (441), and `Cannot retrieve a non-object type by ID`
(root, idlePage, postShotReviewPage) mostly follow from the causes above.

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
`.steamTemperature`. At ~5 Hz that is ~160 binding evaluations per second.

This paragraph used to end "every one of them is interpreted and always will be —
`DE1Device` is a context property, structurally out of AOT's reach." **That is no
longer true.** `DE1Device` is a QML singleton (`contextsingletons_qml.h:185-190`)
and only **3** skips in the whole tree now mention it, so those readouts are
compiled rather than structurally excluded.

The conclusion below survives anyway, on the independent argument: the hot path
contains no QML at all, so what those 32 bindings cost is a rounding error either
way. The stale reasoning is called out rather than deleted because it was the load
-bearing sentence, and anyone re-deriving this will otherwise wonder why the
verdict did not move when the premise did.

**So the steady-state value of AOT to Decenza is close to zero.** It may still help
cold binding execution, which affects page-open latency (felt on the Android
tablet). Bytecode caching — which `--only-bytecode` retains — already covers parse
and compile cost; only *execution* of bindings would change. That is the open
question, and it is measurable (see below).

## The two biggest remaining skip classes, and why neither is worth chasing

Measured 2026-07-30 at 60.7% coverage: 17,661 compiled, 1,415 partial, 10,011
hard-skipped (`codegenResult` `2`; note `1` is **partial**, not skipped — ranking
files by "not 0" makes `ShotDetailPage.qml` look like 2% when it is mostly
partial). Two reasons dominate, and both are traps for anyone ranking by count.

**`Type TranslationManager does not have a property translate` — 1787 skips, 18%
of all skips, spread over every page.** `translate` is a `Q_PROPERTY` holding a
callable so that bindings record a dependency and re-evaluate on a language
change; that is exactly what qmlcachegen cannot compile a call through. Biggest
number on the board, near-smallest prize: the property's `NOTIFY` is
`translationsChanged`, which fires on a language switch or a translation
download and nothing else, so these bindings evaluate **once at page
construction and never again** for the ~95% of users who never change language.
AOT would remove the JS frame, not the C++ hash lookup inside. Not worth
touching the reactivity mechanism `tst_translationreactivity` guards.

**`Cannot retrieve a non-object type by ID: <id>` — 1464 skips across 82 files.**
The condition is `variant() == ObjectById && !retrieved->isReferenceType()`
(`qqmljstypepropagator.cpp:637-641`): `genericType()` walks the base chain looking
for a scope whose `internalName()` is literally `QObject`, and returns
`m_jsValueType` — a value type — when the walk does not get there
(`qqmljstyperesolver.cpp:875-921`).

**Referencing by `id` any object whose type comes from QtQuick.Controls fails.
Everything else works.** Established by controlled experiment (an 11-line probe
compiled with the project's own qmlcachegen flags), not by correlation:

| Referenced object's type | Origin | Result |
|---|---|---|
| `Item`, `Rectangle`, `Timer` | QtQuick / QtQml C++ | compiles |
| `T.Page` | QtQuick.**Templates** (`QQuickPage`) | compiles |
| `ThemedIcon` (roots at `Item`) | Decenza composite | compiles |
| `Page`, `StackView`, `Dialog`, `ApplicationWindow` | QtQuick.**Controls** | **fails** |
| `AccessibleButton` (roots at `Button`) | Decenza composite | **fails** |

It is inherited, so a Decenza composite fails exactly when its own root is a
Controls type. It applies to the file's own root id and to nested ids alike —
root vs nested makes no difference, only the type does.

The reason is visible in `QtQuick/Controls/qmldir`: that module declares **no
types**. Every concrete type arrives from a style module (`optional import
QtQuick.Controls.Material auto`, … , `default import QtQuick.Controls.Basic
auto`), selected at run time. So the compiler cannot resolve the base chain to
QObject, and every id-load of such an object degrades to `var`.

**A fix exists and is mechanical.** Swapping `SteamPage.qml`'s root from `Page` to
`T.Page` — one import line and one identifier — measured:

| SteamPage.qml root | compiled | hard skips | of which id-skips |
|---|---|---|---|
| `Page` (Controls) | 252 | 108 | 52 |
| `T.Page` (Templates) | **280** | **80** | **1** |

What that swap does *not* settle is behaviour: a Templates type ships no style
visuals, so anything relying on the Controls default background, padding or
font needs checking per file. 74 of the 82 affected files already set
`background:` explicitly. Treat the table as proof the AOT half works, and the
visual half as unverified.

**Do not try to fix it by dropping the `id.` qualifier.** Measured on
`SteamPage.qml`: stripping all 202 `steamPage.` prefixes removed all 52 id skips
and added 51 `Cannot access value for name …` and `method … cannot be resolved`
skips, for a net of **−1** — and introduced qmllint `unqualified` warnings inside
`Connections` blocks, where the scope object is the Connections rather than the
page. The qualifier was never the cause.

**Two traps that made this take four wrong answers.** First, a skipped entry
reports only its *first* error, so an id failure is invisible in any function
that already failed on `Could not find signal "clicked"` or `Functions without
type annotations` — which makes it look like the same id compiles in one place
and not another. Second, 291 groups tree-wide have two entries sharing one
(line, column, function), one compiled and one skipped, so attributing a result
to a source line by proximity is unreliable. Both produce clean-looking
correlations that are artifacts.

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
the only validation QML gets never exercises the path 45 % of bindings take in
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
`message` giving the reason). The shape is
`modules[].moduleFiles[].entries[]`, and the key is **`filePath`**, capital P —
this used to say `filepath`, which silently matches nothing, so a dedup written
from it collapses all 216 files into one bucket and reports 288 entries instead
of 29,097. Deduplicate by `filePath` and verify the distinct-file count is ~216
before believing any total.

On a Qt Creator build dir the real duplication risk is different from what this
section used to claim: each stats file holds exactly **one** module file, but the
tree contains a second copy of the whole cache under
`qtc_Ninja_Multi_Config/.rcc/qmlcache/`. Glob both and every number doubles.
Restrict the glob to the top-level `.rcc/`, or dedup by `filePath`, which fixes it
either way.

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
