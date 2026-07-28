# QML Gotchas

Bug-prone QML patterns discovered the hard way. The corresponding one-liners in `CLAUDE.md` point here for the full code samples and rationale.

## Font property conflict

Cannot use `font: Theme.bodyFont` and then override sub-properties like `font.bold: true`. QML treats this as assigning the property twice.

```qml
// BAD - causes "Property has already been assigned a value" error
Text {
    font: Theme.bodyFont
    font.bold: true  // Error!
}

// GOOD - use individual properties
Text {
    font.family: Theme.bodyFont.family
    font.pixelSize: Theme.bodyFont.pixelSize
    font.bold: true
}
```

## Reserved property names in JS model data

`name` is a reserved QML property (`QObject::objectName`). When a JS array of objects is used as a Repeater model, `modelData.name` resolves to the QML object name (empty string), not the JS property. Use a different key like `label`.

```qml
// BAD - modelData.name resolves to empty string
readonly property var items: [{ name: "Foo" }]
Repeater {
    model: items
    delegate: Text { text: modelData.name }  // Shows nothing!
}

// GOOD - use a non-reserved key
readonly property var items: [{ label: "Foo" }]
Repeater {
    model: items
    delegate: Text { text: modelData.label }  // Works correctly
}
```

Other reserved names to avoid in model data: `parent`, `children`, `data`, `state`, `enabled`, `visible`, `width`, `height`, `x`, `y`, `z`, `focus`, `clip`.

## IME last-word drop on mobile

`Keyboard` is a compile-time singleton (`src/core/keyboard.h`) wrapping QInputMethod. Use it
rather than `Qt.inputMethod`, which qmllint types as a bare `QObject` — every member access on
it is unchecked, so a misspelling is invisible until a user hits it.

On Android/iOS virtual keyboards, the last typed word is held in a composing/pre-edit state and is NOT reflected in `TextField.text` until committed. When a button's `onClicked` reads a text field's `.text` directly (to send, save, or pass to a C++ method), always call `Keyboard.commit()` first — otherwise the last word is silently dropped. This is a no-op on desktop so it is safe to always include.

```qml
// BAD - last word may be missing on mobile
onClicked: {
    doSomething(myField.text)
}

// GOOD - commit pending IME composition first
onClicked: {
    Keyboard.commit()
    doSomething(myField.text)
}
```

This applies to every button/action that reads and immediately uses text input — save dialogs, send buttons, preset name dialogs, TOTP code fields, search/import fields, etc. For `doSave()` helper functions called from both buttons and `Keys.onReturnPressed`, put the commit at the top of the function.

## Keyboard handling for text inputs

Always wrap pages with text input fields in `KeyboardAwareContainer` to shift content above the keyboard on mobile:

```qml
KeyboardAwareContainer {
    id: keyboardContainer
    anchors.fill: parent
    textFields: [myTextField1, myTextField2]  // Register all text inputs

    // Your page content here
    ColumnLayout {
        StyledTextField { id: myTextField1 }
        StyledTextField { id: myTextField2 }
    }
}
```

## FINAL properties on Qt types

Never override FINAL properties on Qt types. Qt 6.10+ marks some `Popup`/`Dialog` properties as FINAL (e.g., `message`, `title`). Declaring `property string message` on a Dialog will prevent the component from loading. Use a different name (e.g., `resultMessage`), or use the inherited property directly if it already exists on the base type.

## `elide` is silently ignored on `Text.RichText` — use `Text.StyledText`

