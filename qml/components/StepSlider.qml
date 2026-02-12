import QtQuick
import QtQuick.Controls

/**
 * StepSlider - A Slider that steps by stepSize when the track is clicked,
 * instead of jumping to the click position. Handle drag works normally.
 */
Item {
    id: root

    property real from: 0
    property real to: 100
    property real value: 0
    property real stepSize: 1
    property string accessibleName: ""

    signal moved()

    implicitHeight: slider.implicitHeight
    implicitWidth: 200

    Accessible.role: Accessible.Slider
    Accessible.name: root.accessibleName
    Accessible.focusable: true

    Slider {
        id: slider
        anchors.fill: parent
        from: root.from
        to: root.to
        value: root.value
        stepSize: root.stepSize
        Accessible.ignored: true

        onMoved: {
            root.value = value
            root.moved()
        }
    }

    // Overlay: intercept track clicks and step instead of jumping
    MouseArea {
        anchors.fill: parent

        onPressed: function(mouse) {
            var handleWidth = slider.handle && slider.handle.width > 0 ? slider.handle.width : Theme.scaled(28)
            var handleCenterX = slider.leftPadding + slider.visualPosition * (slider.availableWidth - handleWidth) + handleWidth / 2
            // If click is near the handle, let slider handle it for dragging
            if (Math.abs(mouse.x - handleCenterX) <= handleWidth / 2 + Theme.scaled(10)) {
                mouse.accepted = false
                return
            }
        }

        onClicked: function(mouse) {
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
