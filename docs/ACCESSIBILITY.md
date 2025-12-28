# Accessibility Analysis for Decenza DE1

This document analyzes the current state of accessibility in the Decenza DE1 application and outlines solutions for making the app usable by blind and visually impaired users.

---

## Executive Summary

**Current State**: The application has **zero accessibility implementation**. No screen reader support, no keyboard navigation, no announcements.

**Good News**: The foundation is solid - good touch targets, excellent contrast ratios, well-structured components, and responsive scaling. Adding accessibility is a matter of enhancement, not redesign.

**Scope**: This analysis focuses on UI accessibility. Real-time graph display is out of scope for the initial implementation.

---

## Current Strengths

| Aspect | Status | Details |
|--------|--------|---------|
| Touch Targets | Good | 44dp+ minimum defined in Theme.qml |
| Color Contrast | Excellent | ~19:1 white on dark, meets WCAG AAA |
| Font Sizes | Good | 18px body text, 48-72px for key values |
| Component Structure | Good | Well-organized, reusable components |
| Responsive Scaling | Good | `Theme.scaled()` adapts to screen size |

---

## Current Gaps

### 1. No Screen Reader Support
- Zero `Accessible.*` properties in entire codebase
- No semantic roles defined (button, slider, heading, etc.)
- No accessible names or descriptions
- Screen readers cannot identify or describe any UI element

### 2. No Keyboard Navigation
- No tab order defined
- No `KeyNavigation` chains
- No focus indicators
- Only 2 keyboard handlers exist (debug toggle, screensaver wake)

### 3. Gesture-Only Interactions
The following interactions have no keyboard/screen-reader alternative:

| Gesture | Where Used | What It Does |
|---------|------------|--------------|
| Long-press (500ms) | ActionButton, ValueInput | Open settings, access secondary action |
| Double-click | Preset pills | Edit preset |
| Vertical drag | ValueInput | Adjust value continuously |
| Horizontal drag | PresetPillRow | Reorder presets |
| Drag | TouchSlider | Adjust value |

### 4. Visual-Only Indicators

| Indicator | What It Means | Current Access |
|-----------|---------------|----------------|
| Colored dots (green/red) | Connection status | Visual only |
| Opacity change | Dragging state | Visual only |
| Checkmark animation | Operation complete | Visual only |
| Progress bar fill | Extraction progress | Visual only |
| Line dash style | Goal vs actual data | Visual only |

### 5. No State Announcements
- Machine phase changes (Heating → Ready → Pouring) not announced
- Completion events shown visually only
- Errors and warnings display but don't announce
- Real-time values (pressure, flow, temp, weight) never spoken

### 6. Small Touch Targets
- ValueInput buttons: 32dp wide (should be 44dp+)
- Status indicator dots: 8-12dp (should be 44dp+ or have larger hit area)

---

## Application Structure

### Pages (9 total)

| Page | Purpose | Key Interactions |
|------|---------|------------------|
| **IdlePage** | Main hub | 4 action buttons, temperature, water level, sleep, settings |
| **EspressoPage** | Live extraction | Graph, timer, metrics, progress bar |
| **SteamPage** | Milk steaming | Preset selection, duration/flow/temp controls, live view |
| **HotWaterPage** | Hot water | Same as SteamPage |
| **FlushPage** | Machine flush | Same as SteamPage |
| **SettingsPage** | Configuration | 5 tabs, various toggles and inputs |
| **ProfileSelectorPage** | Profile management | Two lists, favorites, drag-to-reorder |
| **ProfileEditorPage** | Profile editing | Complex form |
| **ScreensaverPage** | Idle display | Any touch to wake |

### Key Components

| Component | Purpose | Accessibility Priority |
|-----------|---------|----------------------|
| **ActionButton** | Main action buttons | HIGH - most common interaction |
| **ValueInput** | Value adjustment | HIGH - complex gestures |
| **TouchSlider** | Slider control | HIGH - value adjustment |
| **PresetPillRow** | Preset selection | MEDIUM - selection + reorder |
| **StatusBar** | Status display | MEDIUM - important info |
| **BottomBar** | Navigation | MEDIUM - back button |
| **CircularGauge** | Value display | LOW - purely informational |
| **ShotGraph** | Extraction chart | DEFERRED - complex visualization |

