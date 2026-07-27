# Phase 4: Audio Feedback and State Announcements

## Goal
Add audio feedback during extraction (tick on frame changes) and TTS announcements for machine state changes, milestones, and errors.

## Prerequisites
- Phase 1 completed (AccessibilityManager with TTS and tick sound)
- Phase 2 completed (Accessible properties)
- Phase 3 completed (Keyboard navigation)

---

## Overview

This phase adds:
1. **Frame change ticks**: Audio tick when profile frame changes during extraction
2. **State announcements**: TTS for machine phases (Heating, Ready, Pouring, etc.)
3. **Milestone announcements**: TTS for targets reached (pressure, weight)
4. **Error announcements**: TTS for errors and warnings

---

## Step 1: Connect Frame Changes to Tick Sound

**File: `src/controllers/maincontroller.cpp`**

Find the frame change detection code (around line 692) and add tick sound:

```cpp
// In handleShotSample() method, around line 692:

// Detect frame changes and add markers with frame names from profile
if (isExtracting && sample.frameNumber >= 0 && sample.frameNumber != m_lastFrameNumber) {
    QString frameName;
    int frameIndex = sample.frameNumber;

    // ... existing frame name lookup code ...

    m_shotDataModel->addPhaseMarker(time, frameName, frameIndex);
    m_lastFrameNumber = sample.frameNumber;

    qDebug() << "Frame change:" << frameIndex << "->" << frameName << "at" << time << "s";

    // NEW: Play tick sound for accessibility
    emit frameChanged(frameIndex, frameName);
}
```

**Add signal to header file: `src/controllers/maincontroller.h`**

```cpp
signals:
    // ... existing signals ...

    // Accessibility: emitted when extraction frame changes
    void frameChanged(int frameIndex, const QString& frameName);
```

---

## Step 2: Connect MainController to AccessibilityManager

**Option A: Connect in C++ (main.cpp)**

```cpp
// After creating both objects:
QObject::connect(mainController, &MainController::frameChanged,
                 accessibilityManager, [accessibilityManager](int frameIndex, const QString& frameName) {
    accessibilityManager->playTick();

    // Optionally announce frame name in verbose mode
    if (accessibilityManager->verbosity() >= AccessibilityManager::Verbose) {
        accessibilityManager->announce(frameName);
    }
});
```

**Option B: Connect in QML (main.qml)**

```qml
Connections {
    target: MainController
    function onFrameChanged(frameIndex, frameName) {
        AccessibilityManager.playTick()

        // Optionally announce frame name in verbose mode
        if (AccessibilityManager.verbosity >= 2) {  // Verbose
            AccessibilityManager.announce(frameName)
        }
    }
}
```

---

## Step 3: Add Machine State Announcements

**File: `qml/main.qml`**

Connect to MachineState phase changes:

```qml
Connections {
    target: MachineState
    enabled: AccessibilityManager.enabled

    function onPhaseChanged() {
        var phase = MachineState.phase
        var announcement = ""

        switch (phase) {
            case MachineState.Phase.Disconnected:
                announcement = "Machine disconnected"
                break
            case MachineState.Phase.Sleep:
                announcement = "Machine sleeping"
                break
            case MachineState.Phase.Idle:
                announcement = "Machine idle"
                break
            case MachineState.Phase.Heating:
                announcement = "Heating"
                break
            case MachineState.Phase.Ready:
                announcement = "Ready"
                break
            case MachineState.Phase.EspressoPreheating:
                announcement = "Preheating for espresso"
                break
            case MachineState.Phase.Preinfusion:
                announcement = "Preinfusion started"
                break
            case MachineState.Phase.Pouring:
                announcement = "Pouring"
                break
            case MachineState.Phase.Ending:
                announcement = "Shot complete"
                break
            case MachineState.Phase.Steaming:
                announcement = "Steaming"
                break
            case MachineState.Phase.HotWater:
                announcement = "Dispensing hot water"
                break
            case MachineState.Phase.Flushing:
                announcement = "Flushing"
                break
        }

        if (announcement.length > 0) {
            AccessibilityManager.announce(announcement, true)
        }
    }
}
```

---

## Step 4: Add Milestone Announcements (Extraction)

**File: `qml/pages/EspressoPage.qml`**

Track and announce milestones during extraction:

