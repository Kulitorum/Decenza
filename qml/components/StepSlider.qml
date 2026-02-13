import QtQuick
import QtQuick.Controls
import DecenzaDE1

/**
 * StepSlider - A Slider that steps by stepSize when the track is clicked,
 * instead of jumping to the click position. Handle drag works normally.
 *
 * Uses a MouseArea that always accepts press events and handles both drag
 * and tap itself. A small movement threshold distinguishes taps from drags.
 * This avoids the fragile mouse.accepted=false pattern that breaks on macOS.
 */
Item {
    id: root

    property real from: 0
    property real to: 100
    property real value: 0
    property real stepSize: 1
    property string accessibleName: ""

    signal moved()

    implicitHeight: Math.max(slider.implicitHeight, Theme.touchTargetMin)
    implicitWidth: 200

    Accessible.role: Accessible.Slider
    Accessible.name: root.accessibleName
    Accessible.description: root.value.toFixed(root.stepSize < 1 ? 1 : 0)
    Accessible.focusable: true
    Accessible.onIncreaseAction: { slider.increase(); root.value = slider.value; root.moved() }
    Accessible.onDecreaseAction: { slider.decrease(); root.value = slider.value; root.moved() }

    // Slider is visual-only; the MouseArea handles all input
    Slider {
        id: slider
        anchors.fill: parent
        from: root.from
        to: root.to
        value: root.value
        stepSize: root.stepSize
        snapMode: Slider.SnapAlways
        Accessible.ignored: true
    }

    MouseArea {
        anchors.fill: parent
        preventStealing: isDrag

        property real pressX: 0
        property real pressY: 0
        property bool isDrag: false

        Accessible.ignored: true

        onPressed: function(mouse) {
            pressX = mouse.x
            pressY = mouse.y
            isDrag = false
        }

        onPositionChanged: function(mouse) {
            var deltaX = Math.abs(mouse.x - pressX)
            var deltaY = Math.abs(mouse.y - pressY)
            if (!isDrag && deltaX > Theme.scaled(4)) {
                if (deltaX > deltaY * 1.5) {
                    isDrag = true
                }
            }
            if (isDrag) {
                var trackWidth = slider.availableWidth
                if (trackWidth <= 0) return
                var fraction = (mouse.x - slider.leftPadding) / trackWidth
                fraction = Math.max(0, Math.min(1, fraction))
                var newValue = slider.from + fraction * (slider.to - slider.from)
                if (root.stepSize > 0) {
                    newValue = Math.round(newValue / root.stepSize) * root.stepSize
                }
                newValue = Math.max(slider.from, Math.min(slider.to, newValue))
                slider.value = newValue
                root.value = newValue
                root.moved()
            }
        }

        onReleased: function(mouse) {
            if (!isDrag) {
                // Tap (no drag) — step by one stepSize in tapped direction
                var handleWidth = slider.handle && slider.handle.width > 0 ? slider.handle.width : Theme.scaled(28)
                var handleCenterX = slider.leftPadding + slider.visualPosition * (slider.availableWidth - handleWidth) + handleWidth / 2
                if (mouse.x < handleCenterX) {
                    slider.decrease()
                } else {
                    slider.increase()
                }
                root.value = slider.value
                root.moved()
            }
        }
    }
}
