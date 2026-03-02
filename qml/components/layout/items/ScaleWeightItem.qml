import QtQuick
import QtQuick.Layouts
import DecenzaDE1
import "../.."

Item {
    id: root
    property bool isCompact: false
    property string itemId: ""

    property bool isFlowScale: ScaleDevice && ScaleDevice.isFlowScale
    property bool scaleConnected: ScaleDevice && ScaleDevice.connected
    property bool accessibilityEnabled: typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled

    // Scale warning: saved BLE scale not connected or connection failed
    // Don't warn if a USB scale is connected — it satisfies the "have a real scale" requirement
    property bool showScaleWarning: !root.scaleConnected
        && (BLEManager.scaleConnectionFailed || BLEManager.hasSavedScale)
        && !UsbScaleManager.scaleConnected

    implicitWidth: isCompact ? compactContent.implicitWidth : fullContent.implicitWidth
    implicitHeight: isCompact ? compactContent.implicitHeight : fullContent.implicitHeight

    // Accessibility: expose weight/status to screen readers
    Accessible.role: Accessible.Button
    Accessible.name: root.accessibleDescription()
    Accessible.focusable: true
    Accessible.onPressAction: {
        if (root.scaleConnected)
            MachineState.tareScale()
        else if (root.showScaleWarning)
            BLEManager.scanForScales()
    }

    function accessibleDescription() {
        if (root.showScaleWarning && !root.scaleConnected)
            return BLEManager.scaleConnectionFailed ? "Scale not found. Tap to scan" : "Scale connecting"
        if (root.scaleConnected)
            return "Scale weight: " + root.weightText() + ". Tap to tare"
        return "No scale connected"
    }

    // Shared color logic
    function scaleColor(pressed) {
        if (pressed) return Theme.accentColor
        if (MainController.brewByRatioActive) return Theme.primaryColor
        if (root.isFlowScale) return Theme.textSecondaryColor
        return Theme.weightColor
    }

    function weightText() {
        var weight = MachineState.scaleWeight.toFixed(1)
        var suffix = root.isFlowScale ? "g~" : "g"
        if (MainController.brewByRatioActive) {
            return weight + suffix + " 1:" + MainController.brewByRatio.toFixed(1)
        }
        return weight + suffix
    }

    // --- COMPACT MODE ---
    Item {
        id: compactContent
        visible: root.isCompact
        anchors.fill: parent
        implicitWidth: compactWarning.visible ? compactWarning.width : compactScaleRow.implicitWidth
        implicitHeight: compactWarning.visible ? compactWarning.implicitHeight : compactScaleRow.implicitHeight

        // Scale warning (connecting / not found) - tap to scan
        Rectangle {
            id: compactWarning
            visible: root.showScaleWarning && !root.scaleConnected
            anchors.centerIn: parent
            width: compactWarningRow.implicitWidth + Theme.spacingMedium
            height: Theme.touchTargetMin
            color: BLEManager.scaleConnectionFailed ? Theme.errorColor : "transparent"
            radius: Theme.scaled(4)

            Row {
                id: compactWarningRow
                anchors.centerIn: parent
                spacing: Theme.spacingSmall

                Tr {
                    key: BLEManager.scaleConnectionFailed ? "statusbar.scale_not_found" : "statusbar.scale_ellipsis"
                    fallback: BLEManager.scaleConnectionFailed ? "Scale not found" : "Scale..."
                    color: BLEManager.scaleConnectionFailed ? "white" : Theme.textSecondaryColor
                    font: Theme.bodyFont
                    anchors.verticalCenter: parent.verticalCenter
                    Accessible.ignored: true
                }
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: BLEManager.scanForScales()
            }
        }

        // Scale connected: weight display with tare/ratio interaction
        Row {
            id: compactScaleRow
            anchors.centerIn: parent
            spacing: Theme.spacingSmall
            visible: root.scaleConnected

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.scaled(8)
                height: Theme.scaled(8)
                radius: Theme.scaled(4)
                color: root.scaleColor(scaleMouseArea.pressed)
            }

            Text {
                text: root.weightText()
                color: root.scaleColor(scaleMouseArea.pressed)
                font: Theme.bodyFont
                Accessible.ignored: true
            }
        }

        // Disconnected (no saved scale)
        Text {
            anchors.centerIn: parent
            visible: !root.scaleConnected && !root.showScaleWarning
            text: "--"
            color: Theme.textSecondaryColor
            font: Theme.bodyFont
            Accessible.ignored: true
        }

        // Tare / ratio interaction overlay
        MouseArea {
            id: scaleMouseArea
            anchors.fill: parent
            anchors.margins: -Theme.spacingSmall
            visible: root.scaleConnected
            cursorShape: Qt.PointingHandCursor

            property int tapCount: 0
            property var lastTapTime: 0
            property bool longPressTriggered: false

            onPressed: {
                longPressTriggered = false
                if (root.accessibilityEnabled && !MachineState.isFlowing) {
                    longPressTimer.start()
                }
            }

            onReleased: {
                longPressTimer.stop()
                if (longPressTriggered) {
                    longPressTriggered = false
                    return
                }
            }

            onCanceled: {
                longPressTimer.stop()
                longPressTriggered = false
            }

            onClicked: {
                if (longPressTriggered) return

                if (MachineState.isFlowing) {
                    MachineState.tareScale()
                    return
                }

                var now = Date.now()
                var timeSinceLast = now - lastTapTime

                if (timeSinceLast < 300) {
                    tapCount++
                } else {
                    tapCount = 1
                }
                lastTapTime = now

                if (tapCount >= 2) {
                    tapCount = 0
                    singleTapTimer.stop()
                    scaleBrewDialog.open()
                } else {
                    singleTapTimer.restart()
                }
            }
        }

        Timer {
            id: longPressTimer
            interval: 600
            onTriggered: {
                scaleMouseArea.longPressTriggered = true
                scaleBrewDialog.open()
            }
        }

        Timer {
            id: singleTapTimer
            interval: 300
            onTriggered: {
                if (scaleMouseArea.tapCount === 1) {
                    MachineState.tareScale()
                }
                scaleMouseArea.tapCount = 0
            }
        }
    }

    // --- FULL MODE ---
    Item {
        id: fullContent
        visible: !root.isCompact
        anchors.fill: parent
        implicitWidth: fullColumn.implicitWidth
        implicitHeight: fullColumn.implicitHeight

        ColumnLayout {
            id: fullColumn
            anchors.centerIn: parent
            spacing: Theme.spacingSmall

            Text {
                Layout.alignment: Qt.AlignHCenter
                visible: root.scaleConnected
                text: root.weightText()
                color: root.scaleColor(false)
                font: Theme.valueFont
                Accessible.ignored: true
            }

            Text {
                Layout.alignment: Qt.AlignHCenter
                visible: !root.scaleConnected
                text: "--"
                color: Theme.textSecondaryColor
                font: Theme.valueFont
                Accessible.ignored: true
            }

            Tr {
                Layout.alignment: Qt.AlignHCenter
                key: "idle.label.scaleweight"
                fallback: "Scale Weight"
                color: Theme.textSecondaryColor
                font: Theme.labelFont
                Accessible.ignored: true
            }
        }
    }

    // Brew settings dialog (temperature, dose, grind, ratio, yield)
    BrewDialog {
        id: scaleBrewDialog
    }
}
