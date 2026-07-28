# Bugs and latent defects surfaced by this change

A running ledger. The point of this change is to **ship fewer QML bugs**, so what it actually
turns up is the measure of whether it worked — more than any warning count.

Rules for entries: record what was *observed*, not what was assumed; say plainly when something
was checked and turned out to be fine, so nobody re-investigates it; and never promote a
suspicion to a finding without evidence.

---

## Confirmed defects, fixed here

**1. The `qmllint_check` / `qmllint_report` CMake targets had never run — not once.**
CMake picks `Python3_EXECUTABLE`, which on macOS is Xcode's python3.9, and
`scripts/qmllint_report.py` used PEP 604 `Path | None` annotations — 3.10+ syntax that 3.9
evaluates eagerly and dies on at import, before argparse. Every check that "passed" had been run
by hand with a newer interpreter. CI would not have caught it either (ubuntu-24.04 ships 3.12).
Fixed with `from __future__ import annotations`. *Lesson recorded in tasks.md 1.12: verify a
target by running the target, not the command you believe it runs.*

**2. `Settings.<domain>.<prop>` was unverifiable across 1,310 call sites and 281 settings.**
The domain sub-objects were declared `Q_PROPERTY(QObject* …)`, so qmllint, `qmlcachegen` and the
QML language server could all reach `Settings` and see nothing behind it. A typo like
`Settings.brew.slectedFlushPreset` compiled, linted clean, and failed silently at runtime — the
#1661 defect class, over the most-used API in the QML tree. Not a bug in itself; a permanent
blind spot where bugs could sit indefinitely. Closed by declaring the concrete types (design D2a).

**3. Accessibility was silently dead in three components, introduced by this change.**
The dead-import commit removed `import Decenza` from `AccessibleMouseArea.qml` (used in 39 files),
`AccessibleTapHandler.qml` (25) and `AccessibleLabel.qml`. It was correct *at the time* — nothing
in those files referenced the module. The very next commit made `AccessibilityManager` a QML
singleton, which needs the import, and every use site in those three files sits behind
`typeof AccessibilityManager !== "undefined"` — a guard whose entire purpose is not to throw. So
there was no `ReferenceError`, nothing in the app log, and screen-reader announcements from the
app's two most-used interactive primitives simply stopped.

Worth recording because of *how* it was verified: the migration was checked by running the app and
reading the log for `ReferenceError`s, and that method is structurally incapable of seeing this.
The gate saw it — restoring the imports moved exactly those three files from ceilings of 6/6/2
onto the clean list — and they are now locked at zero there, so the regression cannot recur
quietly.

**4. `TranslationManager::create()` could abort the app on a second QQmlEngine.**
It called `setJsEngine()` unconditionally, and that function `qFatal()`s rather than rebind, because
`translate` is a `QJSValue` bound to exactly one engine. Desktop debug builds really do run a
second `QQmlApplicationEngine` (the GHC simulator window) whose QML does `import Decenza`. It
happens not to reference `TranslationManager` today, so the call did not fire — but one `Tr {}`
added to that window would have turned startup into an abort. Three separate comments asserted the
call was there to serve exactly the case that kills the process. Now guarded: `create()` reports
and declines the second engine.

**5. The gate reported "218/218 clean" from a qmllint that had analysed nothing.**
`run()` failed only on a *negative* exit code, on the reasoning that a positive one just means
"found warnings". Measured: qmllint exits **0 even with 10,910 diagnostics**, 1 on a bad flag, 255
on a file it cannot open. A stub qmllint exiting 1 produced a green gate over an empty analysis —
and invited `--update-baseline` to write that emptiness in as the new truth. Now any non-zero exit
is fatal.

**6. On Windows the gate reported the entire tree clean.** qmllint prints backslash paths there
while the baseline keys are forward-slash, so nothing matched and all 218 files fell into the
clean set. Found and fixed twice: the first fix normalised `qml_files()` and missed
`files_from_response()` and `relative_to_repo()`, which are the two that actually decide the keys.

**7. The backup-status toast rendered 0×0**, introduced by the layout-positioning fix. One of the
238 conversions was a `Rectangle` reparented to `Overlay.overlay` at runtime, where
`Layout.preferred*` is inert. The `Quick.attached-property-type` guard that caught six wrongly
converted `Dialog`s cannot catch this one — a reparented `Rectangle` is still an `Item`, so the
attached property is legal; it just has no layout to talk to.

