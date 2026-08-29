import QtQuick
import QtQuick.Layouts
import Decenza

// A tappable row inside a settings card: emoji, title, one line of description.
//
// Extracted from the Maintenance card's Descaling Wizard and Transport Mode rows
// (SettingsMachineTab.qml), which were two hand-written copies of the same 45
// lines. The Sensor Calibration rows on the Calibration tab are a third use, and
// three copies of a row format is exactly the drift the project rule is about —
// each free to change alone, with nothing failing when one does.
//
// The emoji goes through Theme.emojiToImage: a colour glyph in a plain Text
// crashes the render thread on macOS.
Rectangle {
    id: actionRow

    // Emoji shown at the left. Decorative — it is never the only carrier of
    // meaning, so it is hidden from assistive technology.
    property string emoji: ""
    property string title: ""
    // Second line. Callers use it for whatever the user needs to know BEFORE
    // tapping — what the operation does, or what hardware it requires.
    property string description: ""
    // When false the row dims and stops responding. Give `disabledReason` so the
    // row still explains itself rather than going quietly inert.
    property bool actionEnabled: true
    property string disabledReason: ""
    // Read by screen readers. Defaults to title plus the visible second line,
    // which is what a sighted user gets.
    property string accessibleName: title + (visibleDescription.length > 0 ? ". " + visibleDescription : "")

    readonly property string visibleDescription: actionEnabled ? description : disabledReason

    signal triggered()

    Layout.fillWidth: true
    Layout.preferredHeight: Theme.scaled(58)
    radius: Theme.scaled(8)
    color: rowMouse.isPressed ? Theme.backgroundColor : "transparent"
    border.color: Theme.borderColor
    border.width: 1
    opacity: actionEnabled ? 1.0 : 0.5

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.scaled(12)
        anchors.rightMargin: Theme.scaled(12)
        spacing: Theme.scaled(12)

        Image {
            visible: actionRow.emoji.length > 0
            source: actionRow.emoji.length > 0 ? Theme.emojiToImage(actionRow.emoji) : ""
            sourceSize.width: Theme.scaled(24)
            sourceSize.height: Theme.scaled(24)
            Accessible.ignored: true
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.scaled(2)

            Text {
                text: actionRow.title
                color: Theme.textColor
                font.family: Theme.bodyFont.family
                font.pixelSize: Theme.scaled(14)
                Accessible.ignored: true
            }

            Text {
                Layout.fillWidth: true
                visible: actionRow.visibleDescription.length > 0
                text: actionRow.visibleDescription
                color: Theme.textSecondaryColor
                font.family: Theme.bodyFont.family
                font.pixelSize: Theme.scaled(12)
                wrapMode: Text.WordWrap
                Accessible.ignored: true
            }
        }
    }

    AccessibleMouseArea {
        id: rowMouse
        anchors.fill: parent
        enabled: actionRow.actionEnabled
        accessibleName: actionRow.accessibleName
        accessibleItem: actionRow
        onAccessibleClicked: actionRow.triggered()
    }
}
