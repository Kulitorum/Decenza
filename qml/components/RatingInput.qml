import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1

FocusScope {
    id: root

    property int value: 0
    property string accessibleName: ""
    property bool compact: false  // true = 2x2 grid presets, false = single row

    signal valueModified(int newValue)

    implicitHeight: Theme.scaled(40)

    // Fix #3: activeFocusOnTab for keyboard tab navigation
    activeFocusOnTab: true

    Accessible.role: Accessible.Slider
    Accessible.name: (accessibleName ? accessibleName + " " : "") + value + "%"
    Accessible.focusable: true
    // Fix #4: description explaining interaction methods
    Accessible.description: TranslationManager.translate("ratinginput.accessibility.description",
        "Use preset buttons or drag slider to set rating")

    // Fix #1: keyboard navigation
    Keys.onLeftPressed: adjustValue(-1)
    Keys.onRightPressed: adjustValue(1)
    Keys.onUpPressed: adjustValue(1)
    Keys.onDownPressed: adjustValue(-1)
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_PageUp) {
            adjustValue(25)
            event.accepted = true
        } else if (event.key === Qt.Key_PageDown) {
            adjustValue(-25)
            event.accepted = true
        }
    }

    function adjustValue(step) {
        var newVal = Math.max(0, Math.min(100, root.value + step))
        if (newVal !== root.value) {
            root.value = newVal
            root.valueModified(newVal)
            if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
                AccessibilityManager.announce(newVal + "%")
            }
        }
    }

    // Color interpolation: 0%=red, 50%=yellow, 100%=green
    function ratingColor(val) {
        var t = Math.max(0, Math.min(100, val)) / 100.0
        var r, g, b
        var u
        if (t <= 0.5) {
            // red (#ff4444) -> yellow (#ffaa00)
            u = t / 0.5
            r = 1.0
            g = 0.267 + u * (0.667 - 0.267)  // 0x44/0xff -> 0xaa/0xff
            b = 0.267 * (1 - u)                // 0x44/0xff -> 0
        } else {
            // yellow (#ffaa00) -> green (#00cc6d)
            u = (t - 0.5) / 0.5
            r = 1.0 * (1 - u)                  // 0xff -> 0x00
            g = 0.667 + u * (0.8 - 0.667)      // 0xaa/0xff -> 0xcc/0xff
            b = u * 0.427                       // 0x00 -> 0x6d/0xff
        }
        return Qt.rgba(r, g, b, 1.0)
    }

    readonly property color currentColor: ratingColor(root.value)

    RowLayout {
        anchors.fill: parent
        spacing: Theme.scaled(6)

        // Preset buttons
        // Fix #2: wrapped in FocusScope with KeyNavigation
        Grid {
            id: presetGrid
            columns: root.compact ? 2 : 4
            spacing: Theme.scaled(root.compact ? 3 : 6)

            Repeater {
                model: [25, 50, 75, 100]

                Rectangle {
                    id: presetPill
                    width: Theme.scaled(root.compact ? 32 : 36)
                    height: Theme.scaled(root.compact ? 18 : 28)

                    readonly property bool isActive: root.value === modelData
                    radius: Theme.scaled(root.compact ? 4 : 6)
                    color: isActive ? ratingColor(modelData) : Theme.surfaceColor
                    border.width: 1
                    border.color: isActive ? ratingColor(modelData) : Theme.borderColor

                    Behavior on color { ColorAnimation { duration: 150 } }

                    Accessible.role: Accessible.Button
                    Accessible.name: modelData + "%"
                    Accessible.focusable: true
                    Accessible.onPressAction: ratingArea.clicked(null)

                    Text {
                        anchors.centerIn: parent
                        text: modelData
                        font.pixelSize: Theme.scaled(root.compact ? 10 : 12)
                        font.bold: true
                        color: presetPill.isActive ? "#ffffff" : ratingColor(modelData)
                        Accessible.ignored: true
                    }

                    MouseArea {
                        id: ratingArea
                        anchors.fill: parent
                        onClicked: {
                            root.value = modelData
                            root.valueModified(modelData)
                            if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
                                AccessibilityManager.announce(modelData + "%")
                            }
                        }
                    }
                }
            }
        }

        // Slider
        Slider {
            id: slider
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.scaled(40)
            // Fix #2: KeyNavigation from slider back to root
            KeyNavigation.backtab: root

            from: 0
            to: 100
            stepSize: 1

            // Use Binding element so the binding survives slider drag
            Binding on value {
                value: root.value
                restoreMode: Binding.RestoreBindingOrValue
            }

            onMoved: {
                root.value = Math.round(value)
                root.valueModified(root.value)
            }

            background: Item {
                x: slider.leftPadding
                y: slider.topPadding + slider.availableHeight / 2 - height / 2
                width: slider.availableWidth
                height: Theme.scaled(40)

                // Full gradient track (faint)
                Rectangle {
                    anchors.centerIn: parent
                    width: parent.width
                    height: Theme.scaled(6)
                    radius: height / 2
                    opacity: 0.25
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: "#ff4444" }
                        GradientStop { position: 0.5; color: "#ffaa00" }
                        GradientStop { position: 1.0; color: "#00cc6d" }
                    }
                }

                // Filled gradient track (full opacity up to current position)
                Item {
                    anchors.verticalCenter: parent.verticalCenter
                    width: slider.visualPosition * parent.width
                    height: Theme.scaled(6)
                    clip: true

                    Rectangle {
                        width: slider.availableWidth
                        height: parent.height
                        radius: height / 2
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0.0; color: "#ff4444" }
                            GradientStop { position: 0.5; color: "#ffaa00" }
                            GradientStop { position: 1.0; color: "#00cc6d" }
                        }
                    }
                }
            }

            handle: Item {
                x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
                y: slider.topPadding + slider.availableHeight / 2 - height / 2
                width: Theme.scaled(40)
                height: Theme.scaled(40)

                Rectangle {
                    anchors.centerIn: parent
                    width: Theme.scaled(20)
                    height: Theme.scaled(20)
                    radius: width / 2
                    color: slider.pressed ? Qt.darker(root.currentColor, 1.2) : root.currentColor
                    border.width: 2
                    border.color: Qt.lighter(root.currentColor, 1.3)
                }
            }
        }

        // Value label
        Text {
            Layout.preferredWidth: Theme.scaled(40)
            horizontalAlignment: Text.AlignRight
            text: root.value + "%"
            font.pixelSize: Theme.scaled(14)
            font.bold: true
            color: root.currentColor
        }
    }
}
