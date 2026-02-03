import QtQuick
import QtQuick.Layouts

Item {
    id: root

    // The current color being edited
    property color color: Qt.hsla(hue / 360, saturation / 100, lightness / 100, 1.0)

    // HSL components (0-360 for hue, 0-100 for S/L)
    property real hue: 220
    property real saturation: 70
    property real lightness: 55

    // Set color from external source
    function setColor(c) {
        var h = c.hslHue * 360
        var s = c.hslSaturation * 100
        var l = c.hslLightness * 100
        if (h < 0) h = 0
        hue = h
        saturation = s
        lightness = l
    }

    RowLayout {
        anchors.fill: parent
        spacing: Theme.spacingMedium

        // Left side: Color wheel
        ColorWheel {
            id: colorWheel
            Layout.preferredWidth: Theme.scaled(130)
            Layout.preferredHeight: Theme.scaled(130)
            Layout.alignment: Qt.AlignVCenter
            hue: root.hue

            onHueChanged: {
                root.hue = colorWheel.hue
            }
        }

        // Right side: Sliders only - no labels, no preview
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacingSmall

            // Saturation slider
            Rectangle {
                id: satSlider
                Layout.fillWidth: true
                height: Theme.scaled(24)
                radius: Theme.scaled(12)

                Accessible.role: Accessible.Slider
                Accessible.name: TranslationManager.translate("colorEditor.saturation", "Saturation") + " " + Math.round(root.saturation) + "%"
                Accessible.focusable: true

                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: Qt.hsla(root.hue / 360, 0, root.lightness / 100, 1.0) }
                    GradientStop { position: 1.0; color: Qt.hsla(root.hue / 360, 1.0, root.lightness / 100, 1.0) }
                }

                border.color: Theme.borderColor
                border.width: 1

                Rectangle {
                    width: Theme.scaled(28)
                    height: Theme.scaled(28)
                    radius: Theme.scaled(14)
                    color: root.color
                    border.color: "white"
                    border.width: 2
                    x: (root.saturation / 100) * (parent.width - width)
                    y: (parent.height - height) / 2
                }

                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -8

                    function updateSat(mouseX) {
                        root.saturation = Math.max(0, Math.min(100, (mouseX / satSlider.width) * 100))
                    }

                    onPressed: function(mouse) { updateSat(mouse.x) }
                    onPositionChanged: function(mouse) { if (pressed) updateSat(mouse.x) }
                    onReleased: {
                        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
                            AccessibilityManager.announce(TranslationManager.translate("colorEditor.saturation", "Saturation") + " " + Math.round(root.saturation) + "%")
                        }
                    }
                }
            }

            // Lightness slider
            Rectangle {
                id: lightSlider
                Layout.fillWidth: true
                height: Theme.scaled(24)
                radius: Theme.scaled(12)

                Accessible.role: Accessible.Slider
                Accessible.name: TranslationManager.translate("colorEditor.lightness", "Lightness") + " " + Math.round(root.lightness) + "%"
                Accessible.focusable: true

                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: Qt.hsla(root.hue / 360, root.saturation / 100, 0, 1.0) }
                    GradientStop { position: 0.5; color: Qt.hsla(root.hue / 360, root.saturation / 100, 0.5, 1.0) }
                    GradientStop { position: 1.0; color: Qt.hsla(root.hue / 360, root.saturation / 100, 1.0, 1.0) }
                }

                border.color: Theme.borderColor
                border.width: 1

                Rectangle {
                    width: Theme.scaled(28)
                    height: Theme.scaled(28)
                    radius: Theme.scaled(14)
                    color: root.color
                    border.color: "white"
                    border.width: 2
                    x: (root.lightness / 100) * (parent.width - width)
                    y: (parent.height - height) / 2
                }

                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -8

                    function updateLight(mouseX) {
                        root.lightness = Math.max(0, Math.min(100, (mouseX / lightSlider.width) * 100))
                    }

                    onPressed: function(mouse) { updateLight(mouse.x) }
                    onPositionChanged: function(mouse) { if (pressed) updateLight(mouse.x) }
                    onReleased: {
                        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
                            AccessibilityManager.announce(TranslationManager.translate("colorEditor.lightness", "Lightness") + " " + Math.round(root.lightness) + "%")
                        }
                    }
                }
            }

            // Screen brightness slider
            Rectangle {
                id: brightnessSlider
                Layout.fillWidth: true
                height: Theme.scaled(24)
                radius: Theme.scaled(12)

                Accessible.role: Accessible.Slider
                Accessible.name: TranslationManager.translate("colorEditor.screenBrightness", "Screen brightness") + " " + Math.round(Settings.screenBrightness * 100) + "%"
                Accessible.focusable: true

                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: "#000000" }
                    GradientStop { position: 1.0; color: "#ffffff" }
                }

                border.color: Theme.borderColor
                border.width: 1

                Rectangle {
                    width: Theme.scaled(28)
                    height: Theme.scaled(28)
                    radius: Theme.scaled(14)
                    color: Qt.rgba(Settings.screenBrightness, Settings.screenBrightness, Settings.screenBrightness, 1.0)
                    border.color: "white"
                    border.width: 2
                    x: Settings.screenBrightness * (parent.width - width)
                    y: (parent.height - height) / 2
                }

                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -8

                    function updateBrightness(mouseX) {
                        var val = Math.max(0, Math.min(1, mouseX / brightnessSlider.width))
                        Settings.screenBrightness = val
                    }

                    onPressed: function(mouse) { updateBrightness(mouse.x) }
                    onPositionChanged: function(mouse) { if (pressed) updateBrightness(mouse.x) }
                    onReleased: {
                        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
                            AccessibilityManager.announce(TranslationManager.translate("colorEditor.screenBrightness", "Screen brightness") + " " + Math.round(Settings.screenBrightness * 100) + "%")
                        }
                    }
                }
            }

            Item { Layout.fillHeight: true }
        }
    }
}