```qml
Page {
    id: espressoPage

    // Track announced milestones to avoid repeats
    property bool announcedPressureReached: false
    property bool announcedWeightReached: false
    property int lastAnnouncedTime: 0  // For periodic announcements

    // Reset on new shot
    Connections {
        target: MachineState

        function onShotStarted() {
            announcedPressureReached = false
            announcedWeightReached = false
            lastAnnouncedTime = 0
        }
    }

    // Monitor pressure for announcement
    Connections {
        target: DE1Device
        enabled: AccessibilityManager.enabled && AccessibilityManager.verbosity >= 1

        function onPressureChanged() {
            // Announce when pressure reaches target (within 0.5 bar)
            if (!espressoPage.announcedPressureReached &&
                DE1Device.pressure >= MainController.targetPressure - 0.5) {
                espressoPage.announcedPressureReached = true
                AccessibilityManager.announce("Target pressure reached: " +
                    DE1Device.pressure.toFixed(1) + " bar")
            }
        }
    }

    // Monitor weight for announcement
    Connections {
        target: MachineState
        enabled: AccessibilityManager.enabled && AccessibilityManager.verbosity >= 1

        function onScaleWeightChanged() {
            var currentWeight = MachineState.scaleWeight
            var targetWeight = MainController.targetWeight

            // Announce when approaching target (within 5g)
            if (!espressoPage.announcedWeightReached &&
                targetWeight > 0 &&
                currentWeight >= targetWeight - 5) {
                espressoPage.announcedWeightReached = true
                AccessibilityManager.announce("Target weight reached: " +
                    currentWeight.toFixed(0) + " grams")
            }
        }
    }

    // Periodic time announcements (verbose mode only)
    Timer {
        id: periodicAnnounceTimer
        interval: 10000  // Every 10 seconds
        running: MachineState.isExtracting && AccessibilityManager.enabled && AccessibilityManager.verbosity >= 2
        repeat: true

        onTriggered: {
            var time = MachineState.shotTime
            if (Math.floor(time / 10) > espressoPage.lastAnnouncedTime) {
                espressoPage.lastAnnouncedTime = Math.floor(time / 10)
                AccessibilityManager.announce(
                    Math.floor(time) + " seconds. " +
                    DE1Device.pressure.toFixed(1) + " bar. " +
                    espressoPage.currentWeight.toFixed(0) + " grams."
                )
            }
        }
    }

    // ... existing page code ...
}
```

---

## Step 5: Add Error and Warning Announcements

**File: `qml/main.qml`**

Announce errors when dialogs open:

```qml
// BLE Error Dialog
Dialog {
    id: bleErrorDialog
    // ... existing code ...

    onOpened: {
        if (AccessibilityManager.enabled) {
            AccessibilityManager.announce("Error: " + contentItem.text, true)
        }
    }
}

// Scale Disconnected Dialog
Dialog {
    id: scaleDisconnectedDialog
    // ... existing code ...

    onOpened: {
        if (AccessibilityManager.enabled) {
            AccessibilityManager.announce("Warning: Scale disconnected", true)
        }
    }
}

// Refill Dialog
Dialog {
    id: refillDialog
    // ... existing code ...

    onOpened: {
        if (AccessibilityManager.enabled) {
            AccessibilityManager.announce("Warning: Water tank needs refill", true)
        }
    }
}
```

---

## Step 6: Add Steam/HotWater/Flush Announcements

**File: `qml/pages/SteamPage.qml`** (and similar pages)

```qml
Page {
    id: steamPage

    // Announce when steaming completes
    Connections {
        target: MachineState
        enabled: AccessibilityManager.enabled

        function onPhaseChanged() {
            if (previousPhase === MachineState.Phase.Steaming &&
                MachineState.phase !== MachineState.Phase.Steaming) {
                AccessibilityManager.announce("Steaming complete", true)
            }
        }
    }

    // Track progress and announce milestones
    property real previousProgress: 0

    Connections {
        target: MachineState
        enabled: AccessibilityManager.enabled && AccessibilityManager.verbosity >= 1

        function onSteamProgressChanged() {
            var progress = MachineState.steamProgress
            // Announce 50% milestone
            if (previousProgress < 0.5 && progress >= 0.5) {
                AccessibilityManager.announce("Halfway complete")
            }
            previousProgress = progress
        }
    }
}
```

---

## Step 7: Add Shot Completion Summary

**File: `qml/main.qml`** or EspressoPage

```qml
Connections {
    target: MachineState
    enabled: AccessibilityManager.enabled

    function onShotEnded() {
        // Announce shot summary
        var time = MachineState.shotTime
        var weight = MachineState.scaleWeight

        var summary = "Shot complete. " +
            time.toFixed(1) + " seconds. " +
            weight.toFixed(0) + " grams."

        // Delay slightly to not overlap with phase change announcement
        Qt.callLater(function() {
            AccessibilityManager.announce(summary, true)
        })
    }
}
```

