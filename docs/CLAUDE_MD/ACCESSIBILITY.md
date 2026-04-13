# Accessibility (TalkBack / VoiceOver)

## Current Implementation

The app has accessibility support via `AccessibilityManager` (C++) with:
- Text-to-speech announcements via `AccessibilityManager.announce()`
- Tick sounds for frame changes
- `AccessibleTapHandler` and `AccessibleMouseArea` for touch handling
- Extraction announcements (phase changes, weight milestones, periodic updates)
- User-configurable settings in Settings → Accessibility

### Key QML Components

| Component | Purpose |
|-----------|---------|
| `qml/components/AccessibleButton.qml` | Standard button with required `accessibleName` |
| `qml/components/AccessibleMouseArea.qml` | Custom-styled button with announce-first TalkBack behavior |
| `qml/components/AccessibleTapHandler.qml` | TapHandler variant that works with TalkBack |
| `qml/components/AccessibleLabel.qml` | Tap-to-announce text label |

### Key C++ Component

`src/core/accessibilitymanager.h/cpp` — TTS engine, tick sounds, settings persistence.

---

## Rule 1 — Every Interactive Element Must Be Discoverable

Every interactive element must have `Accessible.role`, `Accessible.name`, and `Accessible.focusable: true`. Without these, TalkBack/VoiceOver cannot discover or activate the element.

| Element | Preferred | If raw, must set |
|---------|-----------|-----------------|
| Button (Rectangle+MouseArea) | `AccessibleButton` or `AccessibleMouseArea` | `Accessible.role: Accessible.Button` + `Accessible.name` + `Accessible.focusable: true` + `Accessible.onPressAction` |
| Text input | `StyledTextField` | `Accessible.role: Accessible.EditableText` + `Accessible.name` + `Accessible.description: text` + `Accessible.focusable: true` |
| Autocomplete field | `SuggestionField` | (same as text input) |
| Checkbox | Qt `CheckBox` | `Accessible.name` + `Accessible.checked: checked` + `Accessible.focusable: true` |
| Dropdown | `StyledComboBox` | `Accessible.role: Accessible.ComboBox` + `Accessible.name` (use label, not displayText) + `Accessible.focusable: true` |
| List delegate | — | `Accessible.role: Accessible.Button` + `Accessible.name` (summarize row content) + `Accessible.focusable: true` + `Accessible.onPressAction` |

```qml
// BAD - TalkBack can't see this button
Rectangle {
    MouseArea { onClicked: doSomething() }
}

// GOOD - use AccessibleButton (preferred for standard buttons)
AccessibleButton {
    text: "Save"
    accessibleName: "Save changes"
    onClicked: doSomething()
}

// GOOD - use AccessibleMouseArea (for custom-styled buttons, provides announce-first TalkBack behavior)
Rectangle {
    id: myButton
    color: Theme.primaryColor
    Accessible.ignored: true
    Text { text: "Save"; Accessible.ignored: true }
    AccessibleMouseArea {
        anchors.fill: parent
        accessibleName: "Save changes"
        accessibleItem: myButton
        onAccessibleClicked: doSomething()
    }
}

// OK - add accessibility to Rectangle manually (last resort, loses announce-first behavior)
Rectangle {
    Accessible.role: Accessible.Button
    Accessible.name: "Save changes"
    Accessible.focusable: true
    Accessible.onPressAction: area.clicked(null)
    MouseArea { id: area; onClicked: doSomething() }
}
```

---

## Rule 2 — Secondary Actions Must Be Announced

Any interactive element with secondary actions (long-press, double-tap) **must** set `Accessible.description` (or `accessibleDescription` on `AccessibleTapHandler`) describing those actions. TalkBack/VoiceOver reads this as a hint after the element name. Without it, blind users cannot discover secondary workflows.

Format: `"Double-tap or long-press to <action>."` Where to set it depends on which component you're using:

| Component | How to set hint |
|-----------|----------------|
| `AccessibleTapHandler` | `accessibleDescription` property (built into the component) |
| `ActionButton` | `Accessible.description` on the button itself |
| Raw `Rectangle` + `MouseArea` | `Accessible.description` on the Rectangle |
| `AccessibleMouseArea` | `Accessible.description` on the **parent Rectangle** — `AccessibleMouseArea` does not expose an `accessibleDescription` property |

---

## Rule 3 — Every Page Must Have a Tab Chain (Focus Order)

Screen reader users navigate by swiping, but keyboard users (and some switch-access users) rely on `Tab`/`Shift+Tab`. Both require a logical, complete focus chain.

**Every page must:**

