import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

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
    property string rangeText: ""   // Optional override for range display in popup
    property string accessibleName: ""  // Optional override for accessibility announcement
    property color valueColor: Theme.textColor
    property color accentColor: Theme.primaryColor

    // Geared mode - horizontal drag changes value, vertical drag from drag start selects step multiplier:
    // 0–50 px = ×1 (base stepSize), 50–100 px = ×10, 100+ px = ×100
    property bool geared: stepSize > 0

    // Scale mode - when true, uses Theme.scaledBase() for consistent size across pages
    property bool useBaseScale: false

    // Helper to use either scaled or scaledBase based on useBaseScale property
    function sc(value) {
        return useBaseScale ? Theme.scaledBase(value) : Theme.scaled(value)
    }

    // Signals - emits the new value for parent to apply
    signal valueModified(real newValue)

    // Enable keyboard focus
    activeFocusOnTab: true
    focus: true

    // Accessibility - expose as a slider with contextual label
    Accessible.role: Accessible.Slider
    Accessible.name: (root.accessibleName ? root.accessibleName + " " : "") +
                     (root.displayText || (root.value.toFixed(root.decimals) + root.suffix))
    Accessible.description: TranslationManager.translate("valueinput.accessibility.description", "Use plus and minus buttons to adjust. Tap center for full-screen editor.")
    Accessible.focusable: true

    // Keyboard handling
    Keys.onUpPressed: adjustValue(1)
    Keys.onDownPressed: adjustValue(-1)
    Keys.onLeftPressed: adjustValue(-1)
    Keys.onRightPressed: adjustValue(1)

    Keys.onReturnPressed: scrubberPopup.open()
    Keys.onEnterPressed: scrubberPopup.open()
    Keys.onSpacePressed: scrubberPopup.open()

    // Page up/down for larger steps
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_PageUp) {
            adjustValue(10)
            event.accepted = true
        } else if (event.key === Qt.Key_PageDown) {
            adjustValue(-10)
            event.accepted = true
        }
    }

    // Announce value when focused (for accessibility)
    onActiveFocusChanged: {
        if (activeFocus && typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
            var text = root.accessibleName || root.displayText || (root.value.toFixed(root.decimals) + " " + root.suffix)
            AccessibilityManager.announce(text)
        }
    }

    // Auto-size based on content
    // Buttons: 24 each, margins: 2 each side, spacing: 2 each side = 56 total fixed
    implicitWidth: sc(56) + textMetrics.width + sc(16)
    implicitHeight: sc(36)

    // Measure the text width for auto-sizing
    TextMetrics {
        id: textMetrics
        font.pixelSize: sc(16)
        font.bold: true
        text: root.displayText || (root.value.toFixed(root.decimals) + root.suffix)
    }

    // Focus indicator around the entire control
    Rectangle {
        anchors.fill: valueDisplay
        anchors.margins: -Theme.focusMargin
        visible: root.activeFocus
        color: "transparent"
        border.width: Theme.focusBorderWidth
        border.color: Theme.focusColor
        radius: valueDisplay.radius + Theme.focusMargin
        z: -1
    }

    // Compact value display
    Rectangle {
        id: valueDisplay
        anchors.fill: parent
        radius: sc(8)
        color: Theme.surfaceColor
        border.width: 1
        border.color: Theme.textSecondaryColor

        RowLayout {
            anchors.fill: parent
            anchors.margins: sc(2)
            spacing: sc(2)

            // Minus button - immediate response, Flickable handles scroll detection
            Rectangle {
                Layout.preferredWidth: sc(24)
                Layout.fillHeight: true
                radius: sc(6)
                color: minusArea.pressed ? Qt.darker(Theme.surfaceColor, 1.3) : "transparent"

                Accessible.role: Accessible.Button
                Accessible.name: TranslationManager.translate("valueinput.button.decrease", "Decrease")
                Accessible.focusable: true
                Accessible.onPressAction: minusArea.clicked(null)

                Text {
                    anchors.centerIn: parent
                    text: "\u2212"
                    font.pixelSize: sc(16)
                    font.bold: true
                    color: root.value <= root.from ? Theme.textSecondaryColor : Theme.textColor
                    Accessible.ignored: true
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
                clip: true

                Text {
                    id: valueText
                    anchors.centerIn: parent
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: root.displayText || (root.value.toFixed(root.decimals) + root.suffix)
                    font.pixelSize: sc(16)
                    font.bold: true
                    color: root.valueColor
                    elide: Text.ElideRight
                }

                MouseArea {
                    id: valueDragArea
                    anchors.fill: parent
                    preventStealing: dragReady

                    property real startX: 0
                    property real startY: 0
                    property bool isDragging: false
                    // Direction commitment flag: set when horizontal movement
                    // dominates vertical past a threshold, meaning the user
                    // intends a value drag rather than a scroll.
                    property bool dragReady: false
                    property int currentGear: 0

                    onPressed: function(mouse) {
                        startX = mouse.x
                        startY = mouse.y
                        isDragging = false
                        dragReady = false
                        currentGear = 0

                        // Announce parameter name when touched (accessibility)
                        if (root.accessibleName && typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
                            var valueStr = root.displayText || (root.value.toFixed(root.decimals) + " " + root.suffix.trim())
                            AccessibilityManager.announce(root.accessibleName + ": " + valueStr)
                        }
                    }

                    onPositionChanged: function(mouse) {
                        var deltaX = mouse.x - startX
                        var deltaY = mouse.y - startY

                        // Direction commitment: require horizontal movement to
                        // dominate before activating drag. Vertical-dominant
                        // movement passes through to ScrollView for scrolling
                        // (preventStealing is false while dragReady is false).
                        if (!dragReady) {
                            var absX = Math.abs(deltaX)
                            var absY = Math.abs(deltaY)
                            if (absX > sc(15) && absX > absY) {
                                dragReady = true
                                isDragging = true
                                startX = mouse.x
                                startY = mouse.y
                                return
                            } else {
                                return
                            }
                        }

                        // Vertical distance from drag-start selects gear (50 px per level)
                        var gear = Math.min(2, Math.floor(Math.abs(mouse.y - startY) / sc(50)))
                        if (gear !== currentGear) {
                            currentGear = gear
                            announceGearChange(gear)
                        }

                        var effectiveStep = root.stepSize * Math.pow(10, gear)
                        var steps = Math.round(deltaX / sc(20))
                        if (steps !== 0) {
                            adjustValueWithStep(steps, effectiveStep)
                            startX = mouse.x
                            // Do NOT reset startY — it is the gear reference point
                        }
                    }

                    onReleased: {
                        if (!isDragging) {
                            scrubberPopup.open()
                        }
                        isDragging = false
                        dragReady = false
                    }

                    onCanceled: {
                        isDragging = false
                        dragReady = false
                    }
                }

                // Anchor point for bubble positioning (centered)
                Item {
                    id: bubbleAnchor
                    anchors.centerIn: parent
                    width: sc(1)
                    height: sc(1)
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

                        property point anchorPos: bubbleAnchor.mapToItem(Overlay.overlay, 0, 0)
                        x: anchorPos.x - width / 2
                        y: anchorPos.y - height - sc(15)
                        width: bubbleRect.width
                        height: bubbleRect.height + bubbleTail.height - sc(3)

                        // Pop-in animation
                        scale: valueDragArea.isDragging ? 1.0 : 0.5
                        opacity: valueDragArea.isDragging ? 1.0 : 0
                        transformOrigin: Item.Bottom
                        Behavior on scale { NumberAnimation { duration: 150; easing.type: Easing.OutBack; easing.overshoot: 2 } }
                        Behavior on opacity { NumberAnimation { duration: 100 } }

                        // Bubble body - 1.5x larger
                        Rectangle {
                            id: bubbleRect
                            width: bubbleText.width + sc(36)
                            height: sc(66)
                            radius: height / 2
                            color: root.valueColor

                            // Subtle gradient shine
                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: sc(3)
                                radius: parent.radius - sc(3)
                                gradient: Gradient {
                                    GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.3) }
                                    GradientStop { position: 0.5; color: Qt.rgba(1, 1, 1, 0) }
                                }
                            }

                            Text {
                                id: bubbleText
                                anchors.centerIn: parent
                                text: root.displayText || (root.value.toFixed(root.decimals) + root.suffix)
                                font.pixelSize: sc(30)
                                font.bold: true
                                color: speechBubble.getContrastColor(root.valueColor)
                            }
                        }

                        // Cartoon tail (triangle pointing down)
                        Canvas {
                            id: bubbleTail
                            anchors.horizontalCenter: bubbleRect.horizontalCenter
                            anchors.top: bubbleRect.bottom
                            anchors.topMargin: -sc(3)
                            width: sc(30)
                            height: sc(21)

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

            // Gear indicator + step label — rendered in overlay to the right of the widget
            Loader {
                active: valueDragArea.isDragging
                sourceComponent: Item {
                    parent: Overlay.overlay
                    visible: valueDragArea.isDragging

                    property point widgetPos: valueContainer.mapToItem(Overlay.overlay, valueContainer.width, valueContainer.height / 2)
                    x: widgetPos.x + sc(8)
                    y: widgetPos.y - height / 2
                    width: gearCol.width
                    height: gearCol.height

                    Column {
                        id: gearCol
                        spacing: sc(2)

                        Repeater {
                            model: ["×1", "×10", "×100"]
                            Text {
                                text: modelData
                                font.pixelSize: sc(13)
                                font.bold: valueDragArea.currentGear === index
                                color: valueDragArea.currentGear === index ? Theme.primaryColor : Theme.textSecondaryColor
                                opacity: valueDragArea.currentGear === index ? 1.0 : 0.35
                            }
                        }
                    }
                }
            }

            // Plus button - immediate response, Flickable handles scroll detection
            Rectangle {
                Layout.preferredWidth: sc(24)
                Layout.fillHeight: true
                radius: sc(6)
                color: plusArea.pressed ? Qt.darker(Theme.surfaceColor, 1.3) : "transparent"

                Accessible.role: Accessible.Button
                Accessible.name: TranslationManager.translate("valueinput.button.increase", "Increase")
                Accessible.focusable: true
                Accessible.onPressAction: plusArea.clicked(null)

                Text {
                    anchors.centerIn: parent
                    text: "+"
                    font.pixelSize: sc(16)
                    font.bold: true
                    color: root.value >= root.to ? Theme.textSecondaryColor : Theme.textColor
                    Accessible.ignored: true
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

        onOpened: {
            popupValueContainer.forceActiveFocus()
            if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
                var announcement = root.accessibleName ? root.accessibleName + ". " : ""
                var valueStr = root.displayText || (root.value.toFixed(root.decimals) + " " + root.suffix.trim())
                announcement += TranslationManager.translate("valueinput.editor.announce", "Value editor. Current value:") + " " + valueStr
                AccessibilityManager.announce(announcement, true)
            }
        }

        background: Rectangle {
            color: "#80000000"
        }

        // Content
        Item {
            id: popupContent
            anchors.fill: parent
            property int currentGear: 0

            Accessible.role: Accessible.Dialog
            Accessible.name: TranslationManager.translate("valueinput.editor.title", "Value editor")

            // Tap outside to close
            MouseArea {
                anchors.fill: parent
                onClicked: scrubberPopup.close()
            }

            // Full-width value control - same as compact but larger
            Rectangle {
                id: popupControl
                anchors.centerIn: parent
                width: parent.width - sc(40)
                height: sc(80)
                radius: sc(16)
                color: Theme.surfaceColor
                border.width: 1
                border.color: Theme.textSecondaryColor

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: sc(6)
                    spacing: sc(4)

                    // Minus button
                    Rectangle {
                        Layout.preferredWidth: sc(70)
                        Layout.fillHeight: true
                        radius: sc(12)
                        color: popupMinusArea.pressed ? Qt.darker(Theme.surfaceColor, 1.3) : "transparent"

                        Accessible.role: Accessible.Button
                        Accessible.name: TranslationManager.translate("valueinput.button.decrease", "Decrease")
                        Accessible.focusable: true
                        Accessible.onPressAction: popupMinusArea.clicked(null)

                        Text {
                            anchors.centerIn: parent
                            text: "\u2212"
                            font.pixelSize: sc(32)
                            font.bold: true
                            color: root.value <= root.from ? Theme.textSecondaryColor : Theme.textColor
                            Accessible.ignored: true
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
                        focus: true

                        // Keyboard navigation
                        Keys.onEscapePressed: scrubberPopup.close()
                        Keys.onUpPressed: adjustValue(1)
                        Keys.onDownPressed: adjustValue(-1)
                        Keys.onLeftPressed: adjustValue(-1)
                        Keys.onRightPressed: adjustValue(1)
                        Keys.onReturnPressed: scrubberPopup.close()
                        Keys.onEnterPressed: scrubberPopup.close()

                        Text {
                            anchors.centerIn: parent
                            text: root.displayText || (root.value.toFixed(root.decimals) + root.suffix)
                            font.pixelSize: sc(40)
                            font.bold: true
                            color: root.valueColor
                        }

                        MouseArea {
                            id: popupDragArea
                            anchors.fill: parent
                            preventStealing: isDragging

                            property real startX: 0
                            property real startY: 0
                            property bool isDragging: false

                            onPressed: function(mouse) {
                                startX = mouse.x
                                startY = mouse.y
                                isDragging = false
                                popupContent.currentGear = 0

                                // Announce parameter name when bubble appears (accessibility)
                                if (root.accessibleName && typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
                                    var valueStr = root.displayText || (root.value.toFixed(root.decimals) + " " + root.suffix.trim())
                                    AccessibilityManager.announce(root.accessibleName + ": " + valueStr)
                                }
                            }

                            onPositionChanged: function(mouse) {
                                var deltaX = mouse.x - startX

                                if (!isDragging && (Math.abs(deltaX) > sc(5) || Math.abs(mouse.y - startY) > sc(5))) {
                                    isDragging = true
                                }

                                if (isDragging) {
                                    // Vertical distance from drag-start selects gear (50 px per level)
                                    var gear = Math.min(2, Math.floor(Math.abs(mouse.y - startY) / sc(50)))
                                    if (gear !== popupContent.currentGear) {
                                        popupContent.currentGear = gear
                                        announceGearChange(gear)
                                    }

                                    var effectiveStep = root.stepSize * Math.pow(10, gear)
                                    var steps = Math.round(deltaX / sc(20))
                                    if (steps !== 0) {
                                        adjustValueWithStep(steps, effectiveStep)
                                        startX = mouse.x
                                        // Do NOT reset startY — it is the gear reference point
                                    }
                                }
                            }

                            onReleased: {
                                isDragging = false
                                popupContent.currentGear = 0
                            }

                            onCanceled: {
                                isDragging = false
                                popupContent.currentGear = 0
                            }
                        }

                        // Anchor for popup bubble
                        Item {
                            id: popupBubbleAnchor
                            anchors.centerIn: parent
                            width: sc(1)
                            height: sc(1)
                        }

                        // Speech bubble for popup
                        Loader {
                            active: popupDragArea.pressed
                            sourceComponent: Item {
                                parent: Overlay.overlay
                                visible: popupDragArea.pressed

                                function getContrastColor(c) {
                                    var luminance = 0.299 * c.r + 0.587 * c.g + 0.114 * c.b
                                    return luminance > 0.5 ? "#000000" : "#FFFFFF"
                                }

                                property point globalPos: popupBubbleAnchor.mapToGlobal(0, 0)
                                x: globalPos.x - width / 2
                                y: globalPos.y - height - sc(15)
                                width: popupBubbleRect.width
                                height: popupBubbleRect.height + popupBubbleTail.height - sc(3)

                                scale: popupDragArea.pressed ? 1.0 : 0.5
                                opacity: popupDragArea.pressed ? 1.0 : 0
                                transformOrigin: Item.Bottom
                                Behavior on scale { NumberAnimation { duration: 150; easing.type: Easing.OutBack; easing.overshoot: 2 } }
                                Behavior on opacity { NumberAnimation { duration: 100 } }

                                Rectangle {
                                    id: popupBubbleRect
                                    width: popupBubbleText.width + sc(36)
                                    height: sc(66)
                                    radius: height / 2
                                    color: root.valueColor

                                    Rectangle {
                                        anchors.fill: parent
                                        anchors.margins: sc(3)
                                        radius: parent.radius - sc(3)
                                        gradient: Gradient {
                                            GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.3) }
                                            GradientStop { position: 0.5; color: Qt.rgba(1, 1, 1, 0) }
                                        }
                                    }

                                    Text {
                                        id: popupBubbleText
                                        anchors.centerIn: parent
                                        text: root.displayText || (root.value.toFixed(root.decimals) + root.suffix)
                                        font.pixelSize: sc(30)
                                        font.bold: true
                                        color: parent.parent.getContrastColor(root.valueColor)
                                    }
                                }

                                Canvas {
                                    id: popupBubbleTail
                                    anchors.horizontalCenter: popupBubbleRect.horizontalCenter
                                    anchors.top: popupBubbleRect.bottom
                                    anchors.topMargin: -sc(3)
                                    width: sc(30)
                                    height: sc(21)

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
                        Layout.preferredWidth: sc(70)
                        Layout.fillHeight: true
                        radius: sc(12)
                        color: popupPlusArea.pressed ? Qt.darker(Theme.surfaceColor, 1.3) : "transparent"

                        Accessible.role: Accessible.Button
                        Accessible.name: TranslationManager.translate("valueinput.button.increase", "Increase")
                        Accessible.focusable: true
                        Accessible.onPressAction: popupPlusArea.clicked(null)

                        Text {
                            anchors.centerIn: parent
                            text: "+"
                            font.pixelSize: sc(32)
                            font.bold: true
                            color: root.value >= root.to ? Theme.textSecondaryColor : Theme.textColor
                            Accessible.ignored: true
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

            // Gear selector — column of multipliers to the left of the control
            Column {
                anchors.right: popupControl.left
                anchors.rightMargin: sc(12)
                anchors.verticalCenter: popupControl.verticalCenter
                spacing: sc(8)

                Repeater {
                    model: ["×1", "×10", "×100"]
                    Text {
                        text: modelData
                        font.pixelSize: sc(16)
                        font.bold: popupContent.currentGear === index
                        color: popupContent.currentGear === index ? Theme.primaryColor : Theme.textSecondaryColor
                        opacity: popupContent.currentGear === index ? 1.0 : 0.35
                    }
                }
            }

            // Step size indicator while dragging
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                y: popupControl.y + popupControl.height + sc(20)
                visible: popupDragArea.isDragging
                text: {
                    var effectiveStep = root.stepSize * Math.pow(10, popupContent.currentGear)
                    var d = Math.max(0, -Math.floor(Math.log10(effectiveStep) + 0.0001))
                    return TranslationManager.translate("valueinput.step", "step") + ": " + effectiveStep.toFixed(d)
                }
                font.pixelSize: sc(24)
                font.bold: true
                color: Theme.primaryColor
            }

            // Usage hint when not dragging
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                y: popupControl.y + popupControl.height + sc(20)
                visible: !popupDragArea.isDragging
                text: TranslationManager.translate("valueinput.hint", "← drag  ↕ gear")
                font.pixelSize: sc(16)
                color: Theme.textSecondaryColor
            }

            // Range display below
            Text {
                anchors.top: popupControl.bottom
                anchors.topMargin: sc(12)
                anchors.horizontalCenter: parent.horizontalCenter
                text: root.rangeText || (root.from.toFixed(root.decimals) + root.suffix + " \u2014 " + root.to.toFixed(root.decimals) + root.suffix)
                font: Theme.bodyFont
                color: Theme.textSecondaryColor
            }
        }
    }

    function announceGearChange(gear) {
        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
            var effectiveStep = root.stepSize * Math.pow(10, gear)
            var d = Math.max(0, -Math.floor(Math.log10(effectiveStep) + 0.0001))
            AccessibilityManager.announce(TranslationManager.translate("valueinput.gear.step", "Step") + " " + effectiveStep.toFixed(d))
        }
    }

    function adjustValue(steps) {
        adjustValueWithStep(steps, root.stepSize)
    }

    function adjustValueWithStep(steps, effectiveStep) {
        var newVal = root.value + (steps * effectiveStep)
        newVal = Math.max(root.from, Math.min(root.to, newVal))
        // Always round to base stepSize to maintain precision
        newVal = Math.round(newVal / root.stepSize) * root.stepSize
        if (newVal !== root.value) {
            root.valueModified(newVal)
            if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
                AccessibilityManager.announce(root.displayText || (newVal.toFixed(root.decimals) + (root.suffix.trim() ? " " + root.suffix.trim() : "")))
            }
        }
    }
}
