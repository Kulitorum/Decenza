import QtQuick
import QtQuick.Layouts
import DecenzaDE1

Rectangle {
    color: Theme.surfaceColor

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.chartMarginSmall
        anchors.rightMargin: Theme.spacingLarge
        spacing: Theme.spacingMedium

        // Page title (from root.currentPageTitle)
        // Orange when in simulation mode to indicate simulated machine
        Text {
            text: root.currentPageTitle
            color: DE1Device.simulationMode ? "#E65100" : Theme.textColor
            font.pixelSize: Theme.scaled(20)
            font.bold: true
            Layout.preferredWidth: implicitWidth
            elide: Text.ElideRight
        }

        // Sub state (when actively flowing)
        Text {
            text: "- " + DE1Device.subStateString
            color: Theme.textSecondaryColor
            font: Theme.bodyFont
            visible: MachineState.isFlowing
        }

        // Shot plan summary (fills remaining space, clickable to open BrewDialog)
        ShotPlanText {
            id: statusBarShotPlan
            Layout.fillWidth: true
            visible: Settings.showShotPlan && Settings.showShotPlanOnAllScreens && text !== ""
            onClicked: brewRatioDialog.open()
        }

        // Spacer (when shot plan is not shown in status bar)
        Item {
            Layout.fillWidth: true
            visible: !statusBarShotPlan.visible
        }

        // Temperature (tap to tare scale)
        Item {
            implicitWidth: tempText.implicitWidth
            implicitHeight: tempText.implicitHeight

            Accessible.role: Accessible.Button
            Accessible.name: temperatureAccessible.text + DE1Device.temperature.toFixed(1) + " " + degreesCelsiusAccessible.text + ". " + tapToTareAccessible.text

            // Hidden Tr elements for accessible names
            Tr { id: temperatureAccessible; key: "statusbar.temperature"; fallback: "Temperature: "; visible: false }
            Tr { id: degreesCelsiusAccessible; key: "statusbar.degrees_celsius"; fallback: "degrees Celsius"; visible: false }
            Tr { id: tapToTareAccessible; key: "statusbar.tap_to_tare"; fallback: "Tap to tare scale"; visible: false }

            Text {
                id: tempText
                text: DE1Device.temperature.toFixed(1) + "°C"
                color: tempMouseArea.pressed ? Theme.accentColor : Theme.temperatureColor
                font: Theme.bodyFont
            }

            MouseArea {
                id: tempMouseArea
                anchors.fill: parent
                anchors.margins: -Theme.spacingSmall  // Expand touch target
                cursorShape: Qt.PointingHandCursor
                onClicked: MachineState.tareScale()
            }
        }

        // Separator
        Rectangle {
            width: Theme.scaled(1)
            height: Theme.scaled(30)
            color: Theme.textSecondaryColor
            opacity: 0.3
        }

        // Water level
        Text {
            property bool showMl: Settings.waterLevelDisplayUnit === "ml"
            text: showMl ? DE1Device.waterLevelMl + " ml" : DE1Device.waterLevel.toFixed(0) + "%"
            color: DE1Device.waterLevelMl < 200 ? Theme.errorColor :
                   DE1Device.waterLevelMl < 400 ? Theme.warningColor : Theme.primaryColor
            font: Theme.bodyFont

            Accessible.role: Accessible.StaticText
            Accessible.name: showMl
                ? waterLevelAccessible.text + DE1Device.waterLevelMl + " " + mlAccessible.text
                : waterLevelAccessible.text + DE1Device.waterLevel.toFixed(0) + " " + percentAccessible.text

            // Hidden Tr elements for accessible names
            Tr { id: waterLevelAccessible; key: "statusbar.water_level"; fallback: "Water level: "; visible: false }
            Tr { id: percentAccessible; key: "statusbar.percent"; fallback: "percent"; visible: false }
            Tr { id: mlAccessible; key: "statusbar.ml"; fallback: "milliliters"; visible: false }
        }

        // Separator
        Rectangle {
            width: Theme.scaled(1)
            height: Theme.scaled(30)
            color: Theme.textSecondaryColor
            opacity: 0.3
        }

        // Scale warning (clickable button to scan)
        Rectangle {
            visible: BLEManager.scaleConnectionFailed || (BLEManager.hasSavedScale && (!ScaleDevice || !ScaleDevice.connected))
            color: BLEManager.scaleConnectionFailed ? Theme.errorColor : "transparent"
            radius: Theme.scaled(4)
            Layout.preferredHeight: Theme.touchTargetMin
            Layout.preferredWidth: scaleWarningRow.implicitWidth + Theme.spacingMedium

            Accessible.role: Accessible.Button
            Accessible.name: BLEManager.scaleConnectionFailed ? scaleNotFoundAccessible.text + ". " + scanAccessible.text : scaleConnectingAccessible.text
            Accessible.focusable: true

            // Hidden Tr elements for accessible names
            Tr { id: scaleNotFoundAccessible; key: "statusbar.scale_not_found"; fallback: "Scale not found"; visible: false }
            Tr { id: scanAccessible; key: "statusbar.tap_to_scan"; fallback: "Tap to scan"; visible: false }
            Tr { id: scaleConnectingAccessible; key: "statusbar.scale_connecting"; fallback: "Scale connecting"; visible: false }

            // Make entire area clickable
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: BLEManager.scanForScales()
            }

            Row {
                id: scaleWarningRow
                anchors.centerIn: parent
                spacing: Theme.spacingSmall

                Tr {
                    key: BLEManager.scaleConnectionFailed ? "statusbar.scale_not_found" : "statusbar.scale_ellipsis"
                    fallback: BLEManager.scaleConnectionFailed ? "Scale not found" : "Scale..."
                    color: BLEManager.scaleConnectionFailed ? "white" : Theme.textSecondaryColor
                    font: Theme.bodyFont
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }

        // Scale connected indicator (tap to tare, double-tap for ratio)
        Item {
            id: scaleIndicator
            visible: ScaleDevice && ScaleDevice.connected
            implicitWidth: scaleRow.implicitWidth
            implicitHeight: scaleRow.implicitHeight

            // Detect if using FlowScale (estimated weight from flow sensor)
            property bool isFlowScale: ScaleDevice && ScaleDevice.name === "Flow Scale"

            // Check if accessibility mode is enabled
            property bool accessibilityEnabled: typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled

            Accessible.role: Accessible.Button
            // In accessibility mode, mention long press for ratio; otherwise mention double-tap
            Accessible.name: scaleWeightAccessible.text + MachineState.scaleWeight.toFixed(1) + " " + gramsAccessible.text + (isFlowScale ? " (estimated)" : "") + ". " +
                (accessibilityEnabled ? tapToTareHoldForRatioAccessible.text : tapToTareDoubleTapRatioAccessible.text)

            // Accessibility: Handle TalkBack double-tap directly (triggers tare)
            Accessible.onPressAction: {
                console.log("StatusBar: Accessible.onPressAction triggered (TalkBack double-tap)")
                if (MainController.brewByRatioActive) {
                    MainController.clearBrewByRatio()
                }
                MachineState.tareScale()
            }

            // Hidden Tr elements for accessible names
            Tr { id: scaleWeightAccessible; key: "statusbar.scale_weight"; fallback: "Scale weight: "; visible: false }
            Tr { id: gramsAccessible; key: "statusbar.grams"; fallback: "grams"; visible: false }
            Tr { id: tapToTareHoldForRatioAccessible; key: "statusbar.tap_tare_hold_ratio"; fallback: "Tap to tare, hold for ratio"; visible: false }
            Tr { id: tapToTareDoubleTapRatioAccessible; key: "statusbar.tap_tare_doubletap_ratio"; fallback: "Tap to tare, double-tap for ratio"; visible: false }

            Row {
                id: scaleRow
                spacing: Theme.spacingSmall

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.scaled(8)
                    height: Theme.scaled(8)
                    radius: Theme.scaled(4)
                    color: scaleMouseArea.pressed ? Theme.accentColor
                         : MainController.brewByRatioActive ? Theme.primaryColor
                         : parent.isFlowScale ? Theme.textSecondaryColor
                         : Theme.weightColor
                }

                Text {
                    text: {
                        var weight = MachineState.scaleWeight.toFixed(1)
                        var suffix = parent.isFlowScale ? "g~" : "g"
                        if (MainController.brewByRatioActive) {
                            return weight + suffix + " 1:" + MainController.brewByRatio.toFixed(1)
                        }
                        return weight + suffix
                    }
                    color: scaleMouseArea.pressed ? Theme.accentColor
                         : MainController.brewByRatioActive ? Theme.primaryColor
                         : parent.isFlowScale ? Theme.textSecondaryColor
                         : Theme.weightColor
                    font: Theme.bodyFont
                }
            }

            MouseArea {
                id: scaleMouseArea
                anchors.fill: parent
                anchors.margins: -Theme.spacingSmall  // Expand touch target
                cursorShape: Qt.PointingHandCursor

                property int tapCount: 0
                property var lastTapTime: 0
                property bool longPressTriggered: false

                onPressed: {
                    longPressTriggered = false
                    // Start long press timer only in accessibility mode
                    if (scaleIndicator.accessibilityEnabled && !MachineState.isFlowing) {
                        longPressTimer.start()
                    }
                }

                onReleased: {
                    longPressTimer.stop()
                    // If long press was triggered, don't process as tap
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
                    // If long press was triggered, ignore the click
                    if (longPressTriggered) {
                        return
                    }

                    // Don't allow ratio dialog during shot
                    if (MachineState.isFlowing) {
                        console.log("StatusBar: Tare during shot")
                        if (MainController.brewByRatioActive) {
                            MainController.clearBrewByRatio()
                        }
                        MachineState.tareScale()
                        return
                    }

                    var now = Date.now()
                    var timeSinceLast = now - lastTapTime
                    console.log("StatusBar: Weight tap, timeSinceLast=" + timeSinceLast + "ms")

                    if (timeSinceLast < 300) {
                        tapCount++
                    } else {
                        tapCount = 1
                    }
                    lastTapTime = now

                    console.log("StatusBar: tapCount=" + tapCount)

                    if (tapCount >= 2) {
                        tapCount = 0
                        singleTapTimer.stop()
                        // Double-tap: open ratio dialog
                        console.log("StatusBar: Double-tap detected, opening ratio dialog")
                        brewRatioDialog.open()
                    } else {
                        // Single tap: delay tare to distinguish from double-tap
                        singleTapTimer.restart()
                    }
                }
            }

            // Long press timer for accessibility mode - opens ratio dialog
            Timer {
                id: longPressTimer
                interval: 600  // 600ms for long press
                onTriggered: {
                    console.log("StatusBar: Long press detected (accessibility mode), opening ratio dialog")
                    scaleMouseArea.longPressTriggered = true
                    brewRatioDialog.open()
                }
            }

            Timer {
                id: singleTapTimer
                interval: 300
                onTriggered: {
                    console.log("StatusBar: Timer triggered, tapCount=" + scaleMouseArea.tapCount)
                    if (scaleMouseArea.tapCount === 1) {
                        console.log("StatusBar: Single tap confirmed, taring scale")
                        // Cancel ratio mode if active
                        if (MainController.brewByRatioActive) {
                            console.log("StatusBar: Cancelling brew-by-ratio mode")
                            MainController.clearBrewByRatio()
                        }
                        MachineState.tareScale()
                    }
                    scaleMouseArea.tapCount = 0
                }
            }
        }

        // Separator
        Rectangle {
            width: Theme.scaled(1)
            height: Theme.scaled(30)
            color: Theme.textSecondaryColor
            opacity: 0.3
        }

        // Connection indicator
        Row {
            spacing: Theme.spacingSmall

            Accessible.role: Accessible.Indicator
            Accessible.name: DE1Device.connected ? machineConnectedAccessible.text : machineDisconnectedAccessible.text

            // Hidden Tr elements for accessible names
            Tr { id: machineConnectedAccessible; key: "statusbar.machine_connected"; fallback: "Machine connected"; visible: false }
            Tr { id: machineDisconnectedAccessible; key: "statusbar.machine_disconnected"; fallback: "Machine disconnected"; visible: false }

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.scaled(10)
                height: Theme.scaled(10)
                radius: Theme.scaled(5)
                color: DE1Device.connected ? Theme.successColor : Theme.errorColor
            }

            Tr {
                key: DE1Device.connected ? "statusbar.online" : "statusbar.offline"
                fallback: DE1Device.connected ? "Online" : "Offline"
                color: DE1Device.connected ? Theme.successColor : Theme.textSecondaryColor
                font: Theme.bodyFont
            }
        }
    }

    // Brew settings dialog (temperature, dose, grind, ratio, yield)
    BrewDialog {
        id: brewRatioDialog
    }
}
