import QtQuick
import DecenzaDE1

Item {
    id: root

    property bool running: true
    property bool use24Hour: !WeatherManager.use12HourTime
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
        console.log("[FlipClock] Component loaded, use24Hour:", use24Hour, "use3D:", use3D)
        updateTime()
    }

    // Timer to update time every second
    Timer {
        id: timeTimer
        interval: 1000
        running: root.running && root.visible
        repeat: true
        onTriggered: updateTime()
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
        width: 4 * cardWidth + 2 * digitGap + pairGap + colonWidth
        height: cardHeight

        property real colonWidth: cardWidth * 0.3

        // Apply perspective transform for 3D mode
        transform: use3D ? perspective : null

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
            width: cardWidth
            height: cardHeight
            digit: getDigit(currentHour, 0)
            prevDigit: getDigit(prevHour, 0)
            flipping: hourTensFlipping
            onFlipComplete: hourTensFlipping = false
        }

        // Hour ones
        FlipDigitCard {
            id: hourOnesCard
            x: cardWidth + digitGap
            width: cardWidth
            height: cardHeight
            digit: getDigit(currentHour, 1)
            prevDigit: getDigit(prevHour, 1)
            flipping: hourOnesFlipping
            onFlipComplete: hourOnesFlipping = false
        }

        // Colon
        Item {
            x: 2 * cardWidth + digitGap + (pairGap - clockContainer.colonWidth) / 2
            width: clockContainer.colonWidth
            height: cardHeight

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                y: parent.height * 0.3 - height / 2
                width: cardWidth * 0.12
                height: width
                radius: width / 2
                color: digitColor
            }

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                y: parent.height * 0.7 - height / 2
                width: cardWidth * 0.12
                height: width
                radius: width / 2
                color: digitColor
            }
        }

        // Minute tens
        FlipDigitCard {
            id: minuteTensCard
            x: 2 * cardWidth + digitGap + pairGap
            width: cardWidth
            height: cardHeight
            digit: getDigit(currentMinute, 0)
            prevDigit: getDigit(prevMinute, 0)
            flipping: minuteTensFlipping
            onFlipComplete: minuteTensFlipping = false
        }

        // Minute ones
        FlipDigitCard {
            id: minuteOnesCard
            x: 3 * cardWidth + 2 * digitGap + pairGap
            width: cardWidth
            height: cardHeight
            digit: getDigit(currentMinute, 1)
            prevDigit: getDigit(prevMinute, 1)
            flipping: minuteOnesFlipping
            onFlipComplete: minuteOnesFlipping = false
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
            duration: flipDuration
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
            height: parent.height / 2 - cardGap / 2
            radius: cornerRadius
            color: cardColor
            border.color: outlineColor
            border.width: outlineWidth
            clip: true

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.verticalCenter: parent.top  // Center text at top edge to show bottom half
                // Show OLD digit during entire flip, switch to NEW only when flip completes
                text: flipping ? prevDigit.toString() : digit.toString()
                color: digitColor
                font.pixelSize: cardHeight * 0.75
                font.bold: true
                font.family: "Arial"
            }
        }

        // Layer 2: Static top card - shows NEW digit's top half
        // Always visible - the flipper reveals it as it rotates forward
        Rectangle {
            id: topCard
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: parent.height / 2 - cardGap / 2
            radius: cornerRadius
            color: cardColor
            border.color: outlineColor
            border.width: outlineWidth
            clip: true
            z: 0  // Behind the flipper (z: 10)

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.verticalCenter: parent.bottom  // Center text at bottom edge to show top half
                text: digit.toString()
                color: digitColor
                font.pixelSize: cardHeight * 0.75
                font.bold: true
                font.family: "Arial"
            }
        }

        // Layer 3: Flipper front - shows OLD digit's top half (0° to 90°)
        Item {
            id: flipperFront
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: parent.height / 2 - cardGap / 2
            visible: flipping && flipAngle > -90
            z: 10

            transform: Rotation {
                origin.x: flipperFront.width / 2
                origin.y: flipperFront.height + cardGap / 2  // Align with center gap
                axis { x: 1; y: 0; z: 0 }
                angle: flipAngle
            }

            Rectangle {
                anchors.fill: parent
                radius: cornerRadius
                color: cardColor
                border.color: outlineColor
                border.width: outlineWidth
                clip: true

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.verticalCenter: parent.bottom  // Show top half of digit
                    text: prevDigit.toString()
                    color: digitColor
                    font.pixelSize: cardHeight * 0.75
                    font.bold: true
                    font.family: "Arial"
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
            height: parent.height / 2 - cardGap / 2
            visible: flipping && flipAngle <= -90
            z: 10

            transform: Rotation {
                origin.x: flipperBack.width / 2
                origin.y: flipperBack.height + cardGap / 2  // Align with center gap
                axis { x: 1; y: 0; z: 0 }
                angle: flipAngle
            }

            Rectangle {
                anchors.fill: parent
                radius: cornerRadius
                color: Qt.darker(cardColor, 1.1)
                border.color: outlineColor
                border.width: outlineWidth
                clip: true

                // Flip around X-axis so text appears right-side-up when card is flipped
                Item {
                    anchors.fill: parent
                    transform: Rotation {
                        origin.x: parent.width / 2
                        origin.y: parent.height / 2
                        axis { x: 1; y: 0; z: 0 }
                        angle: 180
                    }

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.verticalCenter: parent.bottom
                        anchors.verticalCenterOffset: cardGap  // Move up by cardGap before clipping
                        text: digit.toString()
                        color: digitColor
                        font.pixelSize: cardHeight * 0.75
                        font.bold: true
                        font.family: "Arial"
                    }
                }
            }
        }

        // Divider line (split line between top and bottom)
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            height: cardGap
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
