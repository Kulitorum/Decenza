import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1

Rectangle {
    id: root

    property string title: ""
    property color barColor: Theme.primaryColor
    property bool showBackButton: true
    property string rightText: ""  // Simple right-aligned text
    default property alias content: contentRow.data  // Custom content goes here

    signal backClicked()

    anchors.left: parent.left
    anchors.right: parent.right
    anchors.bottom: parent.bottom
    height: Theme.bottomBarHeight
    color: barColor

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.chartMarginSmall
        anchors.rightMargin: Theme.spacingLarge
        spacing: Theme.spacingMedium

        // Back button (square hitbox, full bar height)
        Item {
            id: backButton
            visible: root.showBackButton
            Layout.preferredWidth: Theme.bottomBarHeight
            Layout.preferredHeight: Theme.bottomBarHeight

            activeFocusOnTab: true

            // Accessibility: Let AccessibleTapHandler handle screen reader interaction
            // to avoid duplicate focus elements
            Accessible.ignored: true

            // Focus indicator
            Rectangle {
                anchors.fill: parent
                anchors.margins: -Theme.focusMargin
                visible: backButton.activeFocus
                color: "transparent"
                border.width: Theme.focusBorderWidth
                border.color: Theme.focusColor
                radius: Theme.scaled(4)
            }

            Image {
                anchors.centerIn: parent
                source: "qrc:/icons/back.svg"
                sourceSize.width: Theme.scaled(28)
                sourceSize.height: Theme.scaled(28)
                // Decorative - accessibility handled by AccessibleTapHandler
                Accessible.ignored: true
            }

            Keys.onReturnPressed: root.backClicked()
            Keys.onEnterPressed: root.backClicked()
            Keys.onEscapePressed: root.backClicked()

            // Using TapHandler for better touch responsiveness
            AccessibleTapHandler {
                anchors.fill: parent
                accessibleName: TranslationManager.translate("bottombar.button.back.accessible", "Back. Return to previous screen")
                accessibleItem: backButton
                onAccessibleClicked: root.backClicked()
            }
        }

        Text {
            visible: root.title !== ""
            text: root.title
            color: "white"
            font.pixelSize: Theme.scaled(20)
            font.bold: true
        }

        Item { Layout.fillWidth: true }

        // Custom content area
        RowLayout {
            id: contentRow
            spacing: Theme.spacingMedium
        }

        // Simple right text (alternative to custom content)
        Text {
            visible: root.rightText !== ""
            text: root.rightText
            color: "white"
            font: Theme.bodyFont
        }
    }
}