Qt applies `elide` to `Text.PlainText` and `Text.StyledText`, but **not `Text.RichText`** (a `QTextDocument`-backed format). A label with `textFormat: Text.RichText` and `elide: Text.ElideRight` does not truncate — it overruns its width and hard-clips mid-glyph with no ellipsis, which shows up on wider/fallback system fonts (issue #1469). Default to `Text.StyledText` for any HTML-ish label; it supports the tags we use (`<b> <i> <font color> <a href> <img> <br>`) and is lighter than RichText. Reserve `Text.RichText` for the rare label that genuinely needs `QTextDocument`-only features (tables, CSS blocks) — we currently have none. For inline emoji, `Theme.replaceEmojiWithImg` emits `<img align="middle">`, which StyledText honors (it ignores the CSS `style=` attribute). `TextEdit.RichText` (editable fields) is unaffected — this is about read-only `Text`.

## Numeric defaults with `??` not `||`

JavaScript `||` treats `0` as falsy, so `value || 0.6` returns `0.6` when `value` is `0`. Use `value ?? 0.6` (nullish coalescing) which only falls back for `null`/`undefined`. Only use `||` when `0` genuinely means "no data" (e.g., unrated enjoyment, unmeasured TDS).

## `native` is reserved

`native` is a reserved JavaScript keyword — use `nativeName` instead.

## Unicode symbols in text — covered by a bundled fallback font

Decenza Sans carries 927 glyphs: Latin, Greek, Cyrillic, and no symbols at all. Arrows and
geometric shapes written in QML used to be resolved by whatever the host offered, which is why the
same screen could measure differently on two machines. The app now bundles **Noto Sans Math**
(SIL OFL, `resources/fonts/NotoSansMath-Regular.ttf`) purely as a symbol fallback:

- Chained after the UI family in `Theme.fontFamilies`, used by all eight font roles, and set on the
  application font in `main.cpp` so elements that specify only `font.pixelSize` inherit it too.
- Qt consults a later family ONLY for codepoints the earlier one lacks, so letterforms are
  unchanged — Decenza Sans still draws all the text.
- It is a text font, so symbols stay monochrome and take the element's colour. That is the property
  emoji lack, and the reason symbols here are not emoji.

So `→ ← ↗ ↕ ▶ ◀ ⧉` are fine to use. Before introducing a symbol the app does not already use, run
`python3 scripts/check_font_glyph_coverage.py`: it reads both cmaps and reports anything still
falling through to the host. Add another OFL face if needed rather than reaching for an emoji.

Use the script rather than grepping for symbols you happen to think of. Hand-grepping for `→` found
one glyph type; the scan found several more, including `↗`, `⧉`, and the `▶`/`◀` that
are the entire visible content of FlowCalibrationPage's prev/next buttons. It also correctly leaves
`AddLanguagePage`'s native language names alone — those are *supposed* to use a platform fallback.

**Two things that are still wrong:**

- **A bare U+FE0F on a symbol** (`▶️` instead of `▶`) explicitly requests colour-emoji presentation.
  In a plain `Text` — which is what `AccessibleButton.text` and most labels are — that is the macOS
  render-thread crash below. The variation selector makes a working symbol worse.
- **A glyph is not an icon.** For toolbar and navigation affordances use an SVG from `qrc:/icons/`
  with `Image` (buttons: a `Row { Image {} Text {} }` contentItem). Those follow `Theme.iconColor`
  and scale as artwork; a symbol is text that happens to look like a picture.

**On #1537.** This section previously banned symbols outright and cited #1537 as the bug class. The
citation does not support the ban: #1537 dropped the "fi" ligature from "Profile", a word entirely
inside the bundled font, so whatever caused it, it was not a missing glyph and says nothing about
fallbacks. Nothing in this app has been traced to a missing glyph. Recorded so the ban is not
reinstated from memory.

Do not go further than that and state what #1537 *was*. `src/main.cpp` carries two candidate
explanations — a bundled-font family-name collision with a host copy of Roboto making family
lookup ambiguous, and distance-field re-caching during resize — and says in as many words that they
are not reconciled.

Two earlier drafts of this very paragraph got that wrong in two different ways: the first asserted
the distance-field theory as settled fact, and the second described the other hypothesis as a
"race", which is a mechanism main.cpp never mentions. Both are the same move that produced the
wrong citation this paragraph exists to correct. Read main.cpp before restating it.

**Emoji are a different case and are encouraged.** `☕`/`⚠️`/`🔒` never reach the text renderer: the app ships the complete Twemoji set and rewrites every emoji to a bundled `<img>`, so metrics are identical everywhere. Render them through `Theme.emojiToImage()` or `Theme.replaceEmojiWithImg()` — putting one in a plain `Text` lets a colour glyph reach the platform renderer and **crashes the render thread on macOS**. See "Using emoji well" in CLAUDE.md.

## Calling a Q_INVOKABLE in a binding — the binding never re-evaluates

A QML binding re-evaluates when a NOTIFY fires for a **property it read** during its last evaluation. Calling a `Q_INVOKABLE` registers no dependency, so the binding computes once and then freezes — while still returning the correct value if you call it directly, which is why this survives review and unit tests alike. It only shows up when the underlying state changes.

This has bitten this codebase repeatedly: `effectiveFontSizes` (converted to a property), `translate` (3,248 stale call sites — the language-switch bug; now a property holding a callable), and `canAutoTranslate` — which is **still a `Q_INVOKABLE`**, worked around at its call site rather than converted, so don't cite it as an example of the fix.

```qml
// BROKEN — computes once, never updates
text: TranslationManager.translate("k", "fallback")   // if translate() is a Q_INVOKABLE

// WORKS — expose the value as a property instead
Q_PROPERTY(QVariantMap effectiveFontSizes READ effectiveFontSizes NOTIFY customFontSizesChanged)

// WORKS — a property whose value is a CALLABLE keeps the call-site syntax identical
Q_PROPERTY(QJSValue translate READ translateFn NOTIFY translationsChanged)
```

The third form is how `TranslationManager` fixed 3,248 bindings without editing any of them: reading `TranslationManager.translate` is a property read, and the returned function is then invoked.

An invokable **can** work if the same expression also reads a notifying property (`Tr.qml` reads `translationVersion` first), but relying on that is a trap — a later edit removing the "unused" read silently breaks it.

**Non-reactive is sometimes correct.** `EmojiAssets.has()` is an invokable on purpose: the bundled asset set is fixed at build time, so there is nothing to re-evaluate for. Say so in a comment when you do this, or the next reader will assume it is the bug.

## Translucent element renders opaque (scene-graph opaque batch)

A `Rectangle` with a translucent color (e.g. a `Theme.scrimColor(...)` fill at alpha 0.4) can render **fully opaque** — the wallpaper behind it doesn't show through — even though the computed color is correct. Qt Quick's renderer mis-sorts it into the *opaque* batch and drops its alpha. This is platform-independent (seen on Metal/macOS, and reported on Android), so it is **not** an RHI-backend bug.

Symptom seen in practice: the in-page bottom bars and the compact preset-pill popups painted as solid colored slabs over a custom background image, while the top `StatusBar` (a sibling of the StackView pages, composited over the already-rendered page) and elements that overlap other content (the cards, the center preset pills) blended correctly. The exact scene-graph batch-sort trigger was **not** fully pinned down — treat the diagnosis as empirical: if a translucent surface renders opaque over the background, reach for the fix below rather than assuming a specific geometric cause. In particular, being flush against a window edge is *not* the deciding factor — `StatusBar` is edge-flush and blends fine.

What does **not** fix it: a translucent material color alone, or `layer.enabled` (its composite lands at the same spot and hits the same mis-sort).

What **does** fix it: give the item an `opacity < 1`, which inserts a `QSGOpacityNode` and forces the subtree through the alpha pass. The exact value only needs to be just under 1 — `0.99` is visually imperceptible but still trips the alpha pass; don't let a "cleanup" round it back to `1.0`.

```qml
// BAD - flush against the window edge / alone over the background => renders opaque
Rectangle {
    color: Theme.scrimColor(Theme.surfaceColor)   // alpha 0.4, but paints as 1.0
}

// GOOD - opacity node forces the alpha pass; scope it to when the scrim is active
Rectangle {
    color: Settings.theme.backgroundImagePath.length > 0
           ? Theme.scrimColor(Theme.surfaceColor)
           : Theme.surfaceColor
    opacity: Settings.theme.backgroundImagePath.length > 0 ? 0.99 : 1.0
}
```

## Measuring text in a binding — `FontMetrics.advanceWidth()`, never a mutated `TextMetrics`

To measure a string's width, the obvious reach is `TextMetrics`: set `.text`, read `.width`. Inside a **reactive binding** that is a self-triggering loop. Reading `.width` registers it as a dependency of the binding; assigning `.text` in the same binding invalidates `.width`; the invalidation re-runs the binding, which re-assigns `.text` — forever. Qt reports `WARN … Binding loop detected for property "<name>"`, and the property never settles. Sharing one `TextMetrics` across several bindings makes it worse: each binding's `.text` write re-triggers every other binding that reads `.width`.

Use `FontMetrics.advanceWidth(str)` instead — a pure function that reads only the (static) font and mutates nothing, so it registers no self-dependency.

```qml
// BROKEN — mutates .text and reads .width in the same binding => binding loop
TextMetrics { id: tm; font.pixelSize: Theme.scaled(16); font.bold: true }
readonly property var pageSizes: {
    var w = []
    for (var i = 0; i < items.length; ++i) {
        tm.text = items[i].name          // invalidates tm.width...
        w.push(tm.width + Theme.scaled(40))  // ...which this binding depends on => loop
    }
    return packPages(w, availWidth)
}

// WORKS — advanceWidth() is a pure call, no mutated-property dependency
FontMetrics { id: fm; font.pixelSize: Theme.scaled(16); font.bold: true }
readonly property var pageSizes: {
    var w = []
    for (var i = 0; i < items.length; ++i)
        w.push(fm.advanceWidth(items[i].name) + Theme.scaled(40))
    return packPages(w, availWidth)
}
```

The mutated-`TextMetrics` form is only safe when the measurement runs **imperatively** — inside a Timer/handler that writes a plain (non-`readonly`) property — not inside a binding. That is exactly what `PresetPillRow.measureTextWidth()` does (called from the timer-driven `calculateRows()`), which is why it never loops. Binding loops are **runtime-only**: a clean C++/qmlcache build will not catch them — check the running app's log (`debug_get_log`) after the change. (Bitten during `descriptive-recipe-names` computing idle pill-row page sizes; the shared `FontMetrics` is the font-mirroring measurement in `PillFit.js`'s callers.)