---

## Proposed Solutions

### Phase 1: Screen Reader Foundation

Add `Accessible.*` properties to all interactive elements.

#### ActionButton.qml
```qml
ActionButton {
    Accessible.role: Accessible.Button
    Accessible.name: label  // e.g., "Espresso"
    Accessible.description: "Double-click to select profile. Long-press for settings."
    Accessible.focusable: true
}
```

#### ValueInput.qml
```qml
ValueInput {
    Accessible.role: Accessible.Slider
    Accessible.name: label + ": " + value + " " + unit  // e.g., "Temperature: 93.5 degrees"
    Accessible.description: "Use plus and minus buttons to adjust. Current value: " + value
    Accessible.focusable: true
}
```

#### All Interactive Elements
Every `MouseArea`, `Button`, `Switch`, and custom control needs:
- `Accessible.role`: What kind of control is it?
- `Accessible.name`: What does it do? (spoken by screen reader)
- `Accessible.description`: How to use it? (optional additional info)
- `Accessible.focusable: true`: Can receive keyboard focus

### Phase 2: Keyboard Navigation

#### Add Tab Order
```qml
// In each page, define navigation order:
ActionButton {
    id: espressoButton
    KeyNavigation.right: steamButton
    KeyNavigation.down: hotWaterButton
    activeFocusOnTab: true
}
```

#### Add Focus Indicators
```qml
// In Theme.qml, add focus style:
readonly property int focusBorderWidth: 3
readonly property color focusColor: primaryColor

// In components, add focus rectangle:
Rectangle {
    visible: parent.activeFocus
    border.color: Theme.focusColor
    border.width: Theme.focusBorderWidth
    color: "transparent"
    anchors.fill: parent
    anchors.margins: -2
}
```

#### Keyboard Alternatives for Gestures

| Gesture | Keyboard Alternative |
|---------|---------------------|
| Long-press | Press and hold Enter for 500ms, OR use Shift+Enter |
| Double-click | Double-tap Enter quickly |
| Vertical drag | Up/Down arrow keys |
| Horizontal drag | Left/Right arrow keys + modifier |

For ValueInput:
```qml
Keys.onUpPressed: incrementValue()
Keys.onDownPressed: decrementValue()
Keys.onPressed: {
    if (event.key === Qt.Key_Enter || event.key === Qt.Key_Return) {
        openPopup()  // Allow direct numeric entry
    }
}
```

### Phase 3: State Announcements

#### Using Qt TextToSpeech
```cpp
// In maincontroller.cpp or new accessibilitymanager.cpp
#include <QTextToSpeech>

class AccessibilityManager : public QObject {
    Q_OBJECT
public:
    void announce(const QString& text, bool interrupt = false) {
        if (m_enabled && m_tts) {
            if (interrupt) m_tts->stop();
            m_tts->say(text);
        }
    }

private:
    QTextToSpeech* m_tts;
    bool m_enabled = true;
};
```

#### What to Announce

**Machine State Changes** (always):
- "Heating"
- "Ready"
- "Preinfusion started"
- "Pouring"
- "Shot complete"

**Milestones** (configurable verbosity):
- "Target pressure reached: 9 bar"
- "Weight 18 grams reached"
- "Timer: 30 seconds"

**Errors and Warnings** (always):
- "Scale disconnected"
- "Water tank needs refill"
- "Connection lost"

**User Actions** (optional):
- "Profile selected: Rao Allonge"
- "Temperature set to 93.5 degrees"

### Phase 4: Accessible Dialogs

Ensure focus moves to dialogs when they open:
```qml
Dialog {
    onOpened: {
        // Move focus to first interactive element
        okButton.forceActiveFocus()
    }

    // Announce dialog title
    Accessible.role: Accessible.AlertMessage
    Accessible.name: title
}
```

