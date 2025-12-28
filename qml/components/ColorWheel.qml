import QtQuick

Item {
    id: root

    // Current hue value (0-360) - auto-generates hueChanged signal
    property real hue: 0

    // Size of the wheel
    property real wheelSize: Math.min(width, height)
    property real ringWidth: wheelSize * 0.15
    property real outerRadius: wheelSize / 2
    property real innerRadius: outerRadius - ringWidth

    Canvas {
        id: wheelCanvas
        anchors.centerIn: parent
        width: root.wheelSize
        height: root.wheelSize

        onPaint: {
            var ctx = getContext("2d")
            var centerX = width / 2
            var centerY = height / 2

            ctx.clearRect(0, 0, width, height)

            // Draw the hue ring using segments
            var segments = 360
            for (var i = 0; i < segments; i++) {
                var startAngle = (i - 90) * Math.PI / 180
                var endAngle = (i + 1.5 - 90) * Math.PI / 180

                ctx.beginPath()
                ctx.arc(centerX, centerY, root.outerRadius - 1, startAngle, endAngle)
                ctx.arc(centerX, centerY, root.innerRadius + 1, endAngle, startAngle, true)
                ctx.closePath()

                // HSL to RGB for this hue
                ctx.fillStyle = Qt.hsla(i / 360, 1.0, 0.5, 1.0)
                ctx.fill()
            }

            // Draw inner and outer borders for polish
            ctx.beginPath()
            ctx.arc(centerX, centerY, root.outerRadius, 0, 2 * Math.PI)
            ctx.strokeStyle = Qt.rgba(0, 0, 0, 0.3)
            ctx.lineWidth = 1
            ctx.stroke()

            ctx.beginPath()
            ctx.arc(centerX, centerY, root.innerRadius, 0, 2 * Math.PI)
            ctx.stroke()
        }

        Component.onCompleted: requestPaint()
    }

    // Picker indicator
    Rectangle {
        id: picker
        width: root.ringWidth + 8
        height: root.ringWidth + 8
        radius: height / 2
        color: "transparent"
        border.color: "white"
        border.width: 3

        // Position on the ring based on hue
        property real angle: (root.hue - 90) * Math.PI / 180
        property real ringCenter: root.innerRadius + root.ringWidth / 2

        x: wheelCanvas.x + wheelCanvas.width / 2 + ringCenter * Math.cos(angle) - width / 2
        y: wheelCanvas.y + wheelCanvas.height / 2 + ringCenter * Math.sin(angle) - height / 2

        // Inner circle showing current color
        Rectangle {
            anchors.centerIn: parent
            width: parent.width - 6
            height: parent.height - 6
            radius: height / 2
            color: Qt.hsla(root.hue / 360, 1.0, 0.5, 1.0)
            border.color: Qt.rgba(0, 0, 0, 0.5)
            border.width: 1
        }

        // Shadow effect
        Rectangle {
            anchors.centerIn: parent
            width: parent.width + 2
            height: parent.height + 2
            radius: height / 2
            color: "transparent"
            border.color: Qt.rgba(0, 0, 0, 0.3)
            border.width: 2
            z: -1
        }
    }

    // Mouse/touch interaction
    MouseArea {
        anchors.fill: parent

        function updateHue(mouseX, mouseY) {
            var centerX = width / 2
            var centerY = height / 2
            var dx = mouseX - centerX
            var dy = mouseY - centerY
            var distance = Math.sqrt(dx * dx + dy * dy)

            // Check if within the ring area
            if (distance >= root.innerRadius - 10 && distance <= root.outerRadius + 10) {
                var angle = Math.atan2(dy, dx) * 180 / Math.PI + 90
                if (angle < 0) angle += 360
                root.hue = angle
            }
        }

        onPressed: function(mouse) {
            updateHue(mouse.x, mouse.y)
        }

        onPositionChanged: function(mouse) {
            if (pressed) {
                updateHue(mouse.x, mouse.y)
            }
        }
    }

    // Center area (can be used for saturation/value picker later)
    Rectangle {
        id: centerArea
        anchors.centerIn: wheelCanvas
        width: root.innerRadius * 2 - 10
        height: width
        radius: width / 2
        color: Theme.surfaceColor
        opacity: 0.8

        // Display current color preview
        Rectangle {
            anchors.centerIn: parent
            width: parent.width * 0.6
            height: width
            radius: width / 2
            color: Qt.hsla(root.hue / 360, 1.0, 0.5, 1.0)
            border.color: Theme.borderColor
            border.width: 2
        }
    }
}