## Exposing C++ to QML: always compile-time, never `setContextProperty` or runtime `qmlRegister*`

**Rule: a C++ type or object that QML touches is registered by a macro in a header.** Not by
`context->setContextProperty("X", &x)`, and not by `qmlRegisterType<X>("Decenza", 1, 0, "X")` in
`main()`. Both are runtime-only, and *runtime-only means invisible* — to qmllint, to
`qmlcachegen`, and to the language server. A context property is worse than untyped: it is
**indistinguishable from a typo**, because nothing in the build can tell the two apart. #1661 is
what that costs.

| What you have | What to write |
|---|---|
| An object `main()` owns | `QML_FOREIGN` + `QML_SINGLETON` struct in `src/core/contextsingletons_qml.h`, published via `s_singletonInstance` |
| A class only the app compiles | `QML_ELEMENT` (+ `QML_SINGLETON` or `QML_UNCREATABLE`) directly in its header |
| A type QML instantiates | `QML_ELEMENT` in its header |
| Enums QML compares against | Nothing extra — a singleton exposes them as `Singleton.Enumerator` |

**When does a class need the `*Foreign` indirection rather than the macro in its own header?**
Answer it per target, not by inheritance. The rule is only: *does every target that compiles this
.cpp link `Qt6::Qml`?* Today the answer is almost always yes — `decenza_testlib` links `Qt6::Qml`
PUBLIC, so every `add_decenza_test()` binary gets it transitively, and the four targets that do
not (`profile_sync`, `shot_eval`, `saw_replay`, `saw_parity`) compile none of the wrapped classes.

