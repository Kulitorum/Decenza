import QtQuick

// Horizontal swipe gesture detector with elastic bounce feedback
// Place this over content that should respond to left/right swipes
Item {
    id: swipeArea

    // Signals emitted on successful swipe
    signal swipedLeft()   // Swipe left = go to next (newer)
    signal swipedRight()  // Swipe right = go to previous (older)
    signal tapped(real x, real y)  // Non-swipe tap at position (for accessibility graph readout)
    signal moved(real x, real y)   // Drag position (for inspect scrub, emitted when not horizontal swiping)

    // Whether swiping is allowed in each direction (for edge bounce)
    property bool canSwipeLeft: true
    property bool canSwipeRight: true

    // Visual feedback - the content shifts during swipe
    property real swipeOffset: 0

    // Configuration
    property real swipeThreshold: 80  // Minimum distance to trigger swipe
    property real maxBounceDistance: 40  // Max elastic bounce at edges

    // Internal state
    property real startX: 0
    property real startY: 0
    property bool tracking: false
    property bool isHorizontalSwipe: false
    property bool directionDecided: false

    // Reset animation
    NumberAnimation {
        id: resetAnimation
        target: swipeArea
        property: "swipeOffset"
        to: 0
        duration: 200
        easing.type: Easing.OutCubic
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        // Dynamically prevent stealing only after we confirm horizontal swipe
        preventStealing: isHorizontalSwipe

        onPressed: function(mouse) {
            resetAnimation.stop()
            startX = mouse.x
            startY = mouse.y
            tracking = true
            isHorizontalSwipe = false
            directionDecided = false
            swipeOffset = 0
        }

        onPositionChanged: function(mouse) {
            if (!tracking) return

            var deltaX = mouse.x - startX
            var deltaY = mouse.y - startY

            // Determine swipe direction after moving a bit
            if (!directionDecided && (Math.abs(deltaX) > 10 || Math.abs(deltaY) > 10)) {
                directionDecided = true
                isHorizontalSwipe = Math.abs(deltaX) > Math.abs(deltaY)
            }

            if (isHorizontalSwipe) {
                // Calculate visual offset with elastic bounds
                if (deltaX > 0 && !canSwipeRight) {
                    // Trying to swipe right but can't - elastic resistance
                    swipeOffset = Math.min(deltaX * 0.3, maxBounceDistance)
                } else if (deltaX < 0 && !canSwipeLeft) {
                    // Trying to swipe left but can't - elastic resistance
                    swipeOffset = Math.max(deltaX * 0.3, -maxBounceDistance)
                } else {
                    // Normal swipe
                    swipeOffset = deltaX
                }
            } else if (directionDecided) {
                // Not a horizontal swipe - emit for drag-to-inspect.
                // preventStealing is false here, so parent ScrollView/Flickable
                // can still steal the gesture for scrolling.
                swipeArea.moved(mouse.x, mouse.y)
            }
        }

        onReleased: function(mouse) {
            if (!tracking) {
                isHorizontalSwipe = false
                return
            }
            tracking = false

            var deltaX = mouse.x - startX

            if (isHorizontalSwipe) {
                if (deltaX < -swipeThreshold && canSwipeLeft) {
                    // Successful left swipe
                    swipedLeft()
                } else if (deltaX > swipeThreshold && canSwipeRight) {
                    // Successful right swipe
                    swipedRight()
                }
            }

            isHorizontalSwipe = false
            // Animate back to center
            resetAnimation.start()
        }

        onClicked: function(mouse) {
            // A tap (press+release without swiping) — emit position for graph readout
            swipeArea.tapped(mouse.x, mouse.y)
        }

        onCanceled: {
            tracking = false
            isHorizontalSwipe = false
            resetAnimation.start()
        }
    }
}
