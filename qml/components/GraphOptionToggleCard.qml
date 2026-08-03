import QtQuick
import QtQuick.Layouts
import QtQuick.Effects
import Decenza

// A labelled checkbox card, as used by the extraction view selector and the graph options
// menu. Existed three times verbatim in ExtractionViewSelector alone (phase indicator,
// stats, advanced curves) before the graph options menu would have made it four.
Rectangle {
    id: card

    required property string label
    property string description: ""
    required property bool checked

    signal toggled(bool enabled)

    Layout.fillWidth: true
    Layout.preferredHeight: Theme.scaled(48)
    radius: Theme.cardRadius
    color: Theme.backgroundColor

    // The AccessibleMouseArea below carries the role, name and checked state for the whole
    // card; announcing the Rectangle as well would read every option twice.
    Accessible.ignored: true

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingMedium
        anchors.rightMargin: Theme.spacingMedium
        spacing: Theme.spacingMedium

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.scaled(2)

            Text {
                text: card.label
                color: Theme.textColor
                font.family: Theme.bodyFont.family
                font.pixelSize: Theme.bodyFont.pixelSize
                Accessible.ignored: true
            }
            Text {
                text: card.description
                visible: card.description.length > 0
                color: Theme.textSecondaryColor
                font: Theme.captionFont
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                Accessible.ignored: true
            }
        }

        Rectangle {
            Layout.preferredWidth: Theme.scaled(20)
            Layout.preferredHeight: Theme.scaled(20)
            Layout.alignment: Qt.AlignVCenter
            radius: Theme.scaled(4)
            color: card.checked ? Theme.primaryColor : "transparent"
            border.color: card.checked ? Theme.primaryColor : Theme.textSecondaryColor
            border.width: Theme.scaled(2)

            Image {
                anchors.centerIn: parent
                source: "qrc:/icons/tick.svg"
                sourceSize.width: Theme.scaled(14)
                sourceSize.height: Theme.scaled(14)
                visible: card.checked
                Accessible.ignored: true

                layer.enabled: true
                layer.effect: MultiEffect {
                    colorization: 1.0
                    colorizationColor: Theme.surfaceColor
                }
            }
        }
    }

    AccessibleMouseArea {
        anchors.fill: parent
        accessibleName: card.label
        accessibleItem: card
        accessibleRole: Accessible.CheckBox
        accessibleChecked: card.checked
        onAccessibleClicked: card.toggled(!card.checked)
    }
}
