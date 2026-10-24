pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

// One title/action layout for page and dialog headers. Actions move below a
// full-width title when the current text and buttons cannot fit side by side.
Item {
    id: root
    required property string title
    required property font titleFont
    required property color titleColor
    required property real spacing
    default property alias actions: actionRow.data
    readonly property bool stacked: width < titleMetrics.advanceWidth(title)
                                            + actionRow.implicitWidth + spacing
    implicitHeight: stacked ? heading.implicitHeight + spacing + actionRow.implicitHeight
                            : Math.max(heading.implicitHeight, actionRow.implicitHeight)

    property FontMetrics titleMetrics: FontMetrics { font: root.titleFont }

    data: [
        Text {
            id: heading
            objectName: "sharedSectionHeading"
            width: root.stacked ? root.width
                               : Math.max(0, root.width - actionRow.width - root.spacing)
            y: root.stacked ? 0 : (root.height - height) / 2
            text: root.title
            textFormat: Text.PlainText
            font: root.titleFont
            color: root.titleColor
            wrapMode: Text.WordWrap
            Accessible.role: Accessible.Heading
            Accessible.name: text
        },
        RowLayout {
            id: actionRow
            objectName: "sharedSectionActions"
            width: implicitWidth
            x: Math.max(0, root.width - width)
            y: root.stacked ? heading.height + root.spacing : (root.height - height) / 2
            spacing: root.spacing
        }
    ]
}
