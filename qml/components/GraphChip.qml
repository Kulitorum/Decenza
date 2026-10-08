import QtQuick
import Decenza

// One toggle chip under a shot graph: a curve, a phase or an option. Long-press or
// hover shows its tip.
Rectangle {
    id: chip
    property string label
    property string tip: ""
    property color dotColor: "transparent"
    property bool active: true
    property bool checkable: true
    property bool longPressShowing: false
    signal toggled()

    implicitHeight: Theme.scaled(28)
    implicitWidth: chipRow.implicitWidth + Theme.scaled(18)
    radius: height / 2
    color: active ? Qt.alpha(dotColor.a > 0 ? dotColor : Theme.primaryColor, 0.16) : "transparent"
    border.color: active ? (dotColor.a > 0 ? dotColor : Theme.primaryColor) : Theme.borderColor
    border.width: 1
    opacity: active ? 1.0 : 0.6
    Accessible.description: tip
    Behavior on color { ColorAnimation { duration: 120 } }

    Row {
        id: chipRow
        anchors.centerIn: parent
        spacing: Theme.scaled(5)
        Rectangle {
            visible: chip.dotColor.a > 0
            width: Theme.scaled(7); height: width; radius: width / 2
            anchors.verticalCenter: parent.verticalCenter
            color: chip.dotColor
            Accessible.ignored: true
        }
        Text {
            text: chip.label
            font: Theme.captionFont
            color: chip.active ? Theme.textColor : Theme.textSecondaryColor
            anchors.verticalCenter: parent.verticalCenter
            Accessible.ignored: true
        }
    }
    AccessibleMouseArea {
        id: chipArea
        anchors.fill: parent
        accessibleName: chip.label
        accessibleRole: chip.checkable ? Accessible.CheckBox : Accessible.Button
        accessibleChecked: chip.active
        hoverEnabled: chip.tip !== ""
        supportLongPress: chip.tip !== ""
        onAccessibleClicked: chip.toggled()
        onAccessibleLongPressed: {
            chip.longPressShowing = true
            tipHideTimer.restart()
        }
    }
    // UI auto-dismiss for a long-press tip, as CustomLegend does.
    Timer {
        id: tipHideTimer
        interval: 4000
        onTriggered: chip.longPressShowing = false
    }
    HoverTip {
        text: chip.tip
        shown: (chipArea.containsMouse && chipArea.pressedButtons === 0) || chip.longPressShowing
        immediate: chip.longPressShowing
    }
}