`contextsingletons_qml.h` states the constraint as "test and tool targets link no Qt6::Qml", and as
written that is **no longer true** — `decenza_testlib` gained `Qt6::Qml` in #1617, before that
comment was written. Do not use it as the reason for reaching for `*Foreign` on a new class; check
the actual link line. (Whether the pattern still earns its keep for a *different* reason — keeping
`<QtQml/...>` out of widely-included class headers, so a registration change does not invalidate
every TU that includes them — is plausible and **unmeasured**. Do not assert it as fact either.)

Two mechanical traps when adding `QML_ELEMENT` to a header:

- **The header's directory must be in the `target_include_directories(Decenza ...)` block.** The
  generated registration file emits `#if __has_include(<bare-name.h>)`; an unreachable basename
  makes the include expand to nothing and the build fails with `use of undeclared identifier` in
  *generated* code, three tools from the cause. That block lists the directories and the
  duplicate-basename check to run first.
- **If the type inherits from another QML module, `qt_add_qml_module` must declare
  `DEPENDENCIES`.** The import path resolves what QML *imports*; it does not resolve what your
  registered types *inherit*. Without `DEPENDENCIES QtQuick3D`, `PipeCylinderGeometry`'s
  `QQuick3DGeometry` prototype is unlinkable and every use reports `used but it is not resolved` —
  with `Quick3D.qmltypes` shipped and already on the import path.
