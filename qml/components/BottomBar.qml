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
    // Content that belongs with the page title rather than the actions — it sits
    // before the stretch, so it stays put next to the title instead of being pushed
    // across the bar to sit against the buttons.
    property alias leftContent: leftContentRow.data

    signal backClicked()

    readonly property color contentColor: Theme.iconColor
    // Effective fill color, re-exposed for callers that mirror it (e.g.
    // CommunityBrowserPage's "Add to Library" label). The fill lives on the
    // nested bgRect, not this Item root, so alias it back to the public surface.
    property alias color: bgRect.color

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
        // opacity < 1 forces the scrim through the alpha pass; without it this
        // bar renders opaque and the wallpaper can't show through. See
        // docs/CLAUDE_MD/QML_GOTCHAS.md "Translucent element renders opaque".
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

        // spacing 0: when the back button is shown, its hitbox is deliberately a full
        // bar-height wide around a much narrower glyph, so it already supplies the gap
        // before the title. (With no back button the row collapses and the outer
        // leftMargin is the only inset.)
        RowLayout {
            spacing: 0

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
        }

        // Title-side custom content
        RowLayout {
            id: leftContentRow
            spacing: Theme.spacingMedium
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