---

## Step 8: Add Connection Status Announcements

**File: `qml/main.qml`**

```qml
Connections {
    target: DE1Device
    enabled: AccessibilityManager.enabled

    function onConnectedChanged() {
        if (DE1Device.connected) {
            AccessibilityManager.announce("Machine connected")
        } else {
            AccessibilityManager.announce("Machine disconnected", true)
        }
    }
}

Connections {
    target: ScaleDevice
    enabled: AccessibilityManager.enabled

    function onConnectedChanged() {
        if (ScaleDevice && ScaleDevice.connected) {
            AccessibilityManager.announce("Scale connected: " + ScaleDevice.name)
        }
        // Disconnection is handled by scaleDisconnectedDialog
    }
}
```

---

## Step 9: Add Profile Selection Announcement

**File: `qml/pages/ProfileSelectorPage.qml`** or IdlePage

```qml
// When a profile is selected
function selectProfile(profile) {
    MainController.selectProfile(profile)

    if (AccessibilityManager.enabled) {
        AccessibilityManager.announce("Profile selected: " + profile.name)
    }
}
```

---

## Step 10: Add Value Change Announcements

**File: `qml/components/ValueInput.qml`**

Add announcement when value changes (debounced to avoid spam):

```qml
Item {
    id: root

    // ... existing code ...

    // Debounce timer for value announcements
    Timer {
        id: announceTimer
        interval: 500  // Wait 500ms after last change
        onTriggered: {
            if (AccessibilityManager.enabled && AccessibilityManager.verbosity >= 1) {
                var announcement = root.value.toFixed(root.decimals)
                if (root.suffix) {
                    announcement += " " + root.suffix.trim()
                }
                AccessibilityManager.announce(announcement)
            }
        }
    }

    // Trigger announcement on value change (debounced)
    onValueChanged: {
        announceTimer.restart()
    }
}
```

---

## Verification Checklist

After implementing Phase 4:

- [ ] Tick sound plays on each frame change during extraction
- [ ] "Heating", "Ready", "Pouring", "Shot complete" are announced
- [ ] "Target pressure reached" announced when pressure hits target
- [ ] "Target weight reached" announced when weight hits target
- [ ] Periodic time announcements in verbose mode
- [ ] Errors and warnings are announced
- [ ] Connection status changes are announced
- [ ] Profile selection is announced
- [ ] Value changes are announced (debounced)
- [ ] Announcements can be heard during TalkBack usage

---

## Files Modified

| File | Action |
|------|--------|
| src/controllers/maincontroller.h | MODIFY - add frameChanged signal |
| src/controllers/maincontroller.cpp | MODIFY - emit frameChanged |
| src/main.cpp | MODIFY - connect frameChanged to accessibilityManager (Option A) |
| qml/main.qml | MODIFY - add state/error announcements, frameChanged connection (Option B) |
| qml/pages/EspressoPage.qml | MODIFY - add milestone announcements |
| qml/pages/SteamPage.qml | MODIFY - add completion announcement |
| qml/pages/HotWaterPage.qml | MODIFY - add completion announcement |
| qml/pages/FlushPage.qml | MODIFY - add completion announcement |
| qml/pages/ProfileSelectorPage.qml | MODIFY - add selection announcement |
| qml/components/ValueInput.qml | MODIFY - add value change announcement |

---

## Notes for Implementation

1. **Debouncing**: Value change announcements should be debounced to avoid rapid-fire speech during slider drags.

2. **Interrupt vs. Queue**: Use `interrupt = true` for important announcements (errors, shot complete) to cut off any ongoing speech.

3. **Verbosity levels**:
   - Minimal (0): Only errors, start/stop
   - Normal (1): + milestones (pressure, weight reached)
   - Verbose (2): + periodic status every 10 seconds

4. **TTS rate**: Default TTS rate is usually fine, but consider adding a setting for users who prefer faster/slower speech.

5. **Tick sound volume**: The tick sound should be subtle (0.5 volume) so it doesn't interfere with TTS.

6. **TalkBack interaction**: When TalkBack is active, TTS announcements from the app compete with TalkBack's announcements. Use `interrupt = true` sparingly.

7. **Testing**: Test with TalkBack enabled on an Android device to ensure announcements don't conflict with screen reader.