- **Migrating a `#ifdef`-guarded context property silently breaks every `typeof X !== "undefined"`
  guard around it.** This is the one migration hazard that does not announce itself: it compiles
  clean, the gate reports only improvements, and it breaks at runtime on the platform you did not
  build. A context property that `main()` never publishes is genuinely absent, so `typeof` returns
  `"undefined"` and the guard holds. Register the same name as a `QML_SINGLETON` and **the type
  always exists** — on a platform where `create()` returns null, `typeof X` is `"object"`, the
  guard passes, and the next member access dereferences null. `USBManager` and `UsbScaleManager`
  hit exactly this: three bindings in `SettingsConnectionsTab.qml` would have thrown on iOS, and
  a `visible:` gate does not save you, because an invisible element's bindings still evaluate.
  - Fix: guard on the **condition**, not on the name's existence — the file's own
    `readonly property bool usbAvailable: Qt.platform.os !== "ios"`, or a plain truthiness test
    (`X ? X.member : ...`), both of which are correct for a null singleton *and* an absent one.
  - Use `decenzaOptionalSingleton()` (not `decenzaPublishedSingleton()`) for a name whose instance
    legitimately does not exist on some builds; the loud helper would `qCritical` on every launch
    of the platform that is behaving correctly.
  - Grep for `typeof <Name>` before migrating. If there are none, check anyway for
    `Qt.platform.os` tests near the uses — those are the same guard written a different way, and
    they are the ones that stay correct.

## Reading the qmllint gate — four things that will save you a day

`python3 scripts/qmllint_report.py --check` (add `--qmllint <patched>` locally; CI runs stock with
`--skip-unlintable`). `--report` prints the breakdown; `--update-baseline` records it.

1. **A count going UP after a fix is usually the fix working.** Better type resolution reaches
   expressions qmllint previously abandoned, so it finds more. Three recorded instances, each of
   which looked like a regression: the `MainController` migration (`unresolved-type` 2 -> 763),
   the #1680 stale-baseline correction (three files' `unqualified` rose), and the `CupFillView`
   case below (`missing-property` 322 -> 388). Diff the per-file and per-category sets before
   concluding anything — totals alone will mislead you.
2. **An unresolvable type hides every defect behind it.** Fixing `JsCanvasPainterItem`'s
   registration surfaced 66 warnings in `CupFillView.qml` that had never been reachable: the
   `paint()` signal declared `QObject *ctx` while emitting a `JsCanvasContext*`, and the gradient
   factories returned `QObject*` instead of `JsCanvasGradient*`. qmllint was right and useless —
   `QObject` really has no `beginPath`. **When you see many `Member "x" not found on type
   "QObject"`, suspect an erased pointer type in C++, not a mistake in the QML.**
3. **"qmllint cannot do X" is usually a missing declaration, not a tool limitation.** The Quick3D
   case above was measured, accepted as a property of the linter, and was actually one CMake
   keyword. Before writing off a diagnostic class, ask what the module has failed to declare.
4. **The gate only ratchets down, so a too-low number is invisible to it.** Nothing asks whether a
   recorded ceiling is *achievable*. A baseline measured against a stale build under-reports,
   which looks exactly like an improvement — that is how #1680 shipped three ceilings the tree
   could not meet. `check_registry_fresh()` and `--allow-ceiling-rise` exist because of it; do not
   route around either.

## Never directory-import a type the module already provides

`qt_add_qml_module` registers every file in `QML_FILES` as a type of the `Decenza` module,
singletons included. So `import Decenza` is all any file needs, and an extra directory import is
not merely redundant — it **shadows the registration**:

```qml
import Decenza
import "../components"   // WRONG: re-resolves the same files as plain component types
```

A `pragma Singleton` .qml resolved through a directory import is a *component*, not the singleton
instance, so its members are simply absent. That is why `DrinkType.shortLabel`,
`DrinkType.icon`, `DrinkType.fromRecipeMap` and `SettingsTabs.indexOf` all reported as missing
members while being plainly declared in their own files. `Theme` never showed it only because it
lives in `qml/` and nothing directory-imports that.

106 such imports were deleted across 98 files; it cleared the entire `import` category and 32
`missing-property` findings. Keep `import "..." as Namespace` (used as a real namespace) and `.js`
imports. Add nothing else.

## `pragma ComponentBehavior: Bound` — and the delegate it will break

Ids from an enclosing file are not statically resolvable inside a nested component (a delegate, a
`layer.effect`, an inline `Component`). `pragma ComponentBehavior: Bound` makes them resolvable —
this is Qt's own prescription, see qtdeclarative's "Exposing State from C++ to QML".

**It also stops delegates receiving injected model roles.** A delegate that uses `modelData`,
`model` or `index` without declaring them breaks **at runtime, with nothing at build time to say
so**. So:

1. `grep -c 'Repeater\|delegate:\|model:'` the file first. **No delegates → the pragma alone is
   safe**, and that covers most component files (their warnings are usually
   `layer.effect: MultiEffect { colorizationColor: root.x }`).