### Phase 5: Alternative Data Representation (for graphs, later)

For when we tackle graph accessibility:

#### Shot Summary Panel
Instead of only showing the graph, provide a text summary:
```
Current Shot Status:
- Phase: Pouring
- Time: 25.3 seconds
- Pressure: 8.9 bar (target: 9.0)
- Flow: 2.1 ml/s
- Weight: 32.5 grams (target: 36)
- Temperature: 93.2°C
```

#### Event List
Navigable list of extraction events:
```
Events:
- 0:00 Shot started
- 0:07 Preinfusion complete
- 0:12 Pressure reached 9 bar
- 0:24 Flow stabilized at 2.0 ml/s
- 0:35 Target weight reached
```

---

## Implementation Priority

### Tier 1: Essential (enables basic use)
1. Add `Accessible.*` properties to all buttons and controls
2. Add keyboard navigation (Tab, Enter, Arrow keys)
3. Add visible focus indicators
4. Announce machine state changes and errors

### Tier 2: Important (improves usability)
1. Keyboard alternatives for gestures (long-press, drag)
2. Announce value changes when adjusting controls
3. Configurable announcement verbosity
4. Add accessible descriptions for complex controls

### Tier 3: Enhanced (great experience)
1. Shot summary panel (text alternative to graph)
2. Voice commands for basic operations
3. Haptic feedback (if platform supports)
4. Audio tones for status (optional)

---

## File-by-File Implementation Guide

### High Priority Files

| File | Changes Needed |
|------|---------------|
| `qml/components/ActionButton.qml` | Add Accessible properties, keyboard handler, focus indicator |
| `qml/components/ValueInput.qml` | Add Accessible properties, arrow key support, popup accessibility |
| `qml/components/TouchSlider.qml` | Add Accessible properties, keyboard support |
| `qml/components/PresetPillRow.qml` | Add Accessible properties to pills, keyboard reordering |
| `qml/components/StatusBar.qml` | Add Accessible descriptions to status items |
| `qml/components/BottomBar.qml` | Add Accessible properties to back button |
| `qml/Theme.qml` | Add focus indicator styles |
| `qml/pages/IdlePage.qml` | Add KeyNavigation chain, focus management |
| `qml/pages/SettingsPage.qml` | Add tab keyboard navigation |

### Medium Priority Files

| File | Changes Needed |
|------|---------------|
| `qml/pages/SteamPage.qml` | Accessible presets, value controls |
| `qml/pages/HotWaterPage.qml` | Same as SteamPage |
| `qml/pages/FlushPage.qml` | Same as SteamPage |
| `qml/pages/ProfileSelectorPage.qml` | Accessible list navigation |
| `qml/main.qml` | Global focus management, dialog accessibility |

### New Files to Create

| File | Purpose |
|------|---------|
| `src/core/accessibilitymanager.h/cpp` | TTS announcements, accessibility settings |
| `qml/components/FocusIndicator.qml` | Reusable focus rectangle component |
| `qml/components/AccessibleButton.qml` | Optional: wrapper with built-in accessibility |

---

## Testing Strategy

### Manual Testing
1. Enable TalkBack (Android) or VoiceOver (iOS/macOS)
2. Navigate entire app using only focus (swipe or tab)
3. Try all operations without looking at screen
4. Verify announcements are clear and timely

### Test Scenarios
1. Start the app, navigate to Espresso, start a shot
2. Adjust temperature in Steam settings
3. Select a different profile
4. Navigate settings tabs
5. Handle an error (disconnect scale)
6. Complete a shot and hear completion

### Accessibility Checklist
- [ ] Every button has a name
- [ ] Every slider announces its value
- [ ] Focus moves logically with Tab
- [ ] Arrow keys work for value adjustment
- [ ] State changes are announced
- [ ] Dialogs announce and trap focus
- [ ] No interaction requires vision

---

## Platform-Specific Considerations

