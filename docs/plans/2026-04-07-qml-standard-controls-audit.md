# QML Standard Controls Audit

Audit of QML pages and components for UI elements that could be replaced by standard Qt Quick Controls 2 elements for better accessibility, maintainability, and reduced boilerplate.

---

## High Priority

### ~~SuggestionField → ComboBox (editable)~~ — WITHDRAWN
- **File**: `qml/components/SuggestionField.qml`
- **Verdict**: Keep as-is. The component intentionally uses a dual-mode design (inline Popup for normal use, Dialog for accessibility mode) because TalkBack cannot trap focus inside Qt Popup — the same reason StyledComboBox suppresses its native popup. A ComboBox replacement would require the same dual-mode split and custom filtering logic, with no net reduction in code or complexity. Used in 21 places; rewrite risk outweighs any benefit.

---

## Medium Priority

### TouchSlider → SpinBox
- **File**: `qml/components/TouchSlider.qml`
- **Current**: RowLayout + Rectangle +/- buttons + Slider
- **Replace with**: `SpinBox` (or Slider + `Button`)
- **Why**: SpinBox handles ±increment natively; removes custom button-press logic.

### ValueInput → SpinBox
- **File**: `qml/components/ValueInput.qml`
- **Current**: Item with Rectangle +/- buttons + display Text + scrubber dialog
- **Replace with**: `SpinBox` with custom overlay for scrub mode
- **Why**: Shares nearly identical logic with TouchSlider. Consolidating on SpinBox removes two custom implementations.

### StepSlider — remove wrapper
- **File**: `qml/components/StepSlider.qml`
- **Current**: Item wrapping `Slider` + MouseArea for step-clicks
- **Replace with**: `Slider` with `snapMode: Slider.SnapAlways` directly
- **Why**: Removes unnecessary wrapper; built-in snapping covers the use case.

### RatingInput — replace Rectangle pills with Button
- **File**: `qml/components/RatingInput.qml`
- **Current**: FocusScope + Grid of Rectangle preset pills + Slider
- **Replace with**: `Slider` + `Button` row for preset pills
- **Why**: Preset pills are interactive; Button gives accessibility, keyboard nav, and enabled/checked states for free.

### SelectionDialog → RadioButton delegates
- **File**: `qml/components/SelectionDialog.qml`
- **Current**: Dialog + ListView + custom highlight logic for single selection
- **Replace with**: Dialog + `RadioButton` column + `ButtonGroup`
- **Why**: RadioButton is semantic for exclusive selection; ButtonGroup handles mutual exclusion with no custom state.

### ExtractionViewSelector → RadioButton delegates
- **File**: `qml/components/ExtractionViewSelector.qml` *(or similar)*
- **Current**: Dialog + Repeater of Rectangle option cards with custom selection state
- **Replace with**: Dialog + `RadioButton` group
- **Why**: Same as SelectionDialog — exclusive mode selection maps directly to RadioButton.

### ExpandableTextArea → TextArea readOnly toggle
- **File**: `qml/components/ExpandableTextArea.qml`
- **Current**: Rectangle + Text (display mode) + TextArea (edit mode) swapped via visibility
- **Replace with**: Single `TextArea` toggling `readOnly` + Dialog for expanded editing
- **Why**: Removes element-swap logic; `readOnly` toggling is the idiomatic Qt pattern.

### ActionButton — use TapHandler / HoverHandler
- **File**: `qml/components/ActionButton.qml`
- **Current**: Button + custom MouseArea for long-press and double-tap detection
- **Replace with**: `Button` + Qt 6 `TapHandler` / `HoverHandler`
- **Why**: Qt 6 gesture handlers have better separation of concerns and handle multi-touch edge cases.

### HideKeyboardButton → RoundButton / ToolButton
- **File**: `qml/components/HideKeyboardButton.qml`
- **Current**: Rectangle + Image + AccessibleMouseArea
- **Replace with**: `RoundButton` or `ToolButton`
- **Why**: Standard control with built-in accessibility; removes boilerplate.

### ProfileInfoButton → RoundButton / ToolButton
- **File**: `qml/components/ProfileInfoButton.qml`
- **Current**: Item wrapping Rectangle + MouseArea
- **Replace with**: `RoundButton` or `ToolButton`
- **Why**: Same as HideKeyboardButton.

---

## Low Priority (Style-only wrappers)

### StyledSwitch — reduce custom indicator
- **File**: `qml/components/StyledSwitch.qml`
- **Current**: Switch + custom animated Rectangle thumb
- **Simplify**: Use `Switch` with `indicator` style override rather than reimplementing animation
- **Why**: Style-only concern; built-in indicator supports custom geometry.

### StyledTabButton — simplify background
- **File**: `qml/components/StyledTabButton.qml`
- **Current**: TabButton + custom background Rectangle with top-rounded corners
- **Simplify**: Use `TabButton` with `background` override only
- **Why**: Pure style concern; no logic change needed.

### StyledIconButton — use icon.source
- **File**: `qml/components/StyledIconButton.qml`
- **Current**: RoundButton + custom contentItem to handle both text icons and image icons
- **Simplify**: Use `RoundButton` with `icon.source` / `icon.color`
- **Why**: Qt's built-in icon support covers this; only MultiEffect colorization needs to stay custom.

### PresetButton — use checked property
- **File**: `qml/components/PresetButton.qml`
- **Current**: Button with custom `selected` boolean for state styling
- **Simplify**: Replace `selected` with standard `checked` property
- **Why**: Semantic correctness; `ButtonGroup` can then manage exclusivity automatically.

---

## Keep As-Is (Intentional Custom Patterns)

| Component | Reason |
|-----------|--------|
| `AccessibleButton.qml` | Announce-first TalkBack pattern; documents project convention |
| `AccessibleMouseArea.qml` | Purpose-built accessibility layer; not in stdlib |
| `AccessibleTapHandler.qml` | Same — TalkBack focus announcement logic |
| `StyledTextField.qml` | Intentionally suppresses Material floating label (known Qt issue) |
| `StyledComboBox.qml` | Replaces popup with Dialog intentionally — TalkBack cannot trap focus inside Qt Popup |

---

## Notes

- **SpinBox consolidation**: TouchSlider and ValueInput share nearly identical ±button logic. Migrate both together and share a common SpinBox style.
- **Rectangle → Button migrations**: Each raw `Rectangle + MouseArea` interactive element gains accessibility, keyboard navigation, Material ripple, and `enabled`/`checked` states for free with zero extra effort.
- **RadioButton for selection dialogs**: Use `ButtonGroup` for automatic mutual exclusion — no custom `currentIndex` state needed.
- **Do not change**: AccessibleMouseArea/AccessibleButton patterns are intentional project conventions for TalkBack support and should not be replaced.
