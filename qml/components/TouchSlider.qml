import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1

Item {
    id: root

    property color trackColor: Theme.surfaceColor
    property color progressColor: Theme.primaryColor
    property color handleColor: Theme.primaryColor

    // Slider properties exposed
    property real from: 0
    property real to: 100
    property real value: 0
    property real stepSize: 1
    property bool pressed: slider.pressed
    property real visualPosition: slider.visualPosition

    // Button configuration
    property bool showButtons: true
    property real buttonSize: Theme.scaled(44)

    // Accessibility
    property string accessibleName: ""
    property int decimals: stepSize < 1 ? 1 : 0
    property string suffix: ""

    Accessible.role: Accessible.Slider
    Accessible.name: (accessibleName ? accessibleName + " " : "") + value.toFixed(decimals) + suffix
    Accessible.focusable: true

    // Signals
    signal moved()

    implicitWidth: Theme.scaled(200)
    implicitHeight: Theme.scaled(60)

    RowLayout {
        anchors.fill: parent
        spacing: Theme.scaled(8)

        // Minus button
        Rectangle {
            id: minusButton
            visible: root.showButtons
            Layout.preferredWidth: root.buttonSize
            Layout.preferredHeight: root.buttonSize
            radius: height / 2
            color: minusTapHandler.pressed ? Qt.darker(Theme.surfaceColor, 1.3) : Theme.surfaceColor
            border.width: 1
            border.color: Theme.borderColor

            Accessible.role: Accessible.Button
            Accessible.name: TranslationManager.translate("touchslider.button.decrease", "Decrease") +
                             (root.accessibleName ? " " + root.accessibleName : "")
            Accessible.focusable: true

            Text {
                anchors.centerIn: parent
                text: "\u2212"  // minus sign
                font.pixelSize: Theme.scaled(24)
                font.bold: true
                color: root.value <= root.from ? Theme.textSecondaryColor : Theme.textColor
            }

            // Using TapHandler for better touch responsiveness
            TapHandler {
                id: minusTapHandler
                onTapped: {
                    var newVal = root.value - root.stepSize
                    if (newVal >= root.from) {
                        root.value = Math.round(newVal / root.stepSize) * root.stepSize
                        root.moved()
                    }
                }
                // Long press for continuous decrement
                longPressThreshold: 0.3
                onLongPressed: decrementTimer.start()
                onPressedChanged: {
                    if (!pressed) {
                        decrementTimer.stop()
                    }
                }
            }
            HoverHandler {
                cursorShape: Qt.PointingHandCursor
            }

            Timer {
                id: decrementTimer
                interval: 100
                repeat: true
                onTriggered: {
                    var newVal = root.value - root.stepSize
                    if (newVal >= root.from) {
                        root.value = Math.round(newVal / root.stepSize) * root.stepSize
                        root.moved()
                    }
                }
            }
        }

        // The actual slider
        Slider {
            id: slider
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.scaled(60)

            from: root.from
            to: root.to
            value: root.value
            stepSize: root.stepSize

            onMoved: {
                root.value = value
                root.moved()
            }

            onPressedChanged: root.pressedChanged()

            background: Item {
                x: slider.leftPadding
                y: slider.topPadding + slider.availableHeight / 2 - height / 2
                width: slider.availableWidth
                height: Theme.scaled(60)

                // Visual track (smaller than touch area)
                Rectangle {
                    anchors.centerIn: parent
                    width: parent.width
                    height: Theme.scaled(8)
                    radius: height / 2
                    color: root.trackColor

                    // Progress fill
                    Rectangle {
                        width: slider.visualPosition * parent.width
                        height: parent.height
                        radius: parent.radius
                        color: root.progressColor
                    }
                }
            }

            handle: Item {
                x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
                y: slider.topPadding + slider.availableHeight / 2 - height / 2
                width: Theme.scaled(60)
                height: Theme.scaled(60)

                Rectangle {
                    anchors.centerIn: parent
                    width: Theme.scaled(24)
                    height: Theme.scaled(24)
                    radius: width / 2
                    color: slider.pressed ? Qt.darker(root.handleColor, 1.2) : root.handleColor

                    Rectangle {
                        anchors.centerIn: parent
                        width: Theme.scaled(10)
                        height: Theme.scaled(10)
                        radius: width / 2
                        color: "white"
                        opacity: 0.3
                    }
                }
            }
        }

        // Plus button
        Rectangle {
            id: plusButton
            visible: root.showButtons
            Layout.preferredWidth: root.buttonSize
            Layout.preferredHeight: root.buttonSize
            radius: height / 2
            color: plusTapHandler.pressed ? Qt.darker(Theme.surfaceColor, 1.3) : Theme.surfaceColor
            border.width: 1
            border.color: Theme.borderColor

            Accessible.role: Accessible.Button
            Accessible.name: TranslationManager.translate("touchslider.button.increase", "Increase") +
                             (root.accessibleName ? " " + root.accessibleName : "")
            Accessible.focusable: true

            Text {
                anchors.centerIn: parent
                text: "+"
                font.pixelSize: Theme.scaled(24)
                font.bold: true
                color: root.value >= root.to ? Theme.textSecondaryColor : Theme.textColor
            }

            // Using TapHandler for better touch responsiveness
            TapHandler {
                id: plusTapHandler
                onTapped: {
                    var newVal = root.value + root.stepSize
                    if (newVal <= root.to) {
                        root.value = Math.round(newVal / root.stepSize) * root.stepSize
                        root.moved()
                    }
                }
                // Long press for continuous increment
                longPressThreshold: 0.3
                onLongPressed: incrementTimer.start()
                onPressedChanged: {
                    if (!pressed) {
                        incrementTimer.stop()
                    }
                }
            }
            HoverHandler {
                cursorShape: Qt.PointingHandCursor
            }

            Timer {
                id: incrementTimer
                interval: 100
                repeat: true
                onTriggered: {
                    var newVal = root.value + root.stepSize
                    if (newVal <= root.to) {
                        root.value = Math.round(newVal / root.stepSize) * root.stepSize
                        root.moved()
                    }
                }
            }
        }
    }
}
