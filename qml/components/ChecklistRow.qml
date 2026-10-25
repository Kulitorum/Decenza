import QtQuick
import QtQuick.Layouts
import Decenza

// One requirement in a setup checklist, or one numbered step of a how-to:
// whether it is done, what it is, what is still missing, and the control that
// does it (a switch or a button), placed as this row's children.
RowLayout {
    id: row

    property bool done: false
    property string label: ""
    property string detail: ""
    property bool detailIsError: false
    // A step number, shown in the marker until the step is done; 0 for none.
    property int number: 0
    default property alias actions: actionSlot.data

    Layout.fillWidth: true
    spacing: Theme.scaled(12)

    Rectangle {
        Layout.alignment: Qt.AlignTop
        Layout.topMargin: Theme.scaled(2)
        Layout.preferredWidth: Theme.scaled(22)
        Layout.preferredHeight: Theme.scaled(22)
        radius: width / 2
        color: row.done ? Theme.successColor : "transparent"
        border.color: row.done ? Theme.successColor : Theme.textSecondaryColor
        border.width: Theme.scaled(2)
        Accessible.ignored: true

        Text {
            anchors.centerIn: parent
            visible: !row.done && row.number > 0
            text: row.number
            color: Theme.textSecondaryColor
            font.pixelSize: Theme.captionFont.pixelSize
            font.bold: true
            Accessible.ignored: true
        }
        Image {
            anchors.centerIn: parent
            visible: row.done
            source: "qrc:/icons/tick.svg"
            sourceSize.width: Theme.scaled(14)
            sourceSize.height: Theme.scaled(14)
            Accessible.ignored: true
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.scaled(2)

        Text {
            Layout.fillWidth: true
            text: row.label
            color: Theme.textColor
            font.pixelSize: Theme.labelFont.pixelSize
            font.bold: true
            wrapMode: Text.WordWrap
            // Read with its state, so a screen reader hears what is still missing.
            Accessible.role: Accessible.StaticText
            Accessible.name: (row.number > 0 ? TranslationManager.translate("checklist.step", "Step %1").arg(row.number) + ". " : "")
                + row.label
                // A plain how-to step has no state to report until it is ticked.
                + (row.done ? ". " + TranslationManager.translate("checklist.done", "Done")
                   : row.number === 0 ? ". " + TranslationManager.translate("checklist.notDone", "Not done yet") : "")
                + (row.detail.length > 0 ? ". " + row.detail : "")
        }
        Text {
            Layout.fillWidth: true
            visible: row.detail.length > 0
            text: row.detail
            color: row.detailIsError ? Theme.errorColor : Theme.textSecondaryColor
            font.pixelSize: Theme.labelFont.pixelSize
            wrapMode: Text.WordWrap
            Accessible.ignored: true
        }
    }

    RowLayout {
        id: actionSlot
        Layout.alignment: Qt.AlignVCenter
    }
}
