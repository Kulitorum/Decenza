import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../components"

Page {
    objectName: "descalingPage"
    background: Rectangle { color: Theme.backgroundColor }

    Component.onCompleted: root.currentPageTitle = "Descaling"
    StackView.onActivated: root.currentPageTitle = "Descaling"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.standardMargin
        anchors.topMargin: Theme.pageTopMargin
        spacing: Theme.scaled(20)

        Item { Layout.fillHeight: true }

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: Theme.scaled(400)
            Layout.preferredHeight: Theme.scaled(200)
            color: Theme.surfaceColor
            radius: Theme.cardRadius

            ColumnLayout {
                anchors.centerIn: parent
                spacing: Theme.scaled(20)

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: "Descaling Wizard"
                    font: Theme.titleFont
                    color: Theme.textColor
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: "Not implemented yet"
                    font: Theme.bodyFont
                    color: Theme.textSecondaryColor
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: "For now, use the cleaning profiles from\nthe 'Cleaning/Descale' profile list."
                    font: Theme.captionFont
                    color: Theme.textSecondaryColor
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }

        Item { Layout.fillHeight: true }
    }

    BottomBar {
        title: "Descaling"
        onBackClicked: root.goBack()
    }
}
