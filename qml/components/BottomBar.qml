import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

Rectangle {
    id: root

    property string title: ""
    property color barColor: Theme.primaryColor
    property bool showBackButton: true
    property string rightText: ""  // Simple right-aligned text
    default property alias content: contentRow.data  // Custom content goes here

    signal backClicked()

    // Light mode: blend bar color at 15% over background for soft tint
    function _blendColor(fg, bg, t) {
        return Qt.rgba(fg.r * t + bg.r * (1 - t),
                       fg.g * t + bg.g * (1 - t),
                       fg.b * t + bg.b * (1 - t), 1.0)
    }
    readonly property color _effectiveBarColor: !Theme.isDarkMode
        ? _blendColor(barColor, Theme.backgroundColor, 0.15)
        : barColor
    readonly property color contentColor: !Theme.isDarkMode ? barColor : "white"

    anchors.left: parent.left
    anchors.right: parent.right
    anchors.bottom: parent.bottom
    height: Theme.bottomBarHeight
    color: _effectiveBarColor

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

            ThemedIcon {
                anchors.centerIn: parent
                source: "qrc:/icons/back.svg"
                iconSize: Theme.scaled(28)
                color: root.contentColor
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
            color: root.contentColor
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
            color: root.contentColor
            font: Theme.bodyFont
        }
    }
}
