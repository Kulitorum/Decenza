// The RGB/HLS mode Repeater and the three slider rows read this file's `root` id; Bound
// makes it statically resolvable. Each declares every injected role it uses required in
// the same edit -- without that, Bound stops role injection and all three sliders
// collapse onto channel `undefined` at RUNTIME, silently.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Decenza

Item {
    id: root

    // The current color being edited. Updated imperatively (not as a derived
    // binding) to avoid a binding loop: setColor() writes the internal
    // representations, and the consumer's onColorChanged handler can route
    // back through setColor() — Qt's loop detector flags the synchronous
    // re-evaluation chain even though the cycle is broken by external guards.
    property color color: "#4682E6"

    // RGB components (0-255)
    property real _r: 70
    property real _g: 130
    property real _b: 230

    // HSL components (0-360 for H, 0-100 for S/L)
    property real _h: 220
    property real _s: 70
    property real _l: 55

    // Mode: true = RGB, false = HLS
    property bool _rgbMode: false

    // Guard against feedback loops during setColor
    property bool _updating: false

    function _recomputeColor() {
        color = _rgbMode
            ? Qt.rgba(_r / 255, _g / 255, _b / 255, 1.0)
            : Qt.hsla(_h / 360, _s / 100, _l / 100, 1.0)
    }

    function setColor(c) {
        if (typeof c === 'string') c = Qt.color(c)
        _updating = true
        _r = Math.round(c.r * 255)
        _g = Math.round(c.g * 255)
        _b = Math.round(c.b * 255)
        var h = c.hslHue * 360
        if (h < 0) h = 0
        _h = h
        _s = c.hslSaturation * 100
        _l = c.hslLightness * 100
        _updating = false
        color = c
    }

    // Sync the other representation when sliders change
    function _syncFromRgb() {
        if (_updating) return
        _updating = true
        var c = Qt.rgba(_r / 255, _g / 255, _b / 255, 1.0)
        var h = c.hslHue * 360
        if (h < 0) h = 0
        _h = h
        _s = c.hslSaturation * 100
        _l = c.hslLightness * 100
        _updating = false
    }

    function _syncFromHsl() {
        if (_updating) return
        _updating = true
        var c = Qt.hsla(_h / 360, _s / 100, _l / 100, 1.0)
        _r = Math.round(c.r * 255)
        _g = Math.round(c.g * 255)
        _b = Math.round(c.b * 255)
        _updating = false
    }

    on_RChanged: if (_rgbMode) _syncFromRgb()
    on_GChanged: if (_rgbMode) _syncFromRgb()
    on_BChanged: if (_rgbMode) _syncFromRgb()
    on_HChanged: if (!_rgbMode) _syncFromHsl()
    on_SChanged: if (!_rgbMode) _syncFromHsl()
    on_LChanged: if (!_rgbMode) _syncFromHsl()

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.scaled(6)

        // RGB / HLS toggle
        Rectangle {
            implicitWidth: modeRow.implicitWidth
            implicitHeight: Theme.scaled(28)
            radius: Theme.buttonRadius
            color: "transparent"
            border.color: Theme.borderColor
            border.width: 1
            clip: true

            Row {
                id: modeRow
                anchors.fill: parent

                Repeater {
                    model: [
                        { value: true, label: "RGB" },
                        { value: false, label: "HLS" }
                    ]

                    Rectangle {
                        id: modeButton
                        required property var modelData
                        required property int index

                        width: modeLabel.implicitWidth + Theme.scaled(20)
                        height: parent.height
                        color: root._rgbMode === modeButton.modelData.value ? Theme.primaryColor : Theme.surfaceColor

                        Rectangle {
                            visible: modeButton.index > 0
                            anchors.left: parent.left
                            width: 1
                            height: parent.height
                            color: Theme.borderColor
                        }

                        Text {
                            id: modeLabel
                            text: modeButton.modelData.label
                            color: root._rgbMode === modeButton.modelData.value ? Theme.primaryContrastColor : Theme.textColor
                            font: Theme.labelFont
                            anchors.centerIn: parent
                            Accessible.ignored: true
                        }

                        Accessible.role: Accessible.Button
                        Accessible.name: modeButton.modelData.label
                        Accessible.focusable: true
                        Accessible.onPressAction: modeTap.clicked(null)

                        MouseArea {
                            id: modeTap
                            anchors.fill: parent
                            onClicked: root._rgbMode = modeButton.modelData.value
                        }
                    }
                }
            }
        }

        // Slider rows (static model — values bound inside delegate to avoid re-creation)
        Repeater {
            model: 3

            RowLayout {
                id: sliderRow
                required property int index

                Layout.fillWidth: true
                spacing: Theme.scaled(6)

                readonly property string label: root._rgbMode
                    ? ["R","G","B"][sliderRow.index] : ["H","L","S"][sliderRow.index]
                readonly property real maxVal: root._rgbMode
                    ? 255 : [360, 100, 100][sliderRow.index]
                readonly property real currentVal: root._rgbMode
                    ? [root._r, root._g, root._b][sliderRow.index]
                    : [root._h, root._l, root._s][sliderRow.index]

                Text {
                    text: sliderRow.label
                    color: Theme.textSecondaryColor
                    font: Theme.labelFont
                    Layout.preferredWidth: Theme.scaled(14)
                    horizontalAlignment: Text.AlignRight
                }

                Rectangle {
                    id: sliderTrack
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(20)
                    radius: Theme.scaled(10)
                    border.color: Theme.borderColor
                    border.width: 1

                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: root._sliderColor(sliderRow.index, 0) }
                        GradientStop { position: 0.25; color: root._sliderColor(sliderRow.index, 0.25) }
                        GradientStop { position: 0.5; color: root._sliderColor(sliderRow.index, 0.5) }
                        GradientStop { position: 0.75; color: root._sliderColor(sliderRow.index, 0.75) }
                        GradientStop { position: 1.0; color: root._sliderColor(sliderRow.index, 1.0) }
                    }

                    Rectangle {
                        width: Theme.scaled(22)
                        height: Theme.scaled(22)
                        radius: Theme.scaled(11)
                        color: root.color
                        border.color: Theme.primaryContrastColor
                        border.width: 2
                        x: (sliderRow.currentVal / sliderRow.maxVal) * (parent.width - width)
                        y: (parent.height - height) / 2
                    }

                    // Screen readers get a real slider here: the drag MouseArea alone is
                    // invisible to TalkBack/VoiceOver, and this is the only way to set a
                    // channel value in the theme editor.
                    Accessible.role: Accessible.Slider
                    // QQuickAccessibleAttached has no `value` property (only role, name,
                    // description, id, ignored, labelledBy, labelFor plus the bool states),
                    // so the current channel value goes in the name.
                    Accessible.name: TranslationManager.translate("colorEditor.accessible.channel",
                                                                  "Color channel %1")
                                     .arg(sliderRow.label) + ", " + Math.round(sliderRow.currentVal)
                    Accessible.focusable: true
                    Accessible.onIncreaseAction: root._setChannel(
                        sliderRow.index, Math.min(sliderRow.maxVal, Math.round(sliderRow.currentVal) + 1))
                    Accessible.onDecreaseAction: root._setChannel(
                        sliderRow.index, Math.max(0, Math.round(sliderRow.currentVal) - 1))

                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -8
                        preventStealing: true

                        function update(mouseX) {
                            var ratio = Math.max(0, Math.min(1, mouseX / sliderTrack.width))
                            root._setChannel(sliderRow.index, Math.round(ratio * sliderRow.maxVal))
                        }

                        onPressed: function(mouse) { update(mouse.x) }
                        onPositionChanged: function(mouse) { if (pressed) update(mouse.x) }
                    }
                }

                Text {
                    text: Math.round(sliderRow.currentVal)
                    color: Theme.textColor
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.labelFont.pixelSize
                    Layout.preferredWidth: Theme.scaled(30)
                    horizontalAlignment: Text.AlignRight
                }
            }
        }
    }

    // Compute gradient color for a slider at a given position (0-1)
    function _sliderColor(channelIndex, pos) {
        if (_rgbMode) {
            var r = _r, g = _g, b = _b
            if (channelIndex === 0) r = pos * 255
            else if (channelIndex === 1) g = pos * 255
            else b = pos * 255
            return Qt.rgba(r / 255, g / 255, b / 255, 1.0)
        } else {
            var h = _h, l = _l, s = _s
            if (channelIndex === 0) h = pos * 360
            else if (channelIndex === 1) l = pos * 100
            else s = pos * 100
            return Qt.hsla(h / 360, s / 100, l / 100, 1.0)
        }
    }

    // Set a channel value by index
    function _setChannel(channelIndex, val) {
        if (_rgbMode) {
            if (channelIndex === 0) _r = val
            else if (channelIndex === 1) _g = val
            else _b = val
        } else {
            if (channelIndex === 0) _h = val
            else if (channelIndex === 1) _l = val
            else _s = val
        }
        _recomputeColor()
    }
}
