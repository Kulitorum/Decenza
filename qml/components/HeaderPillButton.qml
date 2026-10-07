import QtQuick
import QtQuick.Layouts
import Decenza

// A bordered pill in the post-shot review header (Read TDS, milk weigh). The label is
// Accessible.ignored, so anything a screen reader must hear goes in accessibleName.
Rectangle {
    id: root

    property alias text: label.text
    required property string accessibleName
    property bool highlighted: false
    property bool supportLongPress: false

    signal clicked()
    signal longPressed()
    signal increaseRequested()   // screen-reader increase action

    // Content-driven, with a floor so a short label does not make the pill jump.
    Layout.preferredWidth: Math.min(Theme.touchTargetMin * 4,
                                    Math.max(Theme.touchTargetMin * 2, label.implicitWidth + 2 * Theme.spacingSmall))
    Layout.preferredHeight: Theme.touchTargetMin
    Layout.alignment: Qt.AlignVCenter
    radius: Theme.buttonRadius
    color: Theme.cardBackgroundColor
    border.width: highlighted ? 2 : 1
    border.color: highlighted ? Theme.primaryColor : Theme.textSecondaryColor
    Accessible.ignored: true

    Text {
        id: label
        anchors.centerIn: parent
        width: Math.min(implicitWidth, parent.width - 2 * Theme.spacingSmall)
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
        color: Theme.textColor
        font: Theme.labelFont
        Accessible.ignored: true
    }

    AccessibleMouseArea {
        anchors.fill: parent
        accessibleName: root.accessibleName
        accessibleItem: root
        enabled: root.enabled
        supportLongPress: root.supportLongPress
        onAccessibleClicked: root.clicked()
        onAccessibleLongPressed: root.longPressed()
        Accessible.onIncreaseAction: root.increaseRequested()
    }
}
