import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DE1App

Item {
    id: root

    // Value properties
    property real value: 0
    property real from: 0
    property real to: 100
    property real stepSize: 1
    property int decimals: stepSize < 1 ? 1 : 0

    // Display
    property string suffix: ""
    property string displayText: ""  // Optional override for value display
    property color valueColor: Theme.textColor
    property color accentColor: Theme.primaryColor

    // Signals - emits the new value for parent to apply
    signal valueModified(real newValue)

    implicitWidth: Theme.scaled(120)
    implicitHeight: Theme.scaled(56)

    // Compact value display
    Rectangle {
        id: valueDisplay
        anchors.fill: parent
        radius: Theme.scaled(12)
        color: Theme.surfaceColor
        border.width: 1
        border.color: Theme.borderColor

        RowLayout {
            anchors.fill: parent
            anchors.margins: Theme.scaled(4)
            spacing: Theme.scaled(2)

            // Minus button
            Rectangle {
                Layout.preferredWidth: Theme.scaled(40)
                Layout.fillHeight: true
                radius: Theme.scaled(8)
                color: minusArea.pressed ? Qt.darker(Theme.surfaceColor, 1.3) : "transparent"

                Text {
                    anchors.centerIn: parent
                    text: "\u2212"
                    font.pixelSize: Theme.scaled(22)
                    font.bold: true
                    color: root.value <= root.from ? Theme.textSecondaryColor : Theme.textColor
                }

                MouseArea {
                    id: minusArea
                    anchors.fill: parent
                    onClicked: adjustValue(-1)
                    onPressAndHold: decrementTimer.start()
                    onReleased: decrementTimer.stop()
                    onCanceled: decrementTimer.stop()
                }

                Timer {
                    id: decrementTimer
                    interval: 80
                    repeat: true
                    onTriggered: adjustValue(-1)
                }
            }

            // Value display - drag to adjust, tap to open scrubber
            Item {
                id: valueContainer
                Layout.fillWidth: true
                Layout.fillHeight: true

                Text {
                    id: valueText
                    anchors.centerIn: parent
                    text: root.displayText || (root.value.toFixed(root.decimals) + root.suffix)
                    font: Theme.headingFont
                    color: valueDragArea.isDragging ? root.accentColor : root.valueColor
                    opacity: valueDragArea.isDragging ? 0.3 : 1.0
                    Behavior on opacity { NumberAnimation { duration: 100 } }
                }

                MouseArea {
                    id: valueDragArea
                    anchors.fill: parent

                    property real startX: 0
                    property real startY: 0
                    property real currentX: 0
                    property real lastTime: 0
                    property bool isDragging: false

                    drag.target: Item {}  // Enable drag detection
                    drag.axis: Drag.XAndYAxis
                    drag.threshold: 5

                    onPressed: function(mouse) {
                        startX = mouse.x
                        startY = mouse.y
                        currentX = mouse.x
                        lastTime = Date.now()
                        isDragging = false
                    }

                    onPositionChanged: function(mouse) {
                        currentX = mouse.x
                        var deltaX = mouse.x - startX
                        var deltaY = startY - mouse.y  // Inverted: up = increase

                        // Use whichever axis has more movement
                        var delta = Math.abs(deltaX) > Math.abs(deltaY) ? deltaX : deltaY

                        if (Math.abs(delta) > drag.threshold) {
                            isDragging = true
                        }

                        if (isDragging) {
                            var now = Date.now()
                            var dt = Math.max(now - lastTime, 1)
                            var velocity = Math.abs(delta) / dt  // pixels per ms

                            // Accelerate based on velocity squared (softer gearing)
                            var multiplier = 1 + velocity * velocity * 20
                            var effectiveDelta = delta * multiplier

                            var steps = Math.round(effectiveDelta / 20)
                            if (steps !== 0) {
                                adjustValue(steps)
                            }

                            startX = mouse.x
                            startY = mouse.y
                            lastTime = now
                        }
                    }

                    onReleased: {
                        if (!isDragging) {
                            scrubberPopup.open()
                        }
                        isDragging = false
                    }

                    onCanceled: {
                        isDragging = false
                    }
                }

                // Anchor point for bubble positioning (centered)
                Item {
                    id: bubbleAnchor
                    anchors.centerIn: parent
                    width: 1
                    height: 1
                }

                // Floating speech bubble - rendered in overlay to be always on top
                Loader {
                    id: bubbleLoader
                    active: valueDragArea.isDragging
                    sourceComponent: Item {
                        id: speechBubble
                        parent: Overlay.overlay
                        visible: valueDragArea.isDragging

                        // Calculate luminance to determine text color
                        function getContrastColor(c) {
                            var luminance = 0.299 * c.r + 0.587 * c.g + 0.114 * c.b
                            return luminance > 0.5 ? "#000000" : "#FFFFFF"
                        }

                        property point globalPos: bubbleAnchor.mapToGlobal(0, 0)
                        x: globalPos.x - width / 2
                        y: globalPos.y - height - Theme.scaled(15)
                        width: bubbleRect.width
                        height: bubbleRect.height + bubbleTail.height - Theme.scaled(3)

                        // Pop-in animation
                        scale: valueDragArea.isDragging ? 1.0 : 0.5
                        opacity: valueDragArea.isDragging ? 1.0 : 0
                        transformOrigin: Item.Bottom
                        Behavior on scale { NumberAnimation { duration: 150; easing.type: Easing.OutBack; easing.overshoot: 2 } }
                        Behavior on opacity { NumberAnimation { duration: 100 } }

                        // Bubble body - 1.5x larger
                        Rectangle {
                            id: bubbleRect
                            width: bubbleText.width + Theme.scaled(36)
                            height: Theme.scaled(66)
                            radius: height / 2
                            color: root.valueColor

                            // Subtle gradient shine
                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: Theme.scaled(3)
                                radius: parent.radius - Theme.scaled(3)
                                gradient: Gradient {
                                    GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.3) }
                                    GradientStop { position: 0.5; color: Qt.rgba(1, 1, 1, 0) }
                                }
                            }

                            Text {
                                id: bubbleText
                                anchors.centerIn: parent
                                text: root.displayText || (root.value.toFixed(root.decimals) + root.suffix)
                                font.pixelSize: Theme.scaled(30)
                                font.bold: true
                                color: speechBubble.getContrastColor(root.valueColor)
                            }
                        }

                        // Cartoon tail (triangle pointing down)
                        Canvas {
                            id: bubbleTail
                            anchors.horizontalCenter: bubbleRect.horizontalCenter
                            anchors.top: bubbleRect.bottom
                            anchors.topMargin: -Theme.scaled(3)
                            width: Theme.scaled(30)
                            height: Theme.scaled(21)

                            onPaint: {
                                var ctx = getContext("2d")
                                ctx.reset()
                                ctx.fillStyle = root.valueColor
                                ctx.beginPath()
                                ctx.moveTo(0, 0)
                                ctx.lineTo(width, 0)
                                ctx.lineTo(width / 2, height)
                                ctx.closePath()
                                ctx.fill()
                            }

                            // Redraw when color changes
                            Connections {
                                target: root
                                function onValueColorChanged() { bubbleTail.requestPaint() }
                            }
                            Component.onCompleted: requestPaint()
                        }
                    }
                }
            }

            // Plus button
            Rectangle {
                Layout.preferredWidth: Theme.scaled(40)
                Layout.fillHeight: true
                radius: Theme.scaled(8)
                color: plusArea.pressed ? Qt.darker(Theme.surfaceColor, 1.3) : "transparent"

                Text {
                    anchors.centerIn: parent
                    text: "+"
                    font.pixelSize: Theme.scaled(22)
                    font.bold: true
                    color: root.value >= root.to ? Theme.textSecondaryColor : Theme.textColor
                }

                MouseArea {
                    id: plusArea
                    anchors.fill: parent
                    onClicked: adjustValue(1)
                    onPressAndHold: incrementTimer.start()
                    onReleased: incrementTimer.stop()
                    onCanceled: incrementTimer.stop()
                }

                Timer {
                    id: incrementTimer
                    interval: 80
                    repeat: true
                    onTriggered: adjustValue(1)
                }
            }
        }
    }

    // Full-width popup with blur - same as compact but bigger
    Popup {
        id: scrubberPopup
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: parent.width
        height: parent.height
        modal: true
        dim: false
        closePolicy: Popup.CloseOnPressOutside

        background: Rectangle {
            color: "#80000000"
        }

        // Content
        Item {
            anchors.fill: parent

            // Tap outside to close
            MouseArea {
                anchors.fill: parent
                onClicked: scrubberPopup.close()
            }

            // Full-width value control - same as compact but larger
            Rectangle {
                id: popupControl
                anchors.centerIn: parent
                width: parent.width - Theme.scaled(40)
                height: Theme.scaled(80)
                radius: Theme.scaled(16)
                color: Theme.surfaceColor
                border.width: 1
                border.color: Theme.borderColor

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(6)
                    spacing: Theme.scaled(4)

                    // Minus button
                    Rectangle {
                        Layout.preferredWidth: Theme.scaled(70)
                        Layout.fillHeight: true
                        radius: Theme.scaled(12)
                        color: popupMinusArea.pressed ? Qt.darker(Theme.surfaceColor, 1.3) : "transparent"

                        Text {
                            anchors.centerIn: parent
                            text: "\u2212"
                            font.pixelSize: Theme.scaled(32)
                            font.bold: true
                            color: root.value <= root.from ? Theme.textSecondaryColor : Theme.textColor
                        }

                        MouseArea {
                            id: popupMinusArea
                            anchors.fill: parent
                            onClicked: adjustValue(-1)
                            onPressAndHold: popupDecrementTimer.start()
                            onReleased: popupDecrementTimer.stop()
                            onCanceled: popupDecrementTimer.stop()
                        }

                        Timer {
                            id: popupDecrementTimer
                            interval: 80
                            repeat: true
                            onTriggered: adjustValue(-1)
                        }
                    }

                    // Value display - draggable
                    Item {
                        id: popupValueContainer
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        Text {
                            anchors.centerIn: parent
                            text: root.displayText || (root.value.toFixed(root.decimals) + root.suffix)
                            font.pixelSize: Theme.scaled(40)
                            font.bold: true
                            color: root.valueColor
                            opacity: popupDragArea.isDragging ? 0.3 : 1.0
                            Behavior on opacity { NumberAnimation { duration: 100 } }
                        }

                        MouseArea {
                            id: popupDragArea
                            anchors.fill: parent

                            property real startX: 0
                            property real startY: 0
                            property real currentX: 0
                            property real lastTime: 0
                            property bool isDragging: false

                            onPressed: function(mouse) {
                                startX = mouse.x
                                startY = mouse.y
                                currentX = mouse.x
                                lastTime = Date.now()
                                isDragging = false
                            }

                            onPositionChanged: function(mouse) {
                                currentX = mouse.x
                                var deltaX = mouse.x - startX
                                var deltaY = startY - mouse.y
                                var delta = Math.abs(deltaX) > Math.abs(deltaY) ? deltaX : deltaY

                                if (Math.abs(delta) > 5) {
                                    isDragging = true
                                }

                                if (isDragging) {
                                    var now = Date.now()
                                    var dt = Math.max(now - lastTime, 1)
                                    var velocity = Math.abs(delta) / dt  // pixels per ms

                                    // Accelerate based on velocity squared (softer gearing)
                                    var multiplier = 1 + velocity * velocity * 20
                                    var effectiveDelta = delta * multiplier

                                    var steps = Math.round(effectiveDelta / 20)
                                    if (steps !== 0) {
                                        adjustValue(steps)
                                    }

                                    startX = mouse.x
                                    startY = mouse.y
                                    lastTime = now
                                }
                            }

                            onReleased: {
                                isDragging = false
                            }

                            onCanceled: {
                                isDragging = false
                            }
                        }

                        // Anchor for popup bubble
                        Item {
                            id: popupBubbleAnchor
                            anchors.centerIn: parent
                            width: 1
                            height: 1
                        }

                        // Speech bubble for popup
                        Loader {
                            active: popupDragArea.isDragging
                            sourceComponent: Item {
                                parent: Overlay.overlay
                                visible: popupDragArea.isDragging

                                function getContrastColor(c) {
                                    var luminance = 0.299 * c.r + 0.587 * c.g + 0.114 * c.b
                                    return luminance > 0.5 ? "#000000" : "#FFFFFF"
                                }

                                property point globalPos: popupBubbleAnchor.mapToGlobal(0, 0)
                                x: globalPos.x - width / 2
                                y: globalPos.y - height - Theme.scaled(15)
                                width: popupBubbleRect.width
                                height: popupBubbleRect.height + popupBubbleTail.height - Theme.scaled(3)

                                scale: popupDragArea.isDragging ? 1.0 : 0.5
                                opacity: popupDragArea.isDragging ? 1.0 : 0
                                transformOrigin: Item.Bottom
                                Behavior on scale { NumberAnimation { duration: 150; easing.type: Easing.OutBack; easing.overshoot: 2 } }
                                Behavior on opacity { NumberAnimation { duration: 100 } }

                                Rectangle {
                                    id: popupBubbleRect
                                    width: popupBubbleText.width + Theme.scaled(36)
                                    height: Theme.scaled(66)
                                    radius: height / 2
                                    color: root.valueColor

                                    Rectangle {
                                        anchors.fill: parent
                                        anchors.margins: Theme.scaled(3)
                                        radius: parent.radius - Theme.scaled(3)
                                        gradient: Gradient {
                                            GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.3) }
                                            GradientStop { position: 0.5; color: Qt.rgba(1, 1, 1, 0) }
                                        }
                                    }

                                    Text {
                                        id: popupBubbleText
                                        anchors.centerIn: parent
                                        text: root.displayText || (root.value.toFixed(root.decimals) + root.suffix)
                                        font.pixelSize: Theme.scaled(30)
                                        font.bold: true
                                        color: parent.parent.getContrastColor(root.valueColor)
                                    }
                                }

                                Canvas {
                                    id: popupBubbleTail
                                    anchors.horizontalCenter: popupBubbleRect.horizontalCenter
                                    anchors.top: popupBubbleRect.bottom
                                    anchors.topMargin: -Theme.scaled(3)
                                    width: Theme.scaled(30)
                                    height: Theme.scaled(21)

                                    onPaint: {
                                        var ctx = getContext("2d")
                                        ctx.reset()
                                        ctx.fillStyle = root.valueColor
                                        ctx.beginPath()
                                        ctx.moveTo(0, 0)
                                        ctx.lineTo(width, 0)
                                        ctx.lineTo(width / 2, height)
                                        ctx.closePath()
                                        ctx.fill()
                                    }

                                    Component.onCompleted: requestPaint()
                                    Connections {
                                        target: root
                                        function onValueColorChanged() { popupBubbleTail.requestPaint() }
                                    }
                                }
                            }
                        }
                    }

                    // Plus button
                    Rectangle {
                        Layout.preferredWidth: Theme.scaled(70)
                        Layout.fillHeight: true
                        radius: Theme.scaled(12)
                        color: popupPlusArea.pressed ? Qt.darker(Theme.surfaceColor, 1.3) : "transparent"

                        Text {
                            anchors.centerIn: parent
                            text: "+"
                            font.pixelSize: Theme.scaled(32)
                            font.bold: true
                            color: root.value >= root.to ? Theme.textSecondaryColor : Theme.textColor
                        }

                        MouseArea {
                            id: popupPlusArea
                            anchors.fill: parent
                            onClicked: adjustValue(1)
                            onPressAndHold: popupIncrementTimer.start()
                            onReleased: popupIncrementTimer.stop()
                            onCanceled: popupIncrementTimer.stop()
                        }

                        Timer {
                            id: popupIncrementTimer
                            interval: 80
                            repeat: true
                            onTriggered: adjustValue(1)
                        }
                    }
                }
            }

            // Range display below
            Text {
                anchors.top: popupControl.bottom
                anchors.topMargin: Theme.scaled(12)
                anchors.horizontalCenter: parent.horizontalCenter
                text: root.from.toFixed(root.decimals) + " \u2014 " + root.to.toFixed(root.decimals)
                font: Theme.bodyFont
                color: Theme.textSecondaryColor
            }
        }
    }

    function adjustValue(steps) {
        var newVal = root.value + (steps * root.stepSize)
        newVal = Math.max(root.from, Math.min(root.to, newVal))
        newVal = Math.round(newVal / root.stepSize) * root.stepSize
        if (newVal !== root.value) {
            root.valueModified(newVal)
        }
    }
}
