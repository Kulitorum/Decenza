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
        preventStealing: swipeArea.isHorizontalSwipe

        onPressed: function(mouse) {
            resetAnimation.stop()
            swipeArea.startX = mouse.x
            swipeArea.startY = mouse.y
            swipeArea.tracking = true
            swipeArea.isHorizontalSwipe = false
            swipeArea.directionDecided = false
            swipeArea.swipeOffset = 0
        }

        onPositionChanged: function(mouse) {
            if (!swipeArea.tracking) return

            var deltaX = mouse.x - swipeArea.startX
            var deltaY = mouse.y - swipeArea.startY

            // Determine swipe direction after moving a bit
            if (!swipeArea.directionDecided && (Math.abs(deltaX) > 10 || Math.abs(deltaY) > 10)) {
                swipeArea.directionDecided = true
                swipeArea.isHorizontalSwipe = Math.abs(deltaX) > Math.abs(deltaY)
            }

            if (swipeArea.isHorizontalSwipe) {
                // Calculate visual offset with elastic bounds
                if (deltaX > 0 && !swipeArea.canSwipeRight) {
                    // Trying to swipe right but can't - elastic resistance
                    swipeArea.swipeOffset = Math.min(deltaX * 0.3, swipeArea.maxBounceDistance)
                } else if (deltaX < 0 && !swipeArea.canSwipeLeft) {
                    // Trying to swipe left but can't - elastic resistance
                    swipeArea.swipeOffset = Math.max(deltaX * 0.3, -swipeArea.maxBounceDistance)
                } else {
                    // Normal swipe
                    swipeArea.swipeOffset = deltaX
                }
            } else if (swipeArea.directionDecided) {
                // Not a horizontal swipe - emit for drag-to-inspect.
                // preventStealing is false here, so parent ScrollView/Flickable
                // can still steal the gesture for scrolling.
                swipeArea.moved(mouse.x, mouse.y)
            }
        }

        onReleased: function(mouse) {
            if (!swipeArea.tracking) {
                swipeArea.isHorizontalSwipe = false
                return
            }
            swipeArea.tracking = false

            var deltaX = mouse.x - swipeArea.startX

            if (swipeArea.isHorizontalSwipe) {
                if (deltaX < -swipeArea.swipeThreshold && swipeArea.canSwipeLeft) {
                    // Successful left swipe
                    swipeArea.swipedLeft()
                } else if (deltaX > swipeArea.swipeThreshold && swipeArea.canSwipeRight) {
                    // Successful right swipe
                    swipeArea.swipedRight()
                }
            }

            swipeArea.isHorizontalSwipe = false
            // Animate back to center
            resetAnimation.start()
        }

        onClicked: function(mouse) {
            // A tap (press+release without swiping) — emit position for graph readout
            swipeArea.tapped(mouse.x, mouse.y)
        }

        onCanceled: {
            swipeArea.tracking = false
            swipeArea.isHorizontalSwipe = false
            resetAnimation.start()
        }
    }
}
