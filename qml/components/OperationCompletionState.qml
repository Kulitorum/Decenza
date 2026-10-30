import QtQml

// UI-free completion timing and dialog-hold policy shared by both shells.
QtObject {
    id: root

    property string message: ""
    property string completionType: ""
    property int duration: 1500
    property bool pending: false
    property bool suspended: false
    readonly property bool visible: pending && !suspended

    function showCompletion(text, type) {
        message = text
        completionType = type
        pending = true
        if (suspended) {
            completionTimer.stop()
            return
        }
        completionTimer.restart()
    }

    // Keep the completion on screen without counting down; finish() ends it.
    function hold() {
        completionTimer.stop()
    }

    function suspend() {
        suspended = true
        pending = false
        completionTimer.stop()
    }

    function cancel() {
        completionTimer.stop()
        suspended = false
        pending = false
    }

    function finish() {
        completionTimer.stop()
        suspended = false
        pending = false
        root.finished()
    }

    property Timer completionTimerObject: Timer {
        id: completionTimer
        interval: root.duration
        onTriggered: {
            if (!root.suspended)
                root.finish()
        }
    }

    signal finished()
}
