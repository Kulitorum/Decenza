import QtQuick

// Service-free visual shell for the Shot Plan. Hosts supply formatted text,
// style, and an optional icon component; all domain resolution stays outside.
Item {
    id: root

    required property SharedShotPlanStyle planStyle
    property string plainText: ""
    property string richText: ""
    property bool isCleaning: false
    property int maxLines: 2
    // Passive hosts such as the browser recipe list must not capture clicks or
    // expose the native Brew Settings action they cannot perform.
    property bool interactive: false
    // Passive consumers expose one plain-text accessibility node. Native
    // ShotPlanText keeps its established parent-owned accessibility node.
    property bool exposePlainText: true
    property Component iconComponent

    Accessible.role: Accessible.StaticText
    Accessible.name: root.plainText
    Accessible.ignored: !root.exposePlainText

    readonly property color contentColor: mouseArea.pressed ? planStyle.pressedColor
        : (isCleaning ? planStyle.warningColor : planStyle.textColor)

    signal clicked()

    implicitWidth: planIcon.width + row.spacing + textMeasure.implicitWidth
    implicitHeight: Math.max(planIcon.height, planText.height)

    Text {
        id: textMeasure
        visible: false
        text: root.richText
        textFormat: Text.StyledText
        font: root.planStyle.bodyFont
        Accessible.ignored: true
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: root.planStyle.spacing

        Loader {
            id: planIcon
            anchors.verticalCenter: parent.verticalCenter
            width: root.planStyle.iconSize
            height: root.planStyle.iconSize
            sourceComponent: root.iconComponent
            Accessible.ignored: true
        }

        Text {
            id: planText
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(0, Math.min(textMeasure.implicitWidth,
                root.width - planIcon.width - row.spacing))
            text: root.richText
            textFormat: Text.StyledText
            font: root.planStyle.bodyFont
            color: root.contentColor
            wrapMode: Text.Wrap
            maximumLineCount: root.maxLines
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
            Accessible.ignored: true
        }
    }

    MouseArea {
        id: mouseArea
        visible: root.interactive
        anchors.fill: parent
        anchors.margins: -root.planStyle.hitInset
        cursorShape: root.interactive ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: root.clicked()
    }
}
