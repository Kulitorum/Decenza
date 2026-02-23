import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1

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

    // Geared mode - in fullscreen popup, vertical finger distance from center controls step multiplier
    // Auto-enabled when the range has too many steps to comfortably drag through
    property bool geared: stepSize > 0 && (to - from) / stepSize > 200

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
                    }

                    onPositionChanged: function(mouse) {
                        var deltaX = mouse.x - startX
                        var deltaY = mouse.y - startY

                        // Direction commitment: require horizontal movement to
                        // dominate before activating drag. Vertical-dominant
                        // movement passes through to ScrollView for scrolling
                        // (preventStealing is false while isDragging is false).
                        if (!dragReady) {
                            var absX = Math.abs(deltaX)
                            var absY = Math.abs(deltaY)
                            if (absX > sc(15) && absX > absY) {
                                dragReady = true
                                startX = mouse.x
                                startY = mouse.y
                                return
                            } else {
                                return
                            }
                        }

                        if (root.geared) {
                            if (!isDragging) {
                                isDragging = true
                                // Announce parameter name on first drag activation (accessibility)
                                if (root.accessibleName && typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
                                    var valueStr = root.displayText || (root.value.toFixed(root.decimals) + " " + root.suffix.trim())
                                    AccessibilityManager.announce(root.accessibleName + ": " + valueStr)
                                }
                            }

                            // Gear based on vertical distance from center of widget
                            var centerY = valueDragArea.height / 2
                            var verticalDist = Math.abs(mouse.y - centerY)
                            var widgetHeight = root.height
                            var gear = Math.min(2, Math.floor(verticalDist / widgetHeight))

                            if (gear !== currentGear) {
                                currentGear = gear
                                announceGearChange(gear)
                            }

                            var gearMultiplier = Math.pow(10, gear)
                            var effectiveStep = root.stepSize * gearMultiplier

                            var dragStep = sc(20)
                            var steps = Math.round(deltaX / dragStep)
                            if (steps !== 0) {
                                adjustValueWithStep(steps, effectiveStep)
                                startX = mouse.x
                            }
                        } else {
                            if (!isDragging) {
                                isDragging = true
                                // Announce parameter name on first drag activation (accessibility)
                                if (root.accessibleName && typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
                                    var valueStr2 = root.displayText || (root.value.toFixed(root.decimals) + " " + root.suffix.trim())
                                    AccessibilityManager.announce(root.accessibleName + ": " + valueStr2)
                                }
                            }

                            var deltaY2 = startY - mouse.y
                            var delta = Math.abs(deltaX) > Math.abs(deltaY2) ? deltaX : deltaY2

                            var dragStep2 = sc(20)
                            var steps2 = Math.round(delta / dragStep2)
                            if (steps2 !== 0) {
                                adjustValue(steps2)
                                startX = mouse.x
                                startY = mouse.y
                            }
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

            // Inline geared step indicator - rendered in overlay below the widget
            Loader {
                active: root.geared && valueDragArea.isDragging
                sourceComponent: Item {
                    parent: Overlay.overlay
                    visible: root.geared && valueDragArea.isDragging

                    property point widgetPos: valueContainer.mapToItem(Overlay.overlay, valueContainer.width / 2, valueContainer.height)
                    x: widgetPos.x - width / 2
                    y: widgetPos.y + sc(8)
                    width: stepLabel.width
                    height: stepLabel.height

                    Text {
                        id: stepLabel
                        text: {
                            var effectiveStep = root.stepSize * Math.pow(10, valueDragArea.currentGear)
                            var d = Math.max(0, -Math.floor(Math.log10(effectiveStep) + 0.0001))
                            return "step: " + effectiveStep.toFixed(d)
                        }
                        font.pixelSize: sc(14)
                        font.bold: true
                        color: Theme.primaryColor
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
                                if (root.geared) {
                                    var deltaX = mouse.x - startX

                                    if (!isDragging && (Math.abs(deltaX) > sc(5) || Math.abs(mouse.y - startY) > sc(5))) {
                                        isDragging = true
                                    }

                                    if (isDragging) {
                                        // Gear based on vertical distance from center of drag area
                                        var centerY = popupDragArea.height / 2
                                        var verticalDist = Math.abs(mouse.y - centerY)
                                        var widgetHeight = popupControl.height
                                        var gear = Math.min(2, Math.floor(verticalDist / widgetHeight))

                                        if (gear !== popupContent.currentGear) {
                                            popupContent.currentGear = gear
                                            announceGearChange(gear)
                                        }

                                        var gearMultiplier = Math.pow(10, gear)
                                        var effectiveStep = root.stepSize * gearMultiplier

                                        // Horizontal drag changes value
                                        var dragStep = sc(20)
                                        var steps = Math.round(deltaX / dragStep)
                                        if (steps !== 0) {
                                            adjustValueWithStep(steps, effectiveStep)
                                            startX = mouse.x
                                            // Don't reset startY - vertical position is absolute from center
                                        }
                                    }
                                } else {
                                    var deltaX2 = mouse.x - startX
                                    var deltaY = startY - mouse.y
                                    var delta = Math.abs(deltaX2) > Math.abs(deltaY) ? deltaX2 : deltaY

                                    if (Math.abs(delta) > sc(5)) {
                                        isDragging = true
                                    }

                                    if (isDragging) {
                                        var dragStep2 = sc(20)
                                        var steps2 = Math.round(delta / dragStep2)
                                        if (steps2 !== 0) {
                                            adjustValue(steps2)
                                            startX = mouse.x
                                            startY = mouse.y
                                        }
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

            // Gear zone indicators (only in geared mode)
            Item {
                visible: root.geared
                anchors.fill: parent

                // Gear zone boundary lines and labels
                Repeater {
                    model: 2  // gear 1 and gear 2 boundaries

                    Item {
                        property real offsetY: (index + 1) * popupControl.height
                        property real centerY: popupControl.y + popupControl.height / 2
                        property string label: index === 0 ? "×10" : "×100"
                        property bool isActiveGear: popupContent.currentGear > index

                        // Line above center
                        Rectangle {
                            x: popupControl.x
                            y: parent.centerY - parent.offsetY
                            width: popupControl.width
                            height: 1
                            color: Theme.textSecondaryColor
                            opacity: parent.isActiveGear ? 0.5 : 0.15
                        }

                        // Line below center
                        Rectangle {
                            x: popupControl.x
                            y: parent.centerY + parent.offsetY
                            width: popupControl.width
                            height: 1
                            color: Theme.textSecondaryColor
                            opacity: parent.isActiveGear ? 0.5 : 0.15
                        }

                        // Label above
                        Text {
                            x: popupControl.x + popupControl.width + sc(8)
                            y: parent.centerY - parent.offsetY - height / 2
                            text: parent.label
                            font.pixelSize: sc(12)
                            color: Theme.textSecondaryColor
                            opacity: parent.isActiveGear ? 0.8 : 0.3
                        }
                    }
                }

                // Current step indicator (visible while dragging, below bar so finger doesn't cover it)
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: popupControl.y + popupControl.height + sc(40)
                    visible: popupDragArea.isDragging
                    text: {
                        var effectiveStep = root.stepSize * Math.pow(10, popupContent.currentGear)
                        var d = Math.max(0, -Math.floor(Math.log10(effectiveStep) + 0.0001))
                        return "step: " + effectiveStep.toFixed(d)
                    }
                    font.pixelSize: sc(28)
                    font.bold: true
                    color: Theme.primaryColor
                }
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
