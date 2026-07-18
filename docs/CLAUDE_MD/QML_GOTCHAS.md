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

On Android/iOS virtual keyboards, the last typed word is held in a composing/pre-edit state and is NOT reflected in `TextField.text` until committed. When a button's `onClicked` reads a text field's `.text` directly (to send, save, or pass to a C++ method), always call `Qt.inputMethod.commit()` first — otherwise the last word is silently dropped. This is a no-op on desktop so it is safe to always include.

```qml
// BAD - last word may be missing on mobile
onClicked: {
    doSomething(myField.text)
}

// GOOD - commit pending IME composition first
onClicked: {
    Qt.inputMethod.commit()
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

## No Unicode symbols as icons (but emoji are fine)

Never use non-emoji Unicode symbols as icons in text (`"✎"`, `"✗"`, `"☰"`, `"▶"`). They render as font glyphs, are absent from Decenza Sans's cmap, and fall back to a platform font whose metrics vary per machine — that is the class of bug behind #1537. Use SVG icons from `qrc:/icons/` with `Image`; for buttons/menu items use a `Row { Image {} Text {} }` contentItem.

Safe literals (present in the bundled font): `°`, `·`, `—`, `×`, `•`, `…`.

**`→` is NOT safe** — an earlier version of this section listed it as safe, which was wrong. Arrows (`→ ← ↔ ↗ ⇒`) are absent from the bundled font.

**Emoji are a different case and are encouraged.** `☕`/`⚠️`/`🔒` never reach the text renderer: the app ships the complete Twemoji set and rewrites every emoji to a bundled `<img>`, so metrics are identical everywhere. Render them through `Theme.emojiToImage()` or `Theme.replaceEmojiWithImg()` — putting one in a plain `Text` lets a colour glyph reach the platform renderer and **crashes the render thread on macOS**. See "Using emoji well" in CLAUDE.md.

## Calling a Q_INVOKABLE in a binding — the binding never re-evaluates

A QML binding re-evaluates when a NOTIFY fires for a **property it read** during its last evaluation. Calling a `Q_INVOKABLE` registers no dependency, so the binding computes once and then freezes — while still returning the correct value if you call it directly, which is why this survives review and unit tests alike. It only shows up when the underlying state changes.

This has bitten this codebase four times: `effectiveFontSizes`, `canAutoTranslate`, `translate` (3,248 stale call sites — the language-switch bug), and the emoji resolver design before it was made synchronous.

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
