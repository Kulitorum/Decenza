import QtQuick
import Decenza

// The single completion treatment and timeout used by both application shells.
// The shell owns what happens after finished(); this component only presents the
// result and holds it while a post-session dialog is open.
Rectangle {
    id: root

    property alias message: completionState.message
    property alias completionType: completionState.completionType
    readonly property bool pending: completionState.pending

    // Kept configurable for focused timer-policy tests; production shells use
    // the native 1.5 second completion interval.
    property int completionDuration: 1500

    signal finished()

    OperationCompletionState {
        id: completionState
        duration: root.completionDuration
        onFinished: root.finished()
    }

    anchors.fill: parent
    color: Theme.backgroundColor
    opacity: completionState.visible ? 1 : 0
    visible: opacity > 0
    z: 500

    function showCompletion(text, type) { completionState.showCompletion(text, type) }

    function suspend() { completionState.suspend() }

    function cancel() { completionState.cancel() }

    function finish() { completionState.finish() }

    Column {
        anchors.centerIn: parent
        spacing: 20

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 120
            height: 120
            radius: 60
            color: "transparent"
            border.color: Theme.primaryColor
            border.width: 4

            ColoredIcon {
                anchors.centerIn: parent
                source: "qrc:/icons/tick.svg"
                iconWidth: 50
                iconHeight: 50
                iconColor: Theme.primaryColor
            }
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.message
            color: Theme.textSecondaryColor
            font: Theme.bodyFont
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.completionType === "hotwater"
                  ? Math.max(0, MachineState.scaleWeight).toFixed(0) + "g"
                  : MachineState.shotTime.toFixed(1) + "s"
            color: Theme.textColor
            font: Theme.timerFont
        }
    }

    Behavior on opacity {
        // Match native behavior: appear instantly, fade only when dismissed.
        enabled: root.opacity > 0
        NumberAnimation { duration: 200 }
    }
}