**10. Backup "created" / "restored" screen-reader announcements have never fired.**
`SettingsHistoryDataTab.qml` had four blocks of the shape:

```qml
if (MainController.accessibilityManager) {
    MainController.accessibilityManager.announce(...)
}
```

`MainController` has no `accessibilityManager` property and never has — there is no such accessor
on the class. So the guard was permanently falsy and the announce never ran. A blind user got no
confirmation that a backup succeeded, failed, or restored.

Found the moment `MainController` became a registered type: qmllint could suddenly read its
property list and reported `Member "accessibilityManager" not found on type "MainController"` at
all 8 lines. Before that, `MainController` was a context property — invisible — so the access was
just one more `unqualified` warning among 914. This is the single clearest demonstration of what
the change is for: the defect was reachable, user-visible in its absence, and undetectable by
running the app, because the guard is exactly what stops it from throwing.

Fixed by calling the `AccessibilityManager` singleton directly.

**11. Three `Q_INVOKABLE` methods carry pointer parameter types that were only forward-declared.**
`VisualizerUploader::uploadShot()` takes `const Profile*`, and `Profile` was only forward-declared.
Registering the class made moc demand a complete type ("Pointer Meta Types must either point to
fully-defined types..."). Fixed by including the header — but note what it means: `Profile` is not
a registered QML type either, and no QML calls `uploadShot`, so the `Q_INVOKABLE` is decorative.
Whether to keep the annotation or drop it is a judgement call left to the maintainer rather than
made here. `ShotDataModel`/`SteamDataModel::registerFastSeries(FastLineRenderer*...)` is the same
shape but genuinely used from QML, and works today because `FastLineRenderer` IS a registered type.

---

## Upstream (Qt) defects

**8. `QQmlJSTypeResolver::merge()` is exponential — qmllint cannot lint some valid QML at all.**
On `qml/components/layout/items/CustomItem.qml` (613 lines) stock qmllint 6.11.1 reaches a
**313 GB** footprint and is OOM-killed after 622 s, having emitted 36 of that file's 122 warnings.
Not a slowdown — a failure with no diagnostic and no exit status to act on.
Patch submitted: [qtdeclarative/+/755657](https://codereview.qt-project.org/c/qt/qtdeclarative/+/755657)
(`Pick-to: 6.12 6.11`). Consequence for us: **122 diagnostics in that file that nobody had ever
seen**, because every previous run died before reaching it and a file qmllint never reaches
prints no warnings — so it counted as clean.

**9. Qt silently skips a module's declarative type registration if the module already exists.**
`qqmltypeloader.cpp:783` (and identically `qqmlimport.cpp:920`) registers a module's
compile-time types only `if (!module)`, commented *"If the module already exists, the types must
have been already registered"*. For a module that mixes runtime and declarative registrations
that assumption is false: our twenty-odd `qmlRegisterUncreatableType<…>("Decenza", …)` calls
create the module before QML imports it, so `qml_register_types_Decenza()` is never invoked and
**every** `QML_ELEMENT` type in the module is silently absent at runtime. No warning, no error.
Worked around the way Qt does in its own `tools/qml/main.cpp` (an explicit call). Arguably worth
an upstream report: the failure is silent and the diagnosis took reading three Qt source files.

**12. Apple's `TextToSpeech.framework` overflows a 1-byte heap buffer, so any TTS utterance
aborts an ASan build on macOS 27.0.** Tracked as
[#1675](https://github.com/Kulitorum/Decenza/issues/1675).
Not ours, and not caused by this change — recorded because the next person to touch accessibility
will hit it and lose an hour working out whose bug it is.

```
==92783==ERROR: AddressSanitizer: heap-buffer-overflow
READ of size 4 at 0x6030003f5741 thread T74
    #0 memcpy
    #1 (TextToSpeech:arm64e+0x1ed17c)
0x6030003f5741 is located 0 bytes after 1-byte region [0x…5740,0x…5741)
allocated by thread T74 here:
    #2 swift_slowAlloc (libswiftCore)
    #3 (TextToSpeech:arm64e+0x65a34)
SUMMARY: AddressSanitizer: heap-buffer-overflow (TextToSpeech:arm64e+0x1ed17c)
```

Allocation and faulting read are both inside Apple's framework; every frame from `swift_slowAlloc`
to the `memcpy` is theirs, on their own `TTSExecutor` dispatch queue. No Decenza frame appears on
the thread at all.

**Trigger:** turning the accessibility master switch on. The toggle speaks its own confirmation
("Accessibility enabled"), TTS runs, and the process aborts. It is not string-specific — a relaunch
died ~4 s in while speaking "Home screen". Observed on macOS 27.0 beta (`26A5388g`), Qt 6.11.1,
`libqtexttospeech_speechdarwin`.

**Not attributable to the MainController migration**, which was in flight when it was found: the
four rewritten `AccessibilityManager.announce()` calls in `SettingsHistoryDataTab.qml` had fired
zero times when it crashed (`grep -c "preview= Backup"` → 0). The utterances that did fire are all
pre-existing paths.

**Consequences worth knowing.** Local ASan debug builds cannot exercise accessibility TTS on this
OS at all, which blocks hands-on verification for
[#736](https://github.com/Kulitorum/Decenza/issues/736). Release builds do not abort — no ASan —
but a 4-byte read off a 1-byte allocation is real corruption there too, just silent. There is
nothing to work around on our side; the fix is Apple's.

*Unconfirmed:* that it reproduces on `main` without this branch. Expected to, since no frame is
ours, but nobody has run that A/B yet — do not record it as confirmed until someone does.

---

## Checked and found NOT to be bugs

Recorded so they are not re-investigated.

- **`ThemedPageBackground.qml` referencing `LastShotChartSource` with no `import Decenza`.**
  Looked like a second live instance of #1661. It is not: both files live in `qml/components/`,
  and same-directory QML files are implicitly visible without an import. #1661 was `Theme.qml`
  reaching *across* directories, which is the case that fails. Of the 12 import-less files, only
  these two referenced module singletons at all, and both resolved.

- **`ProfileManager.currentProfilePtr` is an opaque pointer property — loaded, but nothing is
  pulling the trigger.** `Q_PROPERTY(Profile* currentProfilePtr READ currentProfilePtr CONSTANT)`
  on `ProfileManager`, where `Profile` is a **plain C++ class**: no `Q_OBJECT`, no `Q_GADGET`.
  QML therefore receives a value it cannot see into, and the natural-looking
  `ProfileManager.currentProfilePtr.title` would evaluate to `undefined` — the same failure the
  `Q_DECLARE_OPAQUE_POINTER` experiment produced for `Settings` (design D2a), arrived at from the
  other direction.

  **Checked, and currently harmless: no QML file references `currentProfilePtr`**, and qmllint
  reports nothing against it. It is an unexercised API surface, not a live defect, which is why it
  is recorded here rather than filed.

  It matters for **task 3.6 (`ProfileManager`)**, the next migration after this one. Registering
  `ProfileManager` will make qmllint able to read its property list, and this property will surface
  as `unresolved-type` exactly the way MainController's 21 sub-objects did. **The fix is to give
  `Profile` a `Q_GADGET` and register it** (it is a value type with no identity, so a gadget is the
  right shape), *not* to declare it opaque and not to drop the property. Whoever does 3.6 should
  also decide whether QML has any business holding a `Profile` at all — nothing uses it today, so
  deleting the property is a legitimate third option.

---

## Fixed: 36 dead imports

An entire category that no count had ever included, because qmllint reports `unused-imports` at
**Info** severity and the report's regex matched only `Warning|Error`. Dead imports are minor in
themselves; the reason this is recorded is that they were *invisible*, which is the failure mode
this change exists to remove.

17 `QtQuick.Controls`, 9 `QtQuick.Layouts`, 5 `Decenza`, 3 relative-directory imports,
1 `QtQuick.Effects`, 1 `QtGraphs` — 34 files. Removing them left `unqualified` (7,612),
`missing-property` (326) and `unresolved-type` (2) all unchanged, which is the evidence that
nothing lost a type it actually needed. Category now **0** and its exemption entry deleted.

A cross-check against type names in each file flagged 8 as suspicious; all 8 were the check being
crude — `Accessible.Button` is a QtQuick enum, and the rest were mentions in comments. qmllint was
right in every case.

## Triaged: the 257 `Quick.layout-positioning` warnings

Qt's message calls this *undefined behaviour*, which undersells it. Read from the Qt source
(`qquicklayout.cpp:1249`, `qquickgridlayoutengine_p.h:32-47`): when a Layout child has no size
hints and no implicit size, the layout falls back to the item's `width`/`height` — **once**.

```cpp
: useFallbackToWidthOrHeight(true) {}                        // enabled
QQuickLayout::effectiveSizeHints_helper(…, useFallbackToWidthOrHeight);
useFallbackToWidthOrHeight = false;                          // ← after the FIRST pass
```

with the value stored in `m_fallbackWidth` and reused forever. Qt's own comment says so plainly:
*"we only want to use the initial width … the preferred width should return the same value,
regardless of the current width."*

So `width:`/`height:` on a Layout child is **read on the first layout pass and then frozen**.
`Rectangle` and `Item` have `implicitWidth`/`implicitHeight` of 0, so they always take this path;
types with real implicit sizes (`Text`, `Button`) do not and track correctly.

**That makes the warning a live-defect detector for this app**, because the values are not
constant: `Theme.scaled(v)` is `v * Theme.scale`, and `Theme.scale` / `Theme.pageScaleMultiplier`
are reassigned at runtime by window resize (`main.qml:970`) and by a user-facing **"Page scale"**
control (`main.qml:1209`, `main.qml:4550`).

| count | shape | verdict |
|---:|---|---|
| 169 | `Theme.scaled(<constant>)` | **latent bug** — frozen; does not follow page-scale or resize |
| 27 | other `Theme.*` expressions | **latent bug**, same mechanism |
| 1 | `height: parent.height` | **latent bug**, same mechanism |
| 44 | constant literal (`height: 1` separators) | harmless — the frozen value *is* the intended value |
| 3 | other constants | harmless |
| 13 | `anchors` / `x` / `y` | **qmllint false positive** — see below |

**197 latent, 41 harmless, 19 false positives** — corrected from an earlier "13 false
positives". The first pass counted only the `anchors`/`x`/`y` warnings as false positives and
missed that six `width:` warnings were also on `Dialog` objects. That error was caught by the
gate, not by review: the mechanical fix rewrote those six to `Layout.preferredWidth`, and a new
`Quick.attached-property-type` category appeared with exactly six entries — *"Layout attached
property must be attached to an object deriving from Item"*. Reverted; category back to 0.

**FIXED.** All 238 real sites now use `Layout.preferredWidth`/`preferredHeight`, so the size is
re-read when the binding changes instead of frozen at the first layout pass. The category went
**257 -> 19**, and the 19 that remain are the false positives, which cannot be driven to zero
from this side.

Verified in the running app, not just by the counts: walked Settings -> History & Data (28 sites),
the layout editor (31) and the library panel (17) — no visual change — then resized the window,
which is what recomputes `Theme.scale` (`main.qml:970`). The library's 30x30 type-filter squares
scaled proportionally with the window and stayed square, which is the behaviour the fix buys; the
app log showed no binding loops or layout warnings throughout.

Worked example, `qml/main.qml:1055` — the untranslated-string count badge:

```qml
Rectangle {
    visible: TranslationManager.untranslatedCount > 0
    width: untranslatedText.width + Theme.scaled(12)   // frozen at first pass
    height: Theme.scaled(22)
}
```

Its width depends on a **sibling's** width, so the badge cannot grow when the count goes from one
digit to three. Nobody would connect that symptom to a layout rule.

**Severity, honestly.** Two caveats keep this from being "197 broken widgets":

1. `Theme.scale` and `pageScaleMultiplier` do change at runtime, but **infrequently** — a window
   resize or a deliberate visit to the page-scale control, not routine use. Most of the 196
   scale-derived cases are therefore latent: wrong, reachable, and rarely reached. They are worth
   fixing because the trigger is a user action we ship a control for, not because the UI is
   broken today.
2. An item that is also `Layout.fillWidth`/`fillHeight` is sized by the fill rather than the
   frozen hint, so a further subset looks correct regardless. The frozen hint still feeds
   minimum/preferred sizing, so they stay wrong, just less visibly.

The exception is the one case that does **not** depend on scale at all: `qml/main.qml:1055`, whose
width tracks a sibling's width and so goes stale on ordinary content changes rather than on a rare
user action. That one is the most likely of the group to be reachable in normal use, and is worth
fixing on its own regardless of what happens to the other 196.

**Fix:** `Layout.preferredWidth` / `Layout.preferredHeight` (or `implicitWidth`/`implicitHeight`),
which the layout re-reads when the binding changes. Mechanical, ~197 sites, and it should be its
own change rather than smuggled into this one.

### qmllint false positive worth reporting upstream

The 13 `anchors`/`x`/`y` warnings are wrong. qmllint flags any child *declared lexically* inside a
Layout without checking that the type is an `Item` the layout can manage:

- 12 are `Popup`/`Dialog` with `parent: Overlay.overlay`. `QQuickPopup : public QObject` — not an
  `Item`, so no Layout ever manages it, and the declaration site is irrelevant.
- 1 is `IdlePage.qml:820`, the `y` of a `Translate` transform. `QQuickTranslate : public
  QQuickTransform` — also not an `Item`.

Both verified in the Qt 6.11.1 source. Nothing to fix in Decenza; do not "clean these up".

## Observed, not yet diagnosed

Do not treat these as fixed or as false positives — nobody has looked.

- **Transient binding-order `TypeError`s at startup** — `parts.length`, `legendRoot.entries.length`,
  `puckPrepRows.length` and similar reading `undefined` during construction
  (`Theme.qml`, `CustomLegend.qml`, `ChangeBeansDialog.qml`, `SwitchEquipmentDialog.qml`). Present
  in one app run, absent in the next with no relevant code change, so they look timing-dependent.
  Not attributed to this change and not claimed as fixed by it.
- **122 diagnostics in `CustomItem.qml`**, seen for the first time once a patched qmllint could
  finish the file. Counted, not read.
- **`qml/Decenza/` was a stale hand-written module stub that nothing builds — now deleted**, along
  with the rest of the dead Qt Design Studio scaffold (`de1-qt.qrc`, `de1-qt.qmlproject`,
  `qml/designer/DE1AppStubs.qml`). See task 3.13 for the full finding. The short version is that
  it was not merely unused: its `qmldir` declares `module Decenza` while its `plugins.qmltypes`
  exports every type under `DE1App/`, so it has been unresolvable since the #338 rename. The
  paragraph below is the original note, kept because its reasoning is what found the rest.

- **`qml/Decenza/` is a stale hand-written module stub that nothing builds.** It contains a
  `qmldir` declaring `module Decenza` plus a `plugins.qmltypes` listing types by hand — including
  `MachineStateType`, whose registration this change deleted, and exporting them under a
  `DE1App/…` module name that no longer exists anywhere else in the tree. Neither file is
  referenced by `CMakeLists.txt`, and the real `Decenza` module is generated into the build
  directory by `qt_add_qml_module`, which is also what qmllint imports. So it is not currently
  doing harm — but it is a second, hand-maintained declaration of the same module name that
  nobody updates, and it will drift further with every type this change migrates. Deleting it is
  the obvious call; it was left alone here only because no task covers it and nothing verified
  whether Qt Creator's QML editor reads it for design-time completion.

- **~15 `typeof MachineState !== "undefined"` / `typeof ProfileManager !== "undefined"` guards are
  now permanently true.** A context property could genuinely be absent; a registered singleton's
  type wrapper always resolves, so every one of these fallbacks became unreachable the moment
  those two names were migrated. That is not merely dead code: the fallback used to render `"—"`,
  and now the guard passes and the expression behind it — `MachineState.scaleWeight.toFixed(1)`,
  `MachineState.tareScale()` — throws instead, failing the whole binding and rendering blank or
  stale. The two in files this change already touched were removed
  (`ShotPlanItem.qml:65`, `IdlePage.qml:668`). The rest are in files this change does not touch:
  `CustomItem.qml` (11 sites) and `SteamItem.qml:124`. Left alone deliberately — `CustomItem.qml`
  is the file a released qmllint cannot analyse (see 1.11), so no static tool will flag them and
  the edit would be unverifiable here. Worth a sweep once the patched qmllint is the CI default.

## missing-property triage (326 findings)

Six causes, not 326 problems. One real bug, ~216 structural false positives, 32 unexplained.

**REAL, fixed:** `Theme.dangerColor` at four sites in `SettingsDebugTab.qml`. No such property —
Theme declares `errorColor` — so both result dialogs rendered their error state with an undefined
border and text colour, i.e. an error looked like a success. Same shape as #1661.

**Structural false positives — the code is correct and works:**
- **88 × `Qt.inputMethod.commit/hide/show`**, reported as members missing on `QObject`. qmllint
  types the `Qt` global's `inputMethod` loosely. `CLAUDE.md` *mandates* `Qt.inputMethod.commit()`
  before reading a `TextField.text`, so these are the documented idiom being flagged.
- **104 × `pageStack.currentItem.<pageProperty>`**. `StackView.currentItem` is typed `QQuickItem`,
  so every page-specific property is "missing". Many sites already guard with
  `typeof x !== "undefined"` — the code is deliberately duck-typed and the warning cannot know it.
- **24 × root-window members** (`goToScreensaver`, `openBrewSettings`, `sessionMeasuredMilkG`, …)
  reached through a reference typed `QQuickWindow`. They are declared on `main.qml`'s root.

Silencing these three needs either per-line suppressions — which rebuilds the hiding problem this
work exists to end — or typing the page/window interface properly. That is a real refactor and
should be decided deliberately, not smuggled in.

**32 UNEXPLAINED — do not exempt these until someone understands them.**
`DrinkType` (21) and `SettingsTabs` (11) report members missing that demonstrably exist
(`DrinkType.qml` declares `shortLabel`, `longLabel`, `icon`, `icons`, `fromRecipeMap`; every
flagged `SettingsTabs` member is declared too). What was established, by experiment:

- **It is per-singleton, not per-caller.** In `RecipesPage.qml`, one invocation: all 94 `Theme.x`
  accesses resolve, and the 2 `DrinkType.` accesses produce 3 findings. `Theme` is declared
  identically — `pragma Singleton`, `import QtQuick`, `import Decenza`, `QtObject { … }`.
- **Not directory-relative.** Files in `qml/components/` — the same directory as `DrinkType.qml` —
  are flagged exactly like files in `qml/pages/`.
- **Not the member kind.** Functions fail, and so does `SettingsTabs.tabLabels`, a
  `readonly property var`.
- **Not missing type annotations.** Giving `shortLabel` an explicit `(t: string) : string`
  signature changed nothing.
- **A file OUTSIDE the module resolves them fine.** A scratch `.qml` importing Decenza and calling
  `DrinkType.shortLabel(0)` and `SettingsTabs.visibleTabs()` lints clean, with `-I` and with the
  generated `.rsp` alike. Only files that are themselves part of the module fail — yet `Theme`,
  also part of it, resolves from those same files.

That last pair is contradictory on any simple theory, which is why no root cause is claimed here.
Next step is to reproduce against a newer qtdeclarative and, if it persists, report upstream.
Treat as suspected tool defect; the runtime behaviour is correct (recipe cards render their drink
labels).

---

## The gate's first CI run: `GHCSimulatorWindow.qml` was linted by nobody

Nightly run 30310401195 — the first execution of the `qmllint_check` step added to
`nightly-sanitizers.yml` — failed with:

> `qml/simulator/GHCSimulatorWindow.qml: exists but is not in the qt_add_qml_module list, so it
> is neither linted nor bundled.`

**Not a runtime bug**, and the first reading of it here was wrong. `DECENZA_SIMULATOR` is defined
on every non-mobile platform including Linux, so it is tempting to conclude Linux compiles the
simulator and then loads a QML file that is not in the resource. It does not: the load site at
`src/main.cpp:3718` sits under `#if (defined(Q_OS_WIN) || defined(Q_OS_MACOS)) && defined(QT_DEBUG)`,
so Linux never opens that window. The CMake guard and the C++ guard agreed all along.

What was actually broken is **lint coverage, and only in CI**. `QML_FILES` appended the file under
`if(WIN32 OR (APPLE AND NOT IOS))`, so the module's file set — which is what
`.rcc/qmllint/Decenza.rsp` is generated from, and therefore what the gate lints — differed by
platform. Local macOS runs linted 217 files; Linux CI linted 216. The missing one carries a
baseline ceiling of **57 diagnostics** that the only automated lint run had never once evaluated.

Fixed by making the file set platform-invariant: the guard is now `if(NOT ANDROID AND NOT IOS)`,
so Linux bundles a window it will never load, purely so it lints one. The alternative —
`NOT_IN_MODULE_BY_DESIGN` in `scripts/qmllint_report.py` — buys the same green CI at the price of
the file never being linted anywhere automated, which is the outcome the check exists to prevent.

Generalisable point: **a per-file gate is only as trustworthy as the invariance of its file set.**
A baseline generated on one platform and enforced on another silently stops covering anything the
second platform excludes, and the exclusion reads as a pass. The `unlisted` check caught this only
because it compares files on disk against the module list rather than trusting the module list
alone. The remaining conditional append — Quick3D screensavers, under
`if(ENABLE_QUICK3D AND Qt6Quick3D_FOUND)` — has the same shape; it did not fire because the Linux
runner does find Quick3D, which is luck rather than design.

---

## Migrating a context property makes its `typeof … !== "undefined"` guard permanently true

Flagged independently by two reviewers on PR #1680, and it extends a gap this document already
recorded for `MachineState`/`ProfileManager`. Writing it down properly because the reasoning is
counter-intuitive in a way that makes it easy to "clean up" wrongly in either direction.

A context property that was never set resolves to `undefined`, so `typeof X !== "undefined"` is a
real guard. A registered singleton type is **always** defined, so after migration the guard can
never be false — but the failure it was written for did not disappear, it changed shape and got
slightly worse:

| | before (context property) | after (`QML_SINGLETON`) |
|---|---|---|
| name never published | `undefined` — guard catches it, fallback branch runs | `create()` returns `nullptr`, name is `null`, `typeof null === "object"` — guard **passes**, `X.member` throws a TypeError in the binding |

So the guard now reads as protection that is not there. PR #1680 adds `DE1Device`, `BLEManager`
and `BatteryManager` to the singleton set, and `qml/components/layout/items/CustomItem.qml`
carries ~19 more guards on those names (lines 154-476) that join the ~11 already noted for
`MachineState`, plus `SteamItem.qml:124`.

**Practical risk here is low and that is not an accident**: all three are constructed
unconditionally in `main()` with no `#ifdef` around the declaration and published before
`engine.load()`, so these particular guards were already vacuously true while they were context
properties. Nothing regressed at runtime.

**FIXED — 153 guards across 54 files**, but not the obvious way, and the obvious way is a
regression. The tempting rewrite is `typeof X !== "undefined"` to a plain truthiness test `if (X)`.
That throws: a bare undeclared identifier in a condition is a **ReferenceError**, and `typeof` is
the only form that is safe on one. The wholesale-registration failure is exactly the case where
the identifier IS undeclared — `qml_register_types_Decenza()` has failed that way before, 1,081
ReferenceErrors against a green build — so the "dead" branch is the one that would start throwing.
`Theme.qml`'s `EmojiAssets` guard is the same shape; it was deleted and reverted during this change
once review showed why.

The fix is therefore purely additive, and cannot regress the case the guard was written for:

```js
typeof X !== "undefined" && X !== null
```

`undefined` is false, `null` is false, a live object is true — all three states, correctly.

Scope was decided by what the type actually is, not by grep. 169 `typeof … !== "undefined"` guards
exist in `qml/`; only the 153 naming a type exported in `Decenza.qmltypes` were rewritten. The
other 16 name things that are still context properties or plain JS — `ScaleDevice`,
`Refractometer`, `USBManager`, `GHCSimulator`, `McpServer`, `pageStack`, `Window` — where the
unmodified `typeof` test is still exactly right. Rewriting those would have been wrong in the other
direction. When `ScaleDevice` and `Refractometer` get their façade, their guards join the 153.

The count in the paragraph above (~19 + ~11) was what the reviewers saw in two files. The real
distribution was dominated by something neither had looked at: `AccessibilityManager`, 106 guards
across 50 files, registered long before this change. That is the value of counting before fixing.
Verified with the patched qmllint, which is the only binary that can read `CustomItem.qml` at all
(`UNLINTABLE_BY_TOOL_BUG`) and therefore the only one that could confirm its 32 rewritten guards:
gate passed, 218/218 linted, 89 clean, no diagnostic movement in either direction. Full suite 105
passed.
