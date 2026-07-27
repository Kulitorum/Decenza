# Phase 5: EspressoPage Frame Navigation and Value Announcements

## Goal
Add swipe gestures on EspressoPage to navigate between frames and announce current values, providing a non-visual way to understand extraction progress.

## Prerequisites
- Phase 1-4 completed
- Understanding of the info bar structure on EspressoPage

---

## Overview

This phase implements:
1. **Frame navigation gestures**: Swipe left/right to navigate between profile frames
2. **Value announcements**: When navigating, announce the current values from the info bar
3. **Current status on demand**: A gesture to hear the current status at any time

---

## Background: EspressoPage Info Bar

The EspressoPage has a bottom info bar showing:
- **Time**: Shot duration (e.g., "25.3s")
- **Pressure**: Current pressure (e.g., "8.9 bar")
- **Flow**: Current flow rate (e.g., "2.1 mL/s")
- **Temperature**: Current temp (e.g., "93.2°C")
- **Weight**: Current/target weight (e.g., "32.5 / 36 g")

Currently this is visual only. We want to make it accessible via gestures.

---

## Step 1: Add Frame Tracking to EspressoPage

**File: `qml/pages/EspressoPage.qml`**

Add properties to track frame information:

```qml
Page {
    id: espressoPage

    // ... existing properties ...

    // Frame navigation for accessibility
    property int currentFrameIndex: 0
    property int totalFrames: MainController.currentProfile ? MainController.currentProfile.steps.length : 0
    property string currentFrameName: ""

    // Update frame info from MainController
    Connections {
        target: MainController

        function onFrameChanged(frameIndex, frameName) {
            espressoPage.currentFrameIndex = frameIndex
            espressoPage.currentFrameName = frameName
        }
    }

    // ... rest of existing code ...
}
```

---

## Step 2: Add Swipe Gesture Detection

**File: `qml/pages/EspressoPage.qml`**

Add a SwipeArea or use MouseArea with swipe detection:

```qml
Page {
    id: espressoPage

    // ... existing properties and code ...

    // Swipe detection for frame navigation (accessibility)
    MouseArea {
        id: accessibilitySwipeArea
        anchors.fill: parent
        enabled: AccessibilityManager.enabled
        z: 10  // Above the shot graph

        property real startX: 0
        property real startY: 0
        property bool swiping: false
        property real swipeThreshold: Theme.scaled(100)

        onPressed: function(mouse) {
            startX = mouse.x
            startY = mouse.y
            swiping = false
        }

        onPositionChanged: function(mouse) {
            if (!swiping) {
                var deltaX = mouse.x - startX
                var deltaY = mouse.y - startY

                // Only register horizontal swipes
                if (Math.abs(deltaX) > swipeThreshold && Math.abs(deltaX) > Math.abs(deltaY) * 2) {
                    swiping = true

                    if (deltaX > 0) {
                        // Swipe right: go to previous frame info
                        announceFrameInfo("previous")
                    } else {
                        // Swipe left: go to next frame info
                        announceFrameInfo("next")
                    }
                }
            }
        }

        onReleased: {
            // If it was a tap (not a swipe), announce current status
            if (!swiping) {
                announceCurrentStatus()
            }
            swiping = false
        }
    }

    // ... rest of existing code ...
}
```

---

## Step 3: Implement Value Announcement Functions

**File: `qml/pages/EspressoPage.qml`**

Add functions to announce current values:

