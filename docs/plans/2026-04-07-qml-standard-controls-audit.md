# QML Standard Controls Audit

Audit of QML pages and components for UI elements that could be replaced by standard Qt Quick Controls 2 elements for better accessibility, maintainability, and reduced boilerplate.

**PR**: Kulitorum/Decenza#681 — dead component removal + ActionButton TapHandler refactor

---

## High Priority

### ~~SuggestionField → ComboBox (editable)~~ — WITHDRAWN
- **File**: `qml/components/SuggestionField.qml`
- **Verdict**: Keep as-is. The component intentionally uses a dual-mode design (inline Popup for normal use, Dialog for accessibility mode) because TalkBack cannot trap focus inside Qt Popup — the same reason StyledComboBox suppresses its native popup. A ComboBox replacement would require the same dual-mode split and custom filtering logic, with no net reduction in code or complexity. Used in 21 places; rewrite risk outweighs any benefit.

---

## Medium Priority

### ~~TouchSlider → SpinBox~~ — REMOVED
- **File**: `qml/components/TouchSlider.qml`
- **Verdict**: Component was dead code (zero usages). Deleted outright.

### ~~ValueInput → SpinBox~~ — WITHDRAWN
- **File**: `qml/components/ValueInput.qml`
- **Verdict**: ValueInput is far richer than SpinBox — drag-to-scrub, multi-gear (10x/100x/fine), speech-bubble overlay, and a full-screen scrubber popup. Replacing with SpinBox would be a regression on mobile. Keep as-is.
- **Improvement to add**: **Double-tap inline editing** — double-tap (or Enter key) activates an inline `TextInput` replacing the value display, so desktop/keyboard users can type a known value directly without opening the popup. The `TextInput` needs a `validator` to clamp to `[from, to]`; Escape cancels, Enter/Return commits. Low priority since the popup already works.

### ~~StepSlider — remove wrapper~~ — REMOVED
- **File**: `qml/components/StepSlider.qml`
- **Verdict**: Component was dead code (zero usages). Deleted outright. Note: the plan entry was wrong — `snapMode: Slider.SnapAlways` does not replicate the tap-to-step behavior; `Slider` still jumps to click position on track taps. The custom MouseArea was intentional, not redundant.

### ~~RatingInput — replace Rectangle pills with Button~~ — WITHDRAWN
- **File**: `qml/components/RatingInput.qml`
- **Verdict**: Pills already have correct accessibility wiring. Custom `ratingColor()` per-pill coloring requires a fully custom `background:` on Button anyway, so the Rectangle+MouseArea approach is justified. Net gain is minimal for 2 usages. Keep as-is.

### ~~SelectionDialog → RadioButton delegates~~ — WITHDRAWN
- **File**: `qml/components/SelectionDialog.qml`
- **Verdict**: Used in 8 places. Mutual exclusion is already correct (controlled externally via `currentIndex`/`currentValue`). Delegates already use `AccessibleMouseArea` (project TalkBack pattern). RadioButton would still need a fully custom `background:` to match the visual, and a Column can't replicate the ListView scrolling + fade effects. No net gain. Keep as-is.

### ~~ExtractionViewSelector → RadioButton delegates~~ — WITHDRAWN
- **File**: `qml/components/ExtractionViewSelector.qml`
- **Verdict**: Option cards have icon + title + description + custom border highlight — RadioButton still needs a fully custom `background:` anyway. Already uses `AccessibleMouseArea` throughout. No net gain. Keep as-is.
- **Improvement to add**: The 3 toggle rows (phase indicator, stats, advanced curves) are each a manually-built checkbox (~40 lines each, duplicated 3×). Replace each with `StyledSwitch` or Qt `CheckBox` to remove the custom Rectangle + tick Image + MultiEffect + AccessibleMouseArea boilerplate.

### ~~ExpandableTextArea → TextArea readOnly toggle~~ — WITHDRAWN
- **File**: `qml/components/ExpandableTextArea.qml`
- **Verdict**: Already implemented as described — `TextArea` with `readOnly` + expanded `Dialog`. The separate display `Text` element exists intentionally for `Text.RichText` clickable URL rendering, which `TextArea` does not support. The element swap is load-bearing. Keep as-is.

### ~~ActionButton — use TapHandler / HoverHandler~~ — DONE
- **File**: `qml/components/ActionButton.qml`
- **Result**: Replaced ~90-line MouseArea (with 2 manual Timers, `_longPressTriggered` guard, `_lastTapTime` tracking, bounds check) with ~45-line `TapHandler`. Long-press uses `longPressThreshold`, double-tap uses built-in `onSingleTapped`/`onDoubleTapped`, bounds checking and long-press guard handled natively. `cursorShape` set directly on TapHandler, no separate HoverHandler needed.

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
