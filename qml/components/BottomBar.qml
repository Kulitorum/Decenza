import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

Item {
    id: root

    property string title: ""
    property color barColor: Theme.bottomBarColor
    property bool showBackButton: true
    property string rightText: ""  // Simple right-aligned text
    default property alias content: contentRow.data  // Custom content goes here

    signal backClicked()

    readonly property color contentColor: Theme.iconColor

    anchors.left: parent.left
    anchors.right: parent.right
    anchors.bottom: parent.bottom
    height: Theme.bottomBarHeight

    Rectangle {
        id: bgRect
        anchors.fill: parent
        // When a custom background image is active, every bar uses the same
        // neutral surface scrim as StatusBar and the content cards, so the
        // wallpaper shows through and all bars read consistently — the page's
        // own barColor (e.g. "transparent" on Beans/Equipment/Recipes) only
        // applies when no background image is set.
        color: Settings.theme.backgroundImagePath.length > 0
               ? Theme.scrimColor(Theme.surfaceColor)
               : root.barColor
        // A translucent, full-width bar flush against the window's bottom edge
        // with only the page background behind it gets mis-sorted into the Qt
        // Quick scene graph's OPAQUE batch (a cross-platform renderer quirk), so
        // its alpha is dropped and the wallpaper can't show through. An opacity
        // node forces the subtree through the alpha pass and restores blending;
        // layer.enabled does not (its composite lands at the same edge). Only
        // needed while the scrim is active. (opaque-bottom-bar fix.)
        opacity: Settings.theme.backgroundImagePath.length > 0 ? 0.99 : 1.0
    }

    // Top border for separation
    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 1
        color: Theme.borderColor
    }

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
            Layout.maximumWidth: root.width * 0.5
            elide: Text.ElideRight
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
            elide: Text.ElideRight
            Layout.maximumWidth: root.width * 0.4
        }
    }
}