2. With delegates, add `required property var modelData` / `required property int index` to each
   delegate root in the same edit, and give it an `id` so nested children can qualify against it.
3. Re-lint. Any remaining `modelData`/`index` warning is a delegate you missed — the linter will
   name it, and that check is why this is safe to do at scale.

## Qualify by qmllint's line and column, never by text search

The same identifier can be several different things in one file, and only the flagged occurrences
should move:

- `ProfileEditorPage.qml` has a function-local `var step` **and** `stepEditorScroll`'s
  `property var step`. Only the latter is flagged; a `sed` on `step` would have rewritten the
  local too.
- `ValueInput.qml` has `gear` on a delegate. A bulk pass prefixed it with the component root,
  which has no such member — every gear tap would have assigned `undefined`. It survived exactly
  one relint (`Member "gear" not found on type "ValueInput"`), which is the argument for doing
  this work with the linter rather than around it.

Recover the identifier from the source line at the reported column, assert the token before
rewriting it, and re-lint after every pass.

## `Window` is an attached property — do not write it through an id

```qml
var win = root.Window.window     // works at runtime, uncheckable
readonly property var appWindow: Window.window   // read where it actually attaches
```

`Window` resolves against the *current scope*; it is not a member of the object whose id you
write it on. Reading it once at the item root is both checkable and removes the duplicate
lookups. Same for any attached type.

## A runtime-swapped object becomes a proxy singleton, not a context property

`ScaleDevice` is re-pointed at a FlowScale, a BLE scale, a USB scale or a simulated scale as
hardware comes and goes — eleven sites in `main.cpp`. A context property can be re-pointed; a
`QML_SINGLETON` is created once and cannot be. So the singleton is a **proxy** and the proxy is
what moves: `ScaleDeviceProxy` (and `RefractometerProxy`) hold the target in a `QPointer`, mirror
every property, forward every public slot and re-emit every signal. QML says `ScaleDevice.connected`
exactly as before.

Three rules when writing one:

- Forward **every** public slot, not the ones QML calls today. A context property exposed the
  whole set; forwarding a subset silently removes the rest from QML's reach and the calls still
  parse.
- Properties that are `CONSTANT` on the target are **not** constant on the proxy — `name`,
  `isFlowScale` and `isSimulated` are facts about *which device is attached*, which is exactly
  what the proxy changes.
- The proxy is never null, so guards that test the OBJECT (`ScaleDevice !== null`,
  `typeof ScaleDevice !== "undefined"`) stop guarding anything. Test the STATE:
  `ScaleDevice.connected`.

"Forward every public slot" is the rule and it was broken on its first outing: `ScaleDeviceProxy`
shipped with 9 of `ScaleDevice`'s 12 forwarded, and nothing caught it because the three omitted
are called only from C++. Diff the two `public slots:` blocks when you write one.

## A registered singleton with no instance is TRUTHY, not `undefined`

This is the trap behind `decenzaOptionalSingleton()`, and the guard everyone writes is wrong:

```qml
if (typeof GHCSimulator !== "undefined" && GHCSimulator) GHCSimulator.doThing()  // passes, THROWS
if (GHCSimulator.doThing !== undefined) GHCSimulator.doThing()                   // correct
```

A singleton whose **type** is registered but whose **instance** was never published does not make
the name read as `undefined`. Per Qt 6.11.1: `qv4qmlcontext.cpp:229` resolves the name with
`QQmlTypeWrapper::create(v4, nullptr, r.type)` — it calls `singletonInstance<QObject*>()` and
**discards the result**, so the wrapper is built either way — and `QQmlTypeWrapper` has no
`virtualToBoolean` override, so that wrapper is a truthy `Object` with `typeof === "object"`. Only
the *member read* degrades: `qqmltypewrapper.cpp:319` fails its `if (QObject *singleton = ...)` and
falls through to `Object::virtualGet`, yielding `undefined`.

So the name passes both halves of the usual guard and the first method call throws
`TypeError: Property '...' of object [object Object] is not a function`.

**Guard the member you are about to use, or use a platform check** (`Qt.platform.os !== "ios"`),
which short-circuits before the member read — that is why `USBManager`'s call sites were never
affected while `GHCSimulator`'s were. `GHCSimulator` is registered wherever `DECENZA_SIMULATOR` is
defined (every desktop config) but instanced only on a **debug** Windows/macOS build, so the broken
guard passed — and threw on every window activation — on Linux, on Release desktop and on mobile
Debug, while working on exactly the two configurations it gets tested on.
