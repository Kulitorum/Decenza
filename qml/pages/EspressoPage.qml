import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../components"

Page {
    id: espressoPage
    objectName: "espressoPage"
    background: Rectangle { color: Theme.backgroundColor }

    // Local weight property - updated directly in signal handler for immediate display
    property real currentWeight: 0.0

    // Accessibility: value announcement cycling (swipe left/right)
    property int accessibilityValueIndex: 0
    readonly property var accessibilityValueNames: ["Frame", "Time", "Pressure", "Flow", "Temperature", "Weight"]

    // Enable keyboard focus for the page
    focus: true

    Component.onCompleted: root.currentPageTitle = MainController.currentProfileName
    StackView.onActivated: {
        root.currentPageTitle = MainController.currentProfileName
        espressoPage.forceActiveFocus()  // Ensure keyboard focus
    }

    // Accessibility: get current value for announcement
    function getAccessibilityValue(index) {
        switch (index) {
            case 0: // Frame
                var frameInfo = MainController.currentFrameName || "Starting"
                return "Frame: " + frameInfo
            case 1: // Time
                return "Time: " + MachineState.shotTime.toFixed(1) + " seconds"
            case 2: // Pressure
                return "Pressure: " + DE1Device.pressure.toFixed(1) + " bar"
            case 3: // Flow
                return "Flow: " + DE1Device.flow.toFixed(1) + " milliliters per second"
            case 4: // Temperature
                return "Temperature: " + DE1Device.temperature.toFixed(1) + " degrees"
            case 5: // Weight
                return "Weight: " + espressoPage.currentWeight.toFixed(1) + " of " + MainController.targetWeight.toFixed(0) + " grams"
            default:
                return ""
        }
    }

    // Accessibility: announce next value
    function announceNextValue() {
        accessibilityValueIndex = (accessibilityValueIndex + 1) % accessibilityValueNames.length
        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
            AccessibilityManager.announce(getAccessibilityValue(accessibilityValueIndex), true)
        }
    }

    // Accessibility: announce previous value
    function announcePreviousValue() {
        accessibilityValueIndex = (accessibilityValueIndex - 1 + accessibilityValueNames.length) % accessibilityValueNames.length
        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
            AccessibilityManager.announce(getAccessibilityValue(accessibilityValueIndex), true)
        }
    }

    // Accessibility: announce full status
    function announceFullStatus() {
        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
            var status = "Shot status. "
            status += getAccessibilityValue(0) + ". "  // Frame
            status += getAccessibilityValue(1) + ". "  // Time
            status += getAccessibilityValue(2) + ". "  // Pressure
            status += getAccessibilityValue(3) + ". "  // Flow
            status += getAccessibilityValue(5)         // Weight
            AccessibilityManager.announce(status, true)
        }
    }

    // Keyboard shortcuts to stop and go back
    Keys.onEscapePressed: {
        root.stopReason = "manual"
        DE1Device.stopOperation()
        root.goToIdle()
    }

    Keys.onSpacePressed: {
        root.stopReason = "manual"
        DE1Device.stopOperation()
        root.goToIdle()
    }

    // Additional keyboard navigation for accessibility
    Keys.onPressed: function(event) {
        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
            if (event.key === Qt.Key_Left) {
                announcePreviousValue()
                event.accepted = true
            } else if (event.key === Qt.Key_Right) {
                announceNextValue()
                event.accepted = true
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                announceFullStatus()
                event.accepted = true
            }
        }
        // Backspace also goes back
        if (event.key === Qt.Key_Backspace) {
            root.stopReason = "manual"
            DE1Device.stopOperation()
            root.goToIdle()
            event.accepted = true
        }
    }

    // Force immediate weight update on signal (bypasses lazy binding evaluation)
    Connections {
        target: MachineState
        function onScaleWeightChanged() {
            espressoPage.currentWeight = MachineState.scaleWeight
        }
    }

    // Full-screen shot graph
    ShotGraph {
        id: shotGraph
        anchors.fill: parent
        anchors.topMargin: Theme.scaled(50)
        anchors.bottomMargin: Theme.scaled(100)
    }

    // Status indicator for preheating
    Rectangle {
        id: statusBanner
        anchors.top: parent.top
        anchors.topMargin: Theme.pageTopMargin + Theme.scaled(20)
        anchors.horizontalCenter: parent.horizontalCenter
        width: statusText.width + Theme.spacingLarge * 2
        height: Theme.scaled(36)
        radius: Theme.scaled(18)
        color: MachineState.phase === MachineStateType.Phase.EspressoPreheating ?
               Theme.accentColor : "transparent"
        visible: MachineState.phase === MachineStateType.Phase.EspressoPreheating

        Accessible.role: Accessible.Alert
        Accessible.name: TranslationManager.translate("espresso.accessible.preheating", "Preheating")

        Tr {
            id: statusText
            anchors.centerIn: parent
            key: "espresso.status.preheating"
            fallback: "PREHEATING..."
            color: Theme.textColor
            font: Theme.bodyFont
        }
    }

    // Bottom info bar with live values
    Rectangle {
        id: infoBar
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: Theme.scaled(100)
        color: Qt.darker(Theme.surfaceColor, 1.3)

        RowLayout {
            anchors.fill: parent
            anchors.margins: Theme.spacingMedium
            spacing: Theme.spacingMedium

            // Back button (square hitbox, full height)
            Item {
                id: espressoBackButton
                Layout.fillHeight: true
                Layout.preferredWidth: height

                Accessible.role: Accessible.Button
                Accessible.name: TranslationManager.translate("espresso.accessible.stop", "Stop and go back")
                Accessible.focusable: true

                Image {
                    anchors.centerIn: parent
                    source: "qrc:/icons/back.svg"
                    sourceSize.width: Theme.scaled(28)
                    sourceSize.height: Theme.scaled(28)
                }

                AccessibleMouseArea {
                    anchors.fill: parent
                    accessibleName: "Stop shot and go back"
                    accessibleItem: espressoBackButton
                    onAccessibleClicked: {
                        root.stopReason = "manual"
                        DE1Device.stopOperation()
                        root.goToIdle()
                    }
                }
            }

            // Timer
            ColumnLayout {
                Layout.preferredWidth: Theme.scaled(100)
                spacing: Theme.scaled(2)

                Accessible.role: Accessible.StaticText
                Accessible.name: TranslationManager.translate("espresso.accessible.time", "Time:") + " " + MachineState.shotTime.toFixed(1) + " " + TranslationManager.translate("espresso.accessible.seconds", "seconds")

                Text {
                    text: MachineState.shotTime.toFixed(1) + "s"
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(36)
                    font.weight: Font.Bold
                }
                Tr {
                    key: "espresso.label.time"
                    fallback: "Time"
                    color: Theme.textSecondaryColor
                    font: Theme.captionFont
                }
            }

            // Divider
            Rectangle {
                Layout.preferredWidth: Theme.scaled(1)
                Layout.fillHeight: true
                Layout.topMargin: Theme.chartMarginSmall
                Layout.bottomMargin: Theme.chartMarginSmall
                color: Theme.textSecondaryColor
                opacity: 0.3
            }

            // Pressure
            ColumnLayout {
                Layout.preferredWidth: Theme.scaled(80)
                spacing: Theme.scaled(2)

                Accessible.role: Accessible.StaticText
                Accessible.name: TranslationManager.translate("espresso.accessible.pressure", "Pressure:") + " " + DE1Device.pressure.toFixed(1) + " " + TranslationManager.translate("espresso.accessible.bar", "bar")

                Text {
                    text: DE1Device.pressure.toFixed(1)
                    color: Theme.pressureColor
                    font.pixelSize: Theme.scaled(28)
                    font.weight: Font.Medium
                }
                Tr {
                    key: "espresso.unit.bar"
                    fallback: "bar"
                    color: Theme.textSecondaryColor
                    font: Theme.captionFont
                }
            }

            // Flow
            ColumnLayout {
                Layout.preferredWidth: Theme.scaled(80)
                spacing: Theme.scaled(2)

                Accessible.role: Accessible.StaticText
                Accessible.name: TranslationManager.translate("espresso.accessible.flow", "Flow:") + " " + DE1Device.flow.toFixed(1) + " " + TranslationManager.translate("espresso.accessible.mlPerSec", "milliliters per second")

                Text {
                    text: DE1Device.flow.toFixed(1)
                    color: Theme.flowColor
                    font.pixelSize: Theme.scaled(28)
                    font.weight: Font.Medium
                }
                Tr {
                    key: "espresso.unit.flowRate"
                    fallback: "mL/s"
                    color: Theme.textSecondaryColor
                    font: Theme.captionFont
                }
            }

            // Temperature
            ColumnLayout {
                Layout.preferredWidth: Theme.scaled(80)
                spacing: Theme.scaled(2)

                Accessible.role: Accessible.StaticText
                Accessible.name: TranslationManager.translate("espresso.accessible.temperature", "Temperature:") + " " + DE1Device.temperature.toFixed(1) + " " + TranslationManager.translate("espresso.accessible.degrees", "degrees")

                Text {
                    text: DE1Device.temperature.toFixed(1)
                    color: Theme.temperatureColor
                    font.pixelSize: Theme.scaled(28)
                    font.weight: Font.Medium
                }
                Tr {
                    key: "espresso.unit.celsius"
                    fallback: "°C"
                    color: Theme.textSecondaryColor
                    font: Theme.captionFont
                }
            }

            // Divider
            Rectangle {
                Layout.preferredWidth: Theme.scaled(1)
                Layout.fillHeight: true
                Layout.topMargin: Theme.chartMarginSmall
                Layout.bottomMargin: Theme.chartMarginSmall
                color: Theme.textSecondaryColor
                opacity: 0.3
            }

            // Weight with progress
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(4)

                Accessible.role: Accessible.StaticText
                Accessible.name: TranslationManager.translate("espresso.accessible.weight", "Weight:") + " " + espressoPage.currentWeight.toFixed(1) + " " + TranslationManager.translate("espresso.accessible.of", "of") + " " + MainController.targetWeight.toFixed(0) + " " + TranslationManager.translate("espresso.accessible.grams", "grams")

                RowLayout {
                    spacing: Theme.spacingSmall

                    Text {
                        text: espressoPage.currentWeight.toFixed(1)
                        color: Theme.weightColor
                        font.pixelSize: Theme.scaled(28)
                        font.weight: Font.Medium
                        Layout.alignment: Qt.AlignBaseline
                    }
                    Text {
                        text: "/ " + MainController.targetWeight.toFixed(0) + " g"
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(18)
                        Layout.alignment: Qt.AlignBaseline
                    }
                }

                ProgressBar {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.spacingSmall
                    from: 0
                    to: MainController.targetWeight
                    value: espressoPage.currentWeight

                    background: Rectangle {
                        color: Theme.surfaceColor
                        radius: Theme.scaled(4)
                    }

                    contentItem: Rectangle {
                        width: parent.visualPosition * parent.width
                        height: parent.height
                        radius: Theme.scaled(4)
                        color: Theme.weightColor
                    }
                }
            }
        }
    }

    // Accessibility: Swipe gesture detection on info bar (excludes back button)
    MouseArea {
        id: infoBarSwipeArea
        anchors.fill: infoBar
        anchors.leftMargin: Theme.scaled(80)  // Don't cover back button
        enabled: typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled
        propagateComposedEvents: true

        property real startX: 0
        property real startY: 0
        property bool swiped: false

        onPressed: function(mouse) {
            startX = mouse.x
            startY = mouse.y
            swiped = false
        }

        onReleased: function(mouse) {
            var deltaX = mouse.x - startX
            var deltaY = mouse.y - startY
            var threshold = 50

            // Horizontal swipe (left or right)
            if (Math.abs(deltaX) > threshold && Math.abs(deltaX) > Math.abs(deltaY)) {
                swiped = true
                if (deltaX > 0) {
                    espressoPage.announceNextValue()
                } else {
                    espressoPage.announcePreviousValue()
                }
            }
        }

        onClicked: function(mouse) {
            // Pass through non-swipe taps to elements below
            if (!swiped) {
                mouse.accepted = false
            }
        }
    }

    // Two-finger tap for full status announcement (excludes back button)
    MultiPointTouchArea {
        anchors.fill: infoBar
        anchors.leftMargin: Theme.scaled(80)  // Don't cover back button
        enabled: typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled
        minimumTouchPoints: 2
        maximumTouchPoints: 2

        onPressed: function(touchPoints) {
            if (touchPoints.length === 2) {
                espressoPage.announceFullStatus()
            }
        }
    }

    // Swipe gestures on chart for accessibility (swipe left/right to navigate values)
    MouseArea {
        id: chartTapArea
        anchors.fill: shotGraph
        enabled: typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled

        property real startX: 0
        property real startY: 0

        onPressed: function(mouse) {
            startX = mouse.x
            startY = mouse.y
        }

        onReleased: function(mouse) {
            var deltaX = mouse.x - startX
            var deltaY = mouse.y - startY
            var threshold = 50

            // Swipe gesture for accessibility navigation
            if (Math.abs(deltaX) > threshold && Math.abs(deltaX) > Math.abs(deltaY)) {
                if (deltaX > 0) {
                    espressoPage.announceNextValue()
                } else {
                    espressoPage.announcePreviousValue()
                }
            }
        }
    }

    // Two-finger tap on chart for full status announcement
    MultiPointTouchArea {
        anchors.fill: shotGraph
        enabled: typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled
        minimumTouchPoints: 2
        maximumTouchPoints: 2

        onPressed: function(touchPoints) {
            if (touchPoints.length === 2) {
                espressoPage.announceFullStatus()
            }
        }
    }

}