1. Set `activeFocusOnTab: true` on every focusable control (alongside `Accessible.focusable: true`).
2. Wire `KeyNavigation.tab` and `KeyNavigation.backtab` on all interactive controls in logical reading order, forming a **closed loop** (last element tabs back to first).
3. Set initial focus when the page/dialog becomes visible:
   - **Pages** (pushed onto `pageStack`): `Component.onCompleted: firstControl.forceActiveFocus()`
   - **Dialogs/Popups**: `onOpened: firstControl.forceActiveFocus()` — `Component.onCompleted` fires before the dialog is open and is unreliable for focus.
4. Wrap related groups of controls (e.g. a dialog's action buttons) in a `FocusScope`.

```qml
// Full pattern for a page
Item {
    Component.onCompleted: firstField.forceActiveFocus()

    StyledTextField {
        id: firstField
        activeFocusOnTab: true
        Accessible.focusable: true
        KeyNavigation.tab: secondField
        KeyNavigation.backtab: saveButton   // wraps around from last
    }

    StyledTextField {
        id: secondField
        activeFocusOnTab: true
        Accessible.focusable: true
        KeyNavigation.tab: saveButton
        KeyNavigation.backtab: firstField
    }

    AccessibleButton {
        id: saveButton
        activeFocusOnTab: true
        KeyNavigation.tab: firstField       // wraps back to start
        KeyNavigation.backtab: secondField
        onClicked: save()
    }
}

// Group related controls with FocusScope
FocusScope {
    AccessibleButton { id: okButton;     KeyNavigation.tab: cancelButton }
    AccessibleButton { id: cancelButton; KeyNavigation.tab: okButton }
}

// Repeater-based rows (e.g. preset pills) — KeyNavigation.tab can't reference
// dynamic delegates statically. Instead:
// 1. Expose focusTarget on the delegate Item
// 2. Use Keys.onTabPressed with forceActiveFocus()
// 3. Add Keys.onLeftPressed/RightPressed for arrow navigation within the row
Repeater {
    id: myRepeater
    model: myModel

    Item {
        property Item focusTarget: myPill   // expose for external callers

        Rectangle {
            id: myPill
            activeFocusOnTab: true
            Accessible.role: Accessible.Button
            Accessible.name: modelData.label
            Accessible.focusable: true
            Accessible.onPressAction: selectItem(index)

            // Non-Button Rectangle: must handle Enter/Space manually
            Keys.onReturnPressed: { selectItem(index); event.accepted = true }
            Keys.onSpacePressed:  { selectItem(index); event.accepted = true }
            Keys.onLeftPressed: {
                if (index > 0) myRepeater.itemAt(index - 1).focusTarget.forceActiveFocus()
                event.accepted = true
            }
            Keys.onRightPressed: {
                if (index < myRepeater.count - 1) myRepeater.itemAt(index + 1).focusTarget.forceActiveFocus()
                event.accepted = true
            }
            Keys.onTabPressed: {
                if (index < myRepeater.count - 1)
                    myRepeater.itemAt(index + 1).focusTarget.forceActiveFocus()
                else
                    nextControlAfterRow.forceActiveFocus()
                event.accepted = true
            }
            Keys.onBacktabPressed: {
                if (index > 0)
                    myRepeater.itemAt(index - 1).focusTarget.forceActiveFocus()
                else
                    prevControlBeforeRow.forceActiveFocus()
                event.accepted = true
            }
        }
    }
}
```

**Canonical references**: `qml/pages/settings/SettingsCalibrationTab.qml` lines 710–783 (Dialog — uses `onOpened: heaterIdleTempSlider.forceActiveFocus()`, not `Component.onCompleted`). For Repeater-based pill rows, see `qml/pages/FlushPage.qml` (settings preset section).

**Status**: Focus chains are missing from most pages. Work is tracked in
[Kulitorum/Decenza#736](https://github.com/Kulitorum/Decenza/issues/736).

> **Note on priority**: Decenza runs primarily on an Android tablet where users navigate by touch and TalkBack swipe. `KeyNavigation` and `activeFocusOnTab` have no effect on TalkBack — they only benefit desktop or physical-keyboard users. Keyboard chains are correct to add when already touching a page, but they are **low priority** compared to screen reader (Pass 1) work, which directly affects the tablet user base. See the [implementation plan](#screen-reader-audit-implementation-plan) below.

---

## Anti-Patterns (Do NOT Use)

### 1. Parent-ignored / child-accessible
Never set `Accessible.ignored: true` on a parent and put `Accessible.role` on a child occupying the same bounds. TalkBack can't reliably route activation to the child. Put accessibility properties on the interactive element itself.

**Exception**: `AccessibleMouseArea` with `accessibleItem` is designed for this pattern — the parent Rectangle has `Accessible.ignored: true` and `AccessibleMouseArea` carries the accessibility properties. This is the established pattern for custom-styled buttons throughout the codebase.

### 2. Popup for selection lists
Never use `Popup` for lists users must navigate. TalkBack can't trap focus inside Qt `Popup` elements. Use `Dialog { modal: true }` with `AccessibleButton` delegates instead.

### 3. Overlapping accessible elements
Never position accessible buttons inside another accessible element's bounds (e.g., buttons inside a TextField's padding area). TalkBack will only discover one element. Use conditional layout to show buttons in separate bounds when accessibility is enabled. See `ShotHistoryPage.qml` for a real example: it checks `AccessibilityManager.enabled` to conditionally reposition elements that would otherwise overlap.

---

## Common Mistakes Checklist

- **Rectangle+MouseArea without accessibility** — TalkBack cannot see it. Use `AccessibleButton`, `AccessibleMouseArea`, or add all four properties (`role`, `name`, `focusable`, `onPressAction`).
- **Accessibility on raw MouseArea instead of Rectangle** — Never put `Accessible.role`/`name`/`focusable` on a raw `MouseArea` child — put them on the parent Rectangle. `AccessibleMouseArea` is the exception.
- **Missing `Accessible.onPressAction`** — Every raw Rectangle+MouseArea button **must** have `Accessible.onPressAction: mouseAreaId.clicked(null)` (or `.tapped()` for TapHandler). Without it, TalkBack/VoiceOver double-tap does nothing. Not needed for `AccessibleMouseArea` or `AccessibleButton`.
- **Child Text inside accessible button missing `Accessible.ignored: true`** — TalkBack announces button name AND text content, doubling the announcement.
- **Text input missing `Accessible.description: text`** — Field sounds "Empty" even when it contains text. `StyledTextField` and `SuggestionField` set this automatically. Note: `Accessible.value` does not exist in Qt QML — use `Accessible.description` instead.
- **ComboBox `Accessible.name` set to `displayText`** — Announces the selected value instead of the field label. Override with the label text.
- **List row with no accessibility** — Only child elements (e.g. CheckBox) are discoverable; the row itself and its primary action are invisible.
- **Decorative text without `Accessible.ignored: true`** — When a list delegate summarizes its content in `Accessible.name`, all child Text elements must set `Accessible.ignored: true`. Same applies to icon/label text inside buttons that already have `Accessible.name`.
- **Multi-action element missing `Accessible.description` hint** — Secondary actions (long-press, double-tap) are invisible to screen reader users without a hint.
- **`AccessibleTapHandler` with `accessibleItem`: child Text still needs `Accessible.ignored: true`** — When a Rectangle uses `AccessibleTapHandler` with `accessibleItem: myRect`, the TapHandler carries the accessible name on behalf of the Rectangle. Any `Text` children of that Rectangle still need `Accessible.ignored: true` — TalkBack will find both the TapHandler's name and the Text node and announce both. This applies even though the Rectangle itself has no `Accessible.name` property set directly.

**Keyboard navigation (desktop/physical keyboard only — low priority for this tablet app):**
- **Missing `activeFocusOnTab: true`** — Control is unreachable by keyboard Tab navigation even if it has `Accessible.focusable: true`. Both must be set together. Has no effect on TalkBack swipe navigation.
- **Raw Rectangle with `activeFocusOnTab` missing `Keys.onReturnPressed/SpacePressed`** — `activeFocusOnTab` gets the element focused, but a raw Rectangle won't activate on Enter/Space without explicit key handlers. Qt's `Button`-based components (`AccessibleButton`, `ActionButton`) handle this internally; raw Rectangles do not.
- **No `KeyNavigation` chain** — Focus jumps unpredictably or gets stuck for keyboard users. Has no effect on TalkBack swipe navigation.
- **`KeyNavigation.tab` on a Repeater delegate** — `KeyNavigation.tab` requires a static Item reference and cannot point to dynamically-created delegate instances. Use the `focusTarget` pattern instead (see below).
- **No initial focus** — First focusable control on a page should receive focus via `Component.onCompleted: firstControl.forceActiveFocus()`.

---

## Rules for New Components

1. Every interactive element must have `Accessible.role`, `Accessible.name`, `Accessible.focusable: true`, and `Accessible.onPressAction` **on itself** (not on a child). Exception: `AccessibleMouseArea` with `accessibleItem`.
2. Every interactive element with secondary actions must set `Accessible.description` describing those actions.
3. Never use `Popup` for selection lists — use `Dialog` with `AccessibleButton` delegates.
4. Never overlap accessible elements — separate bounds or check `AccessibilityManager.enabled` to conditionally reposition elements (see `ShotHistoryPage.qml` for an example).
5. Test with TalkBack: double-tap to activate, swipe to navigate.
6. *(Low priority for this tablet app)* Every focusable control should also set `activeFocusOnTab: true` for desktop/physical-keyboard users.

## Rules for Modifying Existing Components

When touching existing code, **fix pre-existing violations in the file you're modifying** — do not dismiss them as "pre-existing". Issues compound over time and each modification is an opportunity to fix them. If you add a `Text` element inside an `Accessible.name`-bearing parent and it's missing `Accessible.ignored: true`, add it.

---

## Screen Reader Audit Implementation Plan

### Why screen reader only

Decenza runs primarily on an Android tablet. Users with accessibility needs use TalkBack swipe navigation — `KeyNavigation`, `activeFocusOnTab`, and `forceActiveFocus` have no effect on TalkBack. The keyboard navigation work added to Wave 1 pages (FlushPage, HotWaterPage, SteamPage, IdlePage, EspressoPage) is harmless and correct for desktop users, but it is **not the focus of ongoing accessibility work**. Remaining waves audit screen reader properties only.

The three things that have direct, immediate impact on tablet TalkBack users:
1. **Missing `Accessible.onPressAction`** — element is announced but double-tap does nothing
2. **Missing `Accessible.description`** — secondary actions (long-press, drag) are invisible
3. **Missing `Accessible.ignored: true`** on child Text — element name is announced twice

### Wave 1 — Core operation pages
| Page | Status |
|------|--------|
| `FlushPage.qml` | **Done** |
| `HotWaterPage.qml` | **Done** |
| `SteamPage.qml` | **Done** |
| `IdlePage.qml` | **Done** |
| `EspressoPage.qml` | **Done** |

### Wave 2 — Post-shot and review pages
| Page | Status |
|------|--------|
| `PostShotReviewPage.qml` | **Done** |
| `ShotDetailPage.qml` | **Done** |
| `ShotHistoryPage.qml` | **Done** (was already correct) |
| `BeanInfoPage.qml` | **Done** (was already correct) |

### Wave 3 — Profile management pages
| Page | Status |
|------|--------|
| `ProfileSelectorPage.qml` | **Done** |
| `RecipeEditorPage.qml` | **Done** |
| `SimpleProfileEditorPage.qml` | **Done** |
| `ProfileEditorPage.qml` | **Done** |
| `ProfileInfoPage.qml` | **Done** (was already correct) |

### Wave 4 — Settings tabs
| Tab | Status |
|-----|--------|
| `SettingsConnectionsTab.qml` | **Done** (was already correct) |
| `SettingsMachineTab.qml` | **Done** (was already correct) |
| `SettingsAITab.qml` | **Done** (was already correct) |
| `SettingsLanguageTab.qml` | **Done** |
| `SettingsHistoryDataTab.qml` | **Done** (was already correct) |
| `SettingsThemesTab.qml` | **Done** |
| `SettingsScreensaverTab.qml` | **Done** (was already correct) |
| `SettingsVisualizerTab.qml` | **Done** (was already correct) |
| `SettingsHomeAutomationTab.qml` | **Done** (was already correct) |
| `SettingsDebugTab.qml` | **Done** (was already correct) |
| `SettingsLayoutTab.qml` | **Done** (was already correct) |
| `SettingsUpdateTab.qml` | **Done** (was already correct) |
| `SettingsCalibrationTab.qml` | **Done** (prior wave) |

### Wave 5 — Secondary pages
| Page | Status |
|------|--------|
| `CommunityBrowserPage.qml` | **Done** (was already correct) |
| `VisualizerBrowserPage.qml` | **Done** (was already correct) |
| `VisualizerMultiImportPage.qml` | **Done** |
| `ProfileImportPage.qml` | **Done** (was already correct) |
| `ShotComparisonPage.qml` | **Done** (was already correct) |
| `AutoFavoriteInfoPage.qml` | **Done** (was already correct) |
| `AutoFavoritesPage.qml` | **Done** (was already correct) |
| `DialingAssistantPage.qml` | **Done** (was already correct) |
| `FlowCalibrationPage.qml` | **Done** (was already correct) |
| `DescalingPage.qml` | **Done** (was already correct) |

### Definition of "done" for a page
- All interactive elements have `Accessible.role`, `Accessible.name`, `Accessible.focusable: true`, `Accessible.onPressAction`
- All secondary actions (long-press, drag, double-tap) have `Accessible.description` hints
- All decorative `Text`/icons inside accessible parents have `Accessible.ignored: true`
- No bare `Rectangle+MouseArea` without accessibility properties
