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

---

## Upstream (Qt) defects

**3. `QQmlJSTypeResolver::merge()` is exponential — qmllint cannot lint some valid QML at all.**
On `qml/components/layout/items/CustomItem.qml` (613 lines) stock qmllint 6.11.1 reaches a
**313 GB** footprint and is OOM-killed after 622 s, having emitted 36 of that file's 122 warnings.
Not a slowdown — a failure with no diagnostic and no exit status to act on.
Patch submitted: [qtdeclarative/+/755657](https://codereview.qt-project.org/c/qt/qtdeclarative/+/755657)
(`Pick-to: 6.12 6.11`). Consequence for us: **122 diagnostics in that file that nobody had ever
seen**, because every previous run died before reaching it and a file qmllint never reaches
prints no warnings — so it counted as clean.

**4. Qt silently skips a module's declarative type registration if the module already exists.**
`qqmltypeloader.cpp:783` (and identically `qqmlimport.cpp:920`) registers a module's
compile-time types only `if (!module)`, commented *"If the module already exists, the types must
have been already registered"*. For a module that mixes runtime and declarative registrations
that assumption is false: our twenty-odd `qmlRegisterUncreatableType<…>("Decenza", …)` calls
create the module before QML imports it, so `qml_register_types_Decenza()` is never invoked and
**every** `QML_ELEMENT` type in the module is silently absent at runtime. No warning, no error.
Worked around the way Qt does in its own `tools/qml/main.cpp` (an explicit call). Arguably worth
an upstream report: the failure is silent and the diagnosis took reading three Qt source files.

---

## Checked and found NOT to be bugs

Recorded so they are not re-investigated.

- **`ThemedPageBackground.qml` referencing `LastShotChartSource` with no `import Decenza`.**
  Looked like a second live instance of #1661. It is not: both files live in `qml/components/`,
  and same-directory QML files are implicitly visible without an import. #1661 was `Theme.qml`
  reaching *across* directories, which is the case that fails. Of the 12 import-less files, only
  these two referenced module singletons at all, and both resolved.

---

## Observed, not yet diagnosed

Do not treat these as fixed or as false positives — nobody has looked.

- **255 `Quick.layout-positioning` warnings.** Qt's own wording is *undefined behaviour*: setting
  `width`/`height`/`anchors`/`y` on an item a layout manages. The remedy is
  `implicitWidth`/`implicitHeight` or `Layout.preferredWidth`/`Height`. These were invisible until
  the report parser stopped dropping dotted category names, so they have never been triaged. This
  is the largest pool of plausible real defects the change has exposed.
- **Transient binding-order `TypeError`s at startup** — `parts.length`, `legendRoot.entries.length`,
  `puckPrepRows.length` and similar reading `undefined` during construction
  (`Theme.qml`, `CustomLegend.qml`, `ChangeBeansDialog.qml`, `SwitchEquipmentDialog.qml`). Present
  in one app run, absent in the next with no relevant code change, so they look timing-dependent.
  Not attributed to this change and not claimed as fixed by it.
- **122 diagnostics in `CustomItem.qml`**, seen for the first time once a patched qmllint could
  finish the file. Counted, not read.
