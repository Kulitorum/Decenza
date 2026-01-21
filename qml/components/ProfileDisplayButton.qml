import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1

// A reusable row component showing profile name with an info button
// Used in list contexts where profiles are displayed
// Parent handles navigation via infoRequested() signal

Item {
    id: root

    // Profile data
    property string profileFilename: ""
    property string profileName: ""

    // Options
    property bool showInfoButton: true
    property bool interactive: true

    // Styling
    property alias font: nameText.font
    property alias color: nameText.color
    property alias elide: nameText.elide

    // Layout
    implicitWidth: rowLayout.implicitWidth
    implicitHeight: rowLayout.implicitHeight

    signal nameClicked()
    signal infoRequested()

    RowLayout {
        id: rowLayout
        anchors.fill: parent
        spacing: Theme.scaled(6)

        // Profile name
        Text {
            id: nameText
            Layout.fillWidth: true
            text: root.profileName || root.profileFilename || ""
            font: Theme.bodyFont
            color: Theme.textColor
            elide: Text.ElideRight

            MouseArea {
                anchors.fill: parent
                enabled: root.interactive
                cursorShape: root.interactive ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: root.nameClicked()
            }

            Accessible.role: root.interactive ? Accessible.Button : Accessible.StaticText
            Accessible.name: nameText.text
            Accessible.focusable: root.interactive
        }

        // Info button
        ProfileInfoButton {
            id: infoButton
            visible: root.showInfoButton
            profileFilename: root.profileFilename
            profileName: root.profileName

            onClicked: root.infoRequested()
        }
    }
}
