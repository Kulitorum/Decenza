import QtQuick
import QtQuick.Layouts
import QtQuick.Effects
import Decenza

// Horizontal phase journey visualization for espresso extraction.
// The active phase expands to show rich detail; completed/upcoming phases compact.
Item {
    id: root

    property int phase: 0
    property real shotTime: 0
    property string currentFrame: ""
    property real currentWeight: 0
    property real targetWeight: 36
    property real currentFlow: 0
    property real currentPressure: 0
    property real doseWeight: 0
    property real weightFlowRate: 0
    property real goalPressure: 0
    property real goalFlow: 0

    // Profile tracking — uses Theme.trackingColor() for consistent thresholds
    readonly property bool hasGoal: (goalPressure > 0 || goalFlow > 0) &&
        phase !== MachineStateType.Phase.EspressoPreheating
    readonly property color trackColor: {
        if (!hasGoal) return Theme.weightColor
        var isPressure = goalPressure > 0
        var delta = isPressure ? Math.abs(currentPressure - goalPressure)
                               : Math.abs(currentFlow - goalFlow)
        var goal = isPressure ? goalPressure : goalFlow
        return Theme.trackingColor(delta, goal, isPressure)
    }

    // Map machine phase enum to timeline index: 0=Preheat, 1=Pre-infusion, 2=Pouring, 3=Ending.
    // Returns -1 for non-espresso phases (shows 'waiting' state).
    readonly property int activeIndex: {
        switch (phase) {
            case MachineStateType.Phase.EspressoPreheating: return 0
            case MachineStateType.Phase.Preinfusion: return 1
            case MachineStateType.Phase.Pouring: return 2
            case MachineStateType.Phase.Ending: return 3
            default: return -1
        }
    }

    // ========== PROGRESS DOTS (top) ==========
    Row {
        id: progressDots
        anchors.top: parent.top
        anchors.topMargin: Theme.scaled(12)
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: Theme.scaled(6)

        Repeater {
            model: 4
            Rectangle {
                property int idx: index
                width: root.activeIndex === idx ? Theme.scaled(24) : Theme.scaled(8)
                height: Theme.scaled(8)
                radius: Theme.scaled(4)
                color: {
                    if (root.activeIndex === idx) return Theme.primaryColor
                    if (root.activeIndex > idx) return Theme.successColor
                    return Theme.borderColor
                }
                opacity: root.activeIndex > idx ? 0.6 : 1.0
                Behavior on width { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }
            }
        }
    }

    // Frame name subtitle (profile frame, not phase — phase is shown by EspressoPage pill)
    Text {
        id: frameSubtitle
        anchors.top: progressDots.bottom
        anchors.topMargin: Theme.scaled(12)
        anchors.horizontalCenter: parent.horizontalCenter
        text: root.currentFrame
        color: Theme.textSecondaryColor
        font: Theme.captionFont
        visible: root.currentFrame !== ""
    }

    // ========== ACTIVE PHASE CONTENT ==========
    Item {
        id: contentArea
        anchors.top: frameSubtitle.visible ? frameSubtitle.bottom : progressDots.bottom
        anchors.topMargin: Theme.scaled(12)
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spacingMedium
        anchors.left: parent.left
        anchors.right: parent.right

        // ---------- Preheat: pulsing temperature circle ----------
        Item {
            anchors.centerIn: parent
            width: Theme.scaled(140)
            height: Theme.scaled(140)
            visible: root.activeIndex === 0

            Rectangle {
                id: heatCircle
                anchors.centerIn: parent
                width: Theme.scaled(120)
                height: Theme.scaled(120)
                radius: width / 2
                color: "transparent"
                border.color: Theme.temperatureColor
                border.width: Theme.scaled(3)

                SequentialAnimation on border.width {
                    running: root.activeIndex === 0
                    loops: Animation.Infinite
                    NumberAnimation { to: Theme.scaled(6); duration: 1000; easing.type: Easing.InOutSine }
                    NumberAnimation { to: Theme.scaled(3); duration: 1000; easing.type: Easing.InOutSine }
                }

                SequentialAnimation on opacity {
                    running: root.activeIndex === 0
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.5; duration: 1000; easing.type: Easing.InOutSine }
                    NumberAnimation { to: 1.0; duration: 1000; easing.type: Easing.InOutSine }
                }

                // Rising heat lines
                Repeater {
                    model: 3
                    Rectangle {
                        width: Theme.scaled(2)
                        height: Theme.scaled(16)
                        radius: Theme.scaled(1)
                        color: Theme.temperatureColor
                        opacity: 0.6
                        x: heatCircle.width * 0.25 + index * heatCircle.width * 0.25 - width / 2
                        SequentialAnimation on y {
                            running: root.activeIndex === 0
                            loops: Animation.Infinite
                            NumberAnimation { from: heatCircle.height * 0.35; to: heatCircle.height * 0.15; duration: 800 + index * 200; easing.type: Easing.InOutSine }
                            NumberAnimation { from: heatCircle.height * 0.15; to: heatCircle.height * 0.35; duration: 800 + index * 200; easing.type: Easing.InOutSine }
                        }
                        SequentialAnimation on opacity {
                            running: root.activeIndex === 0
                            loops: Animation.Infinite
                            NumberAnimation { to: 0.2; duration: 800 + index * 200 }
                            NumberAnimation { to: 0.6; duration: 800 + index * 200 }
                        }
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                text: TranslationManager.translate("espresso.phase.heating", "Heating...")
                color: Theme.temperatureColor
                font.pixelSize: Theme.scaled(14)
            }
        }

        // ---------- Pre-infusion: dripping with pressure ----------
        Column {
            anchors.centerIn: parent
            spacing: Theme.scaled(16)
            visible: root.activeIndex === 1

            // Pressure gauge
            Item {
                anchors.horizontalCenter: parent.horizontalCenter
                width: Theme.scaled(120)
                height: Theme.scaled(120)

                // Circular track
                Rectangle {
                    anchors.centerIn: parent
                    width: parent.width
                    height: parent.height
                    radius: width / 2
                    color: "transparent"
                    border.color: Theme.borderColor
                    border.width: Theme.scaled(4)
                }

                // Pressure arc overlay
                Rectangle {
                    anchors.centerIn: parent
                    width: parent.width
                    height: parent.height
                    radius: width / 2
                    color: "transparent"
                    border.color: Theme.pressureColor
                    border.width: Theme.scaled(4)
                    opacity: Math.min(root.currentPressure / 12, 1.0)
                }

                // Center text
                Column {
                    anchors.centerIn: parent
                    spacing: Theme.scaled(2)

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: root.currentPressure.toFixed(1)
                        color: Theme.pressureColor
                        font.pixelSize: Theme.scaled(28)
                        font.weight: Font.Bold
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: TranslationManager.translate("espresso.unit.bar", "bar")
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(12)
                    }
                }
            }

            // Drip animation
            Item {
                anchors.horizontalCenter: parent.horizontalCenter
                width: Theme.scaled(40)
                height: Theme.scaled(30)
                clip: true

                Repeater {
                    model: 3
                    Rectangle {
                        width: Theme.scaled(4)
                        height: Theme.scaled(6)
                        radius: Theme.scaled(3)
                        color: Theme.flowColor
                        x: Theme.scaled(6) + index * Theme.scaled(12)
                        SequentialAnimation on y {
                            running: root.activeIndex === 1
                            loops: Animation.Infinite
                            PauseAnimation { duration: index * 300 }
                            NumberAnimation { from: 0; to: Theme.scaled(28); duration: 700; easing.type: Easing.InQuad }
                            NumberAnimation { from: 0; to: 0; duration: 0 }
                        }
                        SequentialAnimation on opacity {
                            running: root.activeIndex === 1
                            loops: Animation.Infinite
                            PauseAnimation { duration: index * 300 }
                            NumberAnimation { from: 0.8; to: 0; duration: 700 }
                            NumberAnimation { to: 0.8; duration: 0 }
                        }
                    }
                }
            }
        }

        // ---------- Pouring: the big one ----------
        Column {
            anchors.centerIn: parent
            spacing: Theme.scaled(12)
            visible: root.activeIndex === 2

            // Weight as the hero number
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: root.currentWeight.toFixed(1) + "g"
                color: root.trackColor
                font.pixelSize: Theme.scaled(48)
                font.weight: Font.Bold
            }

            // Target subtitle
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: TranslationManager.translate("espresso.timeline.of", "of") + " " + root.targetWeight.toFixed(0) + "g"
                color: Theme.textSecondaryColor
                font.pixelSize: Theme.scaled(16)
                visible: root.targetWeight > 0
            }

            // Wide progress bar
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: root.width * 0.6
                height: Theme.scaled(10)
                radius: Theme.scaled(5)
                color: Theme.surfaceColor

                Rectangle {
                    width: root.targetWeight > 0
                        ? Math.min(root.currentWeight / root.targetWeight, 1.0) * parent.width
                        : 0
                    height: parent.height
                    radius: Theme.scaled(5)
                    color: root.trackColor
                    Behavior on width { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }

                    // Glow at leading edge
                    Rectangle {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.scaled(16)
                        height: parent.height + Theme.scaled(4)
                        radius: height / 2
                        color: root.trackColor
                        opacity: 0.4
                        visible: parent.width > Theme.scaled(8)

                        SequentialAnimation on opacity {
                            running: root.activeIndex === 2
                            loops: Animation.Infinite
                            NumberAnimation { to: 0.7; duration: 600 }
                            NumberAnimation { to: 0.3; duration: 600 }
                        }
                    }
                }
            }

            // Percentage text
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: root.targetWeight > 0
                    ? Math.min(Math.round(root.currentWeight / root.targetWeight * 100), 100) + "%"
                    : ""
                color: Theme.textSecondaryColor
                font.pixelSize: Theme.scaled(14)
                visible: root.targetWeight > 0
            }

            // Live brew ratio and ETA
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Theme.scaled(20)
                visible: root.doseWeight > 0 || root.weightFlowRate > 0.1

                // Live brew ratio
                Column {
                    spacing: Theme.scaled(2)
                    visible: root.doseWeight > 0

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "1:" + (root.doseWeight > 0 ? (root.currentWeight / root.doseWeight).toFixed(1) : "0")
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(22)
                        font.weight: Font.Medium
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: TranslationManager.translate("espresso.timeline.ratio", "ratio")
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(11)
                    }
                }

                // Divider
                Rectangle {
                    width: Theme.scaled(1)
                    height: Theme.scaled(30)
                    color: Theme.borderColor
                    anchors.verticalCenter: parent.verticalCenter
                    visible: root.doseWeight > 0 && root.weightFlowRate > 0.1
                }

                // Estimated time remaining
                Column {
                    spacing: Theme.scaled(2)
                    visible: root.weightFlowRate > 0.1 && root.targetWeight > root.currentWeight

                    property real remaining: (root.targetWeight - root.currentWeight) / root.weightFlowRate

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "~" + Math.max(0, parent.remaining).toFixed(0) + "s"
                        color: Theme.textColor
                        font.pixelSize: Theme.scaled(22)
                        font.weight: Font.Medium
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: TranslationManager.translate("espresso.timeline.remaining", "remaining")
                        color: Theme.textSecondaryColor
                        font.pixelSize: Theme.scaled(11)
                    }
                }
            }
        }

        // ---------- Ending: completion ----------
        Column {
            anchors.centerIn: parent
            spacing: Theme.scaled(12)
            visible: root.activeIndex === 3

            Image {
                anchors.horizontalCenter: parent.horizontalCenter
                source: "qrc:/icons/tick.svg"
                sourceSize.width: Theme.scaled(48)
                sourceSize.height: Theme.scaled(48)
                opacity: 0

                layer.enabled: true
                layer.smooth: true
                layer.effect: MultiEffect {
                    colorization: 1.0
                    colorizationColor: Theme.successColor
                }

                NumberAnimation on opacity {
                    running: root.activeIndex === 3
                    from: 0; to: 1.0
                    duration: 600
                    easing.type: Easing.OutCubic
                }
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: root.currentWeight.toFixed(1) + "g"
                color: Theme.weightColor
                font.pixelSize: Theme.scaled(28)
                font.weight: Font.Bold
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: root.shotTime.toFixed(1) + "s"
                color: Theme.textSecondaryColor
                font.pixelSize: Theme.scaled(18)
            }
        }

        // ---------- Waiting state (before extraction starts) ----------
        Text {
            anchors.centerIn: parent
            text: TranslationManager.translate("espresso.timeline.waiting", "Waiting to start...")
            color: Theme.textSecondaryColor
            font.pixelSize: Theme.scaled(16)
            visible: root.activeIndex < 0
        }
    }

}