```qml
Page {
    id: espressoPage

    // ... existing code ...

    // Navigation index for reading values (separate from actual frame)
    property int announcementIndex: 0  // 0=frame, 1=time, 2=pressure, 3=flow, 4=temp, 5=weight

    function announceFrameInfo(direction) {
        // Cycle through info elements
        if (direction === "next") {
            announcementIndex = (announcementIndex + 1) % 6
        } else if (direction === "previous") {
            announcementIndex = (announcementIndex - 1 + 6) % 6
        }

        announceValueAtIndex(announcementIndex)
    }

    function announceValueAtIndex(index) {
        var announcement = ""

        switch (index) {
            case 0:  // Frame
                announcement = "Frame " + (currentFrameIndex + 1) + " of " + totalFrames
                if (currentFrameName) {
                    announcement += ": " + currentFrameName
                }
                break

            case 1:  // Time
                announcement = "Time: " + MachineState.shotTime.toFixed(1) + " seconds"
                break

            case 2:  // Pressure
                announcement = "Pressure: " + DE1Device.pressure.toFixed(1) + " bar"
                // Add target if available
                if (MainController.targetPressure > 0) {
                    announcement += ", target " + MainController.targetPressure.toFixed(1)
                }
                break

            case 3:  // Flow
                announcement = "Flow: " + DE1Device.flow.toFixed(1) + " milliliters per second"
                break

            case 4:  // Temperature
                announcement = "Temperature: " + DE1Device.temperature.toFixed(1) + " degrees"
                break

            case 5:  // Weight
                announcement = "Weight: " + espressoPage.currentWeight.toFixed(1) + " grams"
                if (MainController.targetWeight > 0) {
                    announcement += " of " + MainController.targetWeight.toFixed(0) + " target"
                }
                break
        }

        if (AccessibilityManager.enabled) {
            AccessibilityManager.announce(announcement, true)
        }
    }

    function announceCurrentStatus() {
        // Full status announcement
        var status = ""

        // Phase
        if (MachineState.phase === MachineState.Phase.Preinfusion) {
            status = "Preinfusion. "
        } else if (MachineState.phase === MachineState.Phase.Pouring) {
            status = "Pouring. "
        } else if (MachineState.phase === MachineState.Phase.EspressoPreheating) {
            status = "Preheating. "
        }

        // Time
        status += MachineState.shotTime.toFixed(1) + " seconds. "

        // Pressure and flow
        status += DE1Device.pressure.toFixed(1) + " bar. "
        status += DE1Device.flow.toFixed(1) + " flow. "

        // Weight
        status += espressoPage.currentWeight.toFixed(1) + " grams"
        if (MainController.targetWeight > 0) {
            var remaining = MainController.targetWeight - espressoPage.currentWeight
            if (remaining > 0) {
                status += ", " + remaining.toFixed(0) + " to go"
            }
        }

        if (AccessibilityManager.enabled) {
            AccessibilityManager.announce(status, true)
        }
    }

    // ... rest of existing code ...
}
```

---

## Step 4: Add Keyboard Navigation for Values

**File: `qml/pages/EspressoPage.qml`**

```qml
Page {
    id: espressoPage

    // ... existing code ...

    // Keyboard focus for value navigation
    focus: true

    Keys.onLeftPressed: {
        if (AccessibilityManager.enabled) {
            announceFrameInfo("previous")
        }
    }

    Keys.onRightPressed: {
        if (AccessibilityManager.enabled) {
            announceFrameInfo("next")
        }
    }

    Keys.onReturnPressed: {
        if (AccessibilityManager.enabled) {
            announceCurrentStatus()
        }
    }

    Keys.onSpacePressed: {
        // Space stops the shot
        DE1Device.stopOperation()
        root.goToIdle()
    }

    // ... rest of existing code ...
}
```

---

## Step 5: Add Two-Finger Tap for Full Status

**File: `qml/pages/EspressoPage.qml`**

```qml
// Alternative: Two-finger tap for full status (useful with TalkBack)
MultiPointTouchArea {
    anchors.fill: parent
    minimumTouchPoints: 2
    maximumTouchPoints: 2
    z: 11  // Above swipe area

    onPressed: function(touchPoints) {
        if (touchPoints.length === 2 && AccessibilityManager.enabled) {
            announceCurrentStatus()
        }
    }
}
```

---

## Step 6: Add Visual Indicator for Navigation Mode

**File: `qml/pages/EspressoPage.qml`**

Show a subtle indicator when in accessibility navigation mode:

```qml
// Accessibility navigation indicator
Rectangle {
    id: navIndicator
    anchors.top: parent.top
    anchors.horizontalCenter: parent.horizontalCenter
    anchors.topMargin: Theme.pageTopMargin + Theme.scaled(60)
    width: navIndicatorText.width + 40
    height: navIndicatorText.height + 16
    radius: height / 2
    color: "#80000000"
    visible: false
    z: 100

    Text {
        id: navIndicatorText
        anchors.centerIn: parent
        text: {
            switch (espressoPage.announcementIndex) {
                case 0: return "Frame"
                case 1: return "Time"
                case 2: return "Pressure"
                case 3: return "Flow"
                case 4: return "Temperature"
                case 5: return "Weight"
                default: return ""
            }
        }
        color: "white"
        font: Theme.bodyFont
    }

    // Show briefly when navigating
    Timer {
        id: navIndicatorTimer
        interval: 1500
        onTriggered: navIndicator.visible = false
    }
}

// Update announceValueAtIndex to show indicator
function announceValueAtIndex(index) {
    // ... existing announcement code ...

    // Show visual indicator
    navIndicator.visible = true
    navIndicatorTimer.restart()
}
```

---

## Step 7: Add Help Announcement on Page Entry

**File: `qml/pages/EspressoPage.qml`**

Announce usage instructions when entering the page:

```qml
Page {
    id: espressoPage

    // ... existing code ...

    StackView.onActivated: {
        root.currentPageTitle = MainController.currentProfileName

        // Announce accessibility help on first entry
        if (AccessibilityManager.enabled && AccessibilityManager.verbosity >= 1) {
            // Delay to let page load
            helpTimer.start()
        }
    }

    Timer {
        id: helpTimer
        interval: 1000
        onTriggered: {
            AccessibilityManager.announce(
                "Espresso extraction. " +
                "Swipe left or right to hear values. " +
                "Tap to hear full status. " +
                "Tap back button or press space to stop.",
                false
            )
        }
    }
}
```

---

## Step 8: Update Info Bar Elements with Accessible Names

**File: `qml/pages/EspressoPage.qml`**

Ensure each info bar element has proper accessibility:

```qml
// Bottom info bar with live values
Rectangle {
    id: infoBar
    // ... existing code ...

    Accessible.role: Accessible.ToolBar
    Accessible.name: "Shot status bar"

    RowLayout {
        // ... existing layout ...

        // Timer
        ColumnLayout {
            Accessible.role: Accessible.StaticText
            Accessible.name: "Time: " + MachineState.shotTime.toFixed(1) + " seconds"

            Text {
                text: MachineState.shotTime.toFixed(1) + "s"
                // ... existing styling ...
            }
            Text {
                text: "Time"
                // ... existing styling ...
            }
        }

        // Pressure
        ColumnLayout {
            Accessible.role: Accessible.StaticText
            Accessible.name: "Pressure: " + DE1Device.pressure.toFixed(1) + " bar"

            Text {
                text: DE1Device.pressure.toFixed(1)
                // ... existing styling ...
            }
            Text {
                text: "bar"
                // ... existing styling ...
            }
        }

        // Flow
        ColumnLayout {
            Accessible.role: Accessible.StaticText
            Accessible.name: "Flow: " + DE1Device.flow.toFixed(1) + " milliliters per second"

            // ... existing children ...
        }

        // Temperature
        ColumnLayout {
            Accessible.role: Accessible.StaticText
            Accessible.name: "Temperature: " + DE1Device.temperature.toFixed(1) + " degrees"

            // ... existing children ...
        }

        // Weight
        ColumnLayout {
            Accessible.role: Accessible.StaticText
            Accessible.name: "Weight: " + espressoPage.currentWeight.toFixed(1) + " of " +
                            MainController.targetWeight.toFixed(0) + " grams"

            // ... existing children ...
        }
    }
}
```

---

## Verification Checklist

After implementing Phase 5:

- [ ] Swipe left on EspressoPage announces next value (Time → Pressure → Flow → etc.)
- [ ] Swipe right announces previous value
- [ ] Tap announces full current status
- [ ] Two-finger tap also announces full status
- [ ] Arrow keys (left/right) navigate values on desktop
- [ ] Enter/Return announces full status
- [ ] Space key stops extraction
- [ ] Help message announced on page entry (in Normal+ verbosity)
- [ ] Visual indicator shows current navigation position
- [ ] All info bar elements have Accessible.name

---

## Gesture Summary for Users

| Gesture | Action |
|---------|--------|
| Swipe Left | Announce next value (cycle: Frame → Time → Pressure → Flow → Temp → Weight) |
| Swipe Right | Announce previous value |
| Tap | Announce full status (phase, time, pressure, flow, weight) |
| Two-finger Tap | Same as tap (full status) |
| Space Key | Stop extraction and go back |
| Arrow Keys | Navigate values (like swipe) |
| Enter Key | Announce full status |

---

## Files Modified

| File | Action |
|------|--------|
| qml/pages/EspressoPage.qml | MODIFY - add swipe gestures, value announcements, keyboard nav |

---

## Notes for Implementation

1. **Swipe sensitivity**: The swipe threshold (100dp) may need adjustment. Too sensitive = accidental triggers. Too insensitive = hard to activate.

2. **TalkBack interaction**: When TalkBack is active, swipe gestures are used for navigation. Consider that users may need to use "passthrough" gestures (one-finger double-tap and hold, then swipe).

3. **Screen reader focus**: The swipe area should not interfere with TalkBack's normal element focus. The `z` ordering is important.

4. **Performance**: Announcement updates should not cause performance issues. The values are already being updated in the UI, we're just reading them.

5. **Localization**: Announcement strings should be translatable. Consider using `qsTr()` for all announcement text.

6. **Testing approach**:
   - First test without TalkBack to verify gestures work
   - Then test with TalkBack enabled to verify compatibility
   - Test keyboard navigation on desktop

---

## Future Enhancements

1. **Sonification**: Map pressure/flow to audio pitch for real-time monitoring without speech
2. **Haptic feedback**: Vibrate on frame changes or milestones
3. **Custom gesture configuration**: Let users choose their preferred gestures
4. **Voice commands**: "What's the pressure?" → "8.5 bar"
