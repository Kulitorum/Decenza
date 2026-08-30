// The FlipDigitCard inline component reads this file's `root` id for every one of its
// style inputs. An inline component is its own scope, so those ids are not statically
// resolvable inside it without this pragma -- they resolve at runtime because the
// component shares the file's QML context, which is exactly the implicit coupling the
// pragma makes checkable. This file declares no delegate and no model role, so nothing
// here needs a `required property`.
pragma ComponentBehavior: Bound

import QtQuick
import Decenza

Item {
    id: root

    property bool running: true
    property bool use24Hour: !Settings.app.use12HourTime
    property bool use3D: ScreensaverManager.flipClockUse3D

    // Animation duration for the flip
    readonly property int flipDuration: 800  // TEST: Slowed down for debugging

    // Card dimensions (scaled based on screen size)
    readonly property real cardWidth: Math.min(width * 0.13, height * 0.30)
    readonly property real cardHeight: cardWidth * 1.4
    readonly property real cardGap: cardHeight * 0.02  // Gap between top and bottom halves
    readonly property real digitGap: cardWidth * 0.12  // Gap between digits in a pair
    readonly property real pairGap: cardWidth * 0.5  // Gap between hour and minute pairs

    // Colors - classic flip clock style
    readonly property color cardColor: "#2a2a2a"
    readonly property color cardDarkColor: "#1a1a1a"
    readonly property color digitColor: "#f0f0f0"
    readonly property color outlineColor: "#404040"
    readonly property real outlineWidth: 2
    readonly property real cornerRadius: cardWidth * 0.08

    // Current time
    property int currentHour: 0
    property int currentMinute: 0

    // Previous time for flip animation
    property int prevHour: 0
    property int prevMinute: 0

    // Track which digits need to flip
    property bool hourTensFlipping: false
    property bool hourOnesFlipping: false
    property bool minuteTensFlipping: false
    property bool minuteOnesFlipping: false

    Component.onCompleted: {
        console.log("[Screensaver] Component loaded, use24Hour:", use24Hour, "use3D:", use3D)
        updateTime()
    }

    // Timer to update time every second
    Timer {
        id: timeTimer
        interval: 1000
        running: root.running && root.visible
        repeat: true
        onTriggered: root.updateTime()
    }

    function updateTime() {
        var now = new Date()
        var hour = now.getHours()
        var minute = now.getMinutes()

        if (!use24Hour) {
            hour = hour % 12
            if (hour === 0) hour = 12
        }

        var newHourTens = Math.floor(hour / 10)
        var newHourOnes = hour % 10
        var newMinuteTens = Math.floor(minute / 10)
        var newMinuteOnes = minute % 10

        var oldHourTens = Math.floor(currentHour / 10)
        var oldHourOnes = currentHour % 10
        var oldMinuteTens = Math.floor(currentMinute / 10)
        var oldMinuteOnes = currentMinute % 10

        // Start flip animations for changed digits
        if (newHourTens !== oldHourTens) {
            hourTensFlipping = true
        }
        if (newHourOnes !== oldHourOnes) {
            hourOnesFlipping = true
        }
        if (newMinuteTens !== oldMinuteTens) {
            minuteTensFlipping = true
        }
        if (newMinuteOnes !== oldMinuteOnes) {
            minuteOnesFlipping = true
        }

        prevHour = currentHour
        prevMinute = currentMinute
        currentHour = hour
        currentMinute = minute
    }

    // Helper function to get digit at position
    function getDigit(value, position) {
        if (position === 0) {
            return Math.floor(value / 10)
        } else {
            return value % 10
        }
    }

    // Center container with perspective
    Item {
        id: clockContainer
        anchors.centerIn: parent
        width: 4 * root.cardWidth + 2 * root.digitGap + root.pairGap + colonWidth
        height: root.cardHeight

        property real colonWidth: root.cardWidth * 0.3

        // Apply perspective transform for 3D mode
        transform: root.use3D ? perspective : null

        Rotation {
            id: perspective
            origin.x: clockContainer.width / 2
            origin.y: clockContainer.height / 2
            axis { x: 1; y: 0; z: 0 }
            angle: 5
        }

        // Hour tens
        FlipDigitCard {
            id: hourTensCard
            x: 0
            width: root.cardWidth
            height: root.cardHeight
            digit: root.getDigit(root.currentHour, 0)
            prevDigit: root.getDigit(root.prevHour, 0)
            flipping: root.hourTensFlipping
            onFlipComplete: root.hourTensFlipping = false
        }

        // Hour ones
        FlipDigitCard {
            id: hourOnesCard
            x: root.cardWidth + root.digitGap
            width: root.cardWidth
            height: root.cardHeight
            digit: root.getDigit(root.currentHour, 1)
            prevDigit: root.getDigit(root.prevHour, 1)
            flipping: root.hourOnesFlipping
            onFlipComplete: root.hourOnesFlipping = false
        }

        // Colon
        Item {
            x: 2 * root.cardWidth + root.digitGap + (root.pairGap - clockContainer.colonWidth) / 2
            width: clockContainer.colonWidth
            height: root.cardHeight

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                y: parent.height * 0.3 - height / 2
                width: root.cardWidth * 0.12
                height: width
                radius: width / 2
                color: root.digitColor
            }

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                y: parent.height * 0.7 - height / 2
                width: root.cardWidth * 0.12
                height: width
                radius: width / 2
                color: root.digitColor
            }
        }

        // Minute tens
        FlipDigitCard {
            id: minuteTensCard
            x: 2 * root.cardWidth + root.digitGap + root.pairGap
            width: root.cardWidth
            height: root.cardHeight
            digit: root.getDigit(root.currentMinute, 0)
            prevDigit: root.getDigit(root.prevMinute, 0)
            flipping: root.minuteTensFlipping
            onFlipComplete: root.minuteTensFlipping = false
        }

        // Minute ones
        FlipDigitCard {
            id: minuteOnesCard
            x: 3 * root.cardWidth + 2 * root.digitGap + root.pairGap
            width: root.cardWidth
            height: root.cardHeight
            digit: root.getDigit(root.currentMinute, 1)
            prevDigit: root.getDigit(root.prevMinute, 1)
            flipping: root.minuteOnesFlipping
            onFlipComplete: root.minuteOnesFlipping = false
        }
    }

    // FlipDigitCard component - a single flip digit with top and bottom cards
    component FlipDigitCard: Item {
        id: flipCard

        property int digit: 0
        property int prevDigit: 0
        property bool flipping: false
        property real flipAngle: 0

        signal flipComplete()

        onFlippingChanged: {
            if (flipping) {
                flipAngle = 0
                flipAnimation.start()
            }
        }

        NumberAnimation {
            id: flipAnimation
            target: flipCard
            property: "flipAngle"
            from: 0
            to: -180  // Negative = flip forward (toward viewer)
            duration: root.flipDuration
            easing.type: Easing.InOutQuad
            onFinished: {
                flipCard.flipAngle = 0
                flipCard.flipComplete()
            }
        }

        // Layer 1: Static bottom card
        // Shows OLD digit during first half of flip (visible), NEW digit otherwise
        Rectangle {
            id: bottomCard
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: parent.height / 2 - root.cardGap / 2
            radius: root.cornerRadius
            color: root.cardColor
            border.color: root.outlineColor
            border.width: root.outlineWidth
            clip: true

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.verticalCenter: parent.top  // Center text at top edge to show bottom half
                // Show OLD digit during entire flip, switch to NEW only when flip completes
                text: flipCard.flipping ? flipCard.prevDigit.toString() : flipCard.digit.toString()
                color: root.digitColor
                font.pixelSize: root.cardHeight * 0.75
                font.bold: true
                // No font.family: inherits the bundled application font.
                // These carried `font.family: "Arial"`, which exists on macOS
                // and Windows but frequently not on Linux or Android — the
                // digits would fall back to a host font with different metrics,
                // and a flip clock's two halves have to line up exactly.
            }
        }

        // Layer 2: Static top card - shows NEW digit's top half
        // Always visible - the flipper reveals it as it rotates forward
        Rectangle {
            id: topCard
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: parent.height / 2 - root.cardGap / 2
            radius: root.cornerRadius
            color: root.cardColor
            border.color: root.outlineColor
            border.width: root.outlineWidth
            clip: true
            z: 0  // Behind the flipper (z: 10)

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.verticalCenter: parent.bottom  // Center text at bottom edge to show top half
                text: flipCard.digit.toString()
                color: root.digitColor
                font.pixelSize: root.cardHeight * 0.75
                font.bold: true
            }
        }

        // Layer 3: Flipper front - shows OLD digit's top half (0° to 90°)
        Item {
            id: flipperFront
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: parent.height / 2 - root.cardGap / 2
            visible: flipCard.flipping && flipCard.flipAngle > -90
            z: 10

            transform: Rotation {
                origin.x: flipperFront.width / 2
                origin.y: flipperFront.height + root.cardGap / 2  // Align with center gap
                axis { x: 1; y: 0; z: 0 }
                angle: flipCard.flipAngle
            }

            Rectangle {
                anchors.fill: parent
                radius: root.cornerRadius
                color: root.cardColor
                border.color: root.outlineColor
                border.width: root.outlineWidth
                clip: true

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.verticalCenter: parent.bottom  // Show top half of digit
                    text: flipCard.prevDigit.toString()
                    color: root.digitColor
                    font.pixelSize: root.cardHeight * 0.75
                    font.bold: true
                }
            }
        }

        // Layer 4: Flipper back - shows NEW digit's bottom half (90° to 180°)
        // This is the "back" of the flipping card, rotates into bottom position
        // The 3D rotation flips everything, so we position for TOP half and let rotation handle it
        Item {
            id: flipperBack
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: parent.height / 2 - root.cardGap / 2
            visible: flipCard.flipping && flipCard.flipAngle <= -90
            z: 10

            transform: Rotation {
                origin.x: flipperBack.width / 2
                origin.y: flipperBack.height + root.cardGap / 2  // Align with center gap
                axis { x: 1; y: 0; z: 0 }
                angle: flipCard.flipAngle
            }

            Rectangle {
                anchors.fill: parent
                radius: root.cornerRadius
                color: Qt.darker(root.cardColor, 1.1)
                border.color: root.outlineColor
                border.width: root.outlineWidth
                clip: true

                // Flip around X-axis so text appears right-side-up when card is flipped
                Item {
                    id: flippedFace
                    anchors.fill: parent
                    transform: Rotation {
                        origin.x: flippedFace.width / 2
                        origin.y: flippedFace.height / 2
                        axis { x: 1; y: 0; z: 0 }
                        angle: 180
                    }

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.verticalCenter: parent.bottom
                        anchors.verticalCenterOffset: root.cardGap  // Move up by root.cardGap before clipping
                        text: flipCard.digit.toString()
                        color: root.digitColor
                        font.pixelSize: root.cardHeight * 0.75
                        font.bold: true
                    }
                }
            }
        }

        // Divider line (split line between top and bottom)
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            height: root.cardGap
            color: "transparent"
            z: 15
        }
    }

    // Black background (can be made transparent for widget embedding)
    property color backgroundColor: "#000000"
    Rectangle {
        anchors.fill: parent
        color: root.backgroundColor
        z: -10
    }

    function reset() {
        updateTime()
    }
}
