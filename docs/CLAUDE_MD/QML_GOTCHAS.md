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

## Numeric defaults with `??` not `||`

JavaScript `||` treats `0` as falsy, so `value || 0.6` returns `0.6` when `value` is `0`. Use `value ?? 0.6` (nullish coalescing) which only falls back for `null`/`undefined`. Only use `||` when `0` genuinely means "no data" (e.g., unrated enjoyment, unmeasured TDS).

## `native` is reserved

`native` is a reserved JavaScript keyword — use `nativeName` instead.

## No Unicode symbols as icons

Never use Unicode symbols as icons in text (e.g., `"✎"`, `"✗"`, `"☰"`). These render as tofu squares on devices without the right font glyphs. Use SVG icons from `qrc:/icons/` with `Image` instead. For buttons/menu items, use a `Row { Image {} Text {} }` contentItem. Safe Unicode characters (°, ·, —, →, ×) that are in standard fonts are OK.