### Android (Primary Platform)
- TalkBack is the screen reader
- Accessible properties map to Android accessibility API
- Need to test on actual device (emulator support is limited)
- Consider AccessibilityService for advanced features

### iOS
- VoiceOver is the screen reader
- Qt accessibility maps to UIAccessibility
- Generally good support in Qt for iOS

### Desktop
- NVDA/JAWS (Windows), VoiceOver (macOS), Orca (Linux)
- Keyboard navigation is primary input method
- Full keyboard alternatives essential

---

## Installing TTS Engines on Android (No Play Store)

If your Android device doesn't have the Google Play Store (e.g., some tablets, degoogled devices, or devices with only F-Droid), you'll need to install a TTS engine manually for text-to-speech to work.

### Prerequisites

- **ADB** (Android Debug Bridge) installed on your computer
- **USB debugging** enabled on the Android device
- Device connected via USB

### Option 1: Install RHVoice (Recommended - Open Source)

RHVoice is a free, open-source TTS engine available on F-Droid with good English voice quality.

```bash
# Download RHVoice from F-Droid
curl -L -o /tmp/rhvoice.apk "https://f-droid.org/repo/com.github.olga_yakovleva.rhvoice.android_116050.apk"

# Install via ADB
adb install /tmp/rhvoice.apk
```

After installation:
1. Open **Settings → Accessibility → Text-to-speech output**
2. Select **RHVoice** as the preferred engine
3. Open RHVoice app to download voice data for your language

### Option 2: Install Aurora Store (Play Store Alternative)

Aurora Store lets you download apps from Google Play without Google Play Services.

```bash
# Download Aurora Store from F-Droid
curl -L -o /tmp/aurora.apk "https://f-droid.org/repo/com.aurora.store_61.apk"

# Install via ADB
adb install /tmp/aurora.apk
```

Then use Aurora Store to:
1. Search for "Google Text-to-Speech"
2. Download and install it
3. Set it as default in Settings → Accessibility → Text-to-speech output

### Option 3: eSpeak NG (Lightweight, Robotic Voice)

If already installed but not working, try reinstalling:

```bash
# Check if eSpeak is installed
adb shell pm list packages | grep espeak

# If present, check current TTS setting
adb shell settings get secure tts_default_synth
```

### Verifying TTS Installation

```bash
# List all TTS-related packages
adb shell pm list packages | grep -iE "tts|speech|voice"

# Check current default TTS engine
adb shell settings get secure tts_default_synth

# Check Android version (for APK compatibility)
adb shell getprop ro.build.version.release

# Check device architecture (for correct APK variant)
adb shell getprop ro.product.cpu.abi
```

### Troubleshooting

| Issue | Solution |
|-------|----------|
| TTS not speaking | Open the TTS app and download voice data |
| Wrong language | Go to TTS settings and select correct language |
| Robotic voice | Switch from eSpeak to RHVoice or Google TTS |
| App won't install | Check Android version compatibility and architecture |

---

## Resources

- [Qt Accessibility](https://doc.qt.io/qt-6/accessible.html)
- [QML Accessible Attached Properties](https://doc.qt.io/qt-6/qml-qtquick-accessible.html)
- [Android Accessibility Guidelines](https://developer.android.com/guide/topics/ui/accessibility)
- [WCAG 2.1 Guidelines](https://www.w3.org/WAI/WCAG21/quickref/)
- [Qt TextToSpeech](https://doc.qt.io/qt-6/qttexttospeech-index.html)

---

## Notes

This document was created in response to feedback from a blind user who wanted to use the Decenza DE1 app. The goal is to make espresso control accessible to users who cannot see the screen - a capability almost no coffee software provides today.

The Qt/QML foundation of this app is actually excellent for accessibility. Unlike web or purely visual frameworks, Qt has first-class accessibility support that maps directly to platform accessibility APIs. We just need to use it.

*"There are blind users who care deeply about extraction theory, profile design, and repeatability - but almost no tools that truly respect that."*
