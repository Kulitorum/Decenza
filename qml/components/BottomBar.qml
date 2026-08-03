import QtQuick
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

    // Derived from the bar's own fill, not the global icon colour. Those diverged once
    // backgrounds became derivable: a light theme with a dark background colour derives
    // iconColor to WHITE while barColor stays the palette's white bar — white on white.
    readonly property color contentColor: Theme.contentColorOn(barColor, Theme.iconColor)

    // How much width the title-side content may take before it starts pushing the
    // action buttons off the end of the bar.
    //
    // A child of a Qt Quick Layout that is not itself a layout and has no explicit
    // Layout.fillWidth gets QLayoutPolicy::Fixed — it cannot shrink below its own
    // preferred width; nested layouts get Preferred unconditionally
    // (qtdeclarative/src/quicklayouts/qquicklayout.cpp:1304-1332, effectiveSizePolicy_helper).
    // A non-layout child can also reach Preferred via :1319-1329, but only under
    // Layout.useDefaultSizePolicy or the app-wide AA_QtQuickUseDefaultSizePolicy
    // attribute — this app sets neither, so Fixed is what its children get.
    // The action buttons are therefore Fixed, so an oversized leftContent does not
    // squeeze them: the row's minimum sum simply exceeds the bar and the whole row
    // is laid out past the right edge, which is what a long profile name did on
    // Shot Review / Shot Detail. Capping leftContent lowers that minimum sum, so
    // pages bind their leftContent's Layout.maximumWidth to this and the text elides.
    //
    // Floored at a couple of characters rather than at 0: Layout.maximumWidth: 0 is a
    // legal zero-width cell, so the text would vanish outright — no ellipsis, and the
    // Accessible.name on the collapsed item goes with it. Overflowing by this floor
    // beats disappearing. Note the subtraction over-counts any fillWidth/WordWrap
    // label in the content slot: the layout can shrink one, but the row still reports
    // its UNCAPPED implicit width as preferred. Give such a label its own
    // Layout.maximumWidth (Shot Review's upload status lines do) rather than leaning
    // on this floor to absorb it.
    //
    // No binding loop: nothing here depends on leftContentRow's own width. A nested
    // layout's implicitWidth is its engine-preferred size, which already clamps each
    // child to that child's Layout.maximumWidth (qquicklayout.cpp:1279, boundSize),
    // so titleRow's term needs no cap of its own — rightTextLabel is a plain Text, so
    // its raw implicitWidth does. Invisible children are dropped from the engine
    // entirely (qquicklayout.cpp:883-890, shouldIgnoreItem), hence the gap count.
    readonly property real leftContentMaxWidth: Math.max(Theme.scaled(48),
        width - titleRow.implicitWidth - contentRow.implicitWidth
              - (rightTextLabel.visible ? Math.min(rightTextLabel.implicitWidth, width * 0.4) : 0)
              - Theme.chartMarginSmall - Theme.spacingLarge
              - Theme.spacingMedium * (rightTextLabel.visible ? 4 : 3))
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
        // When the glass chrome is on, every bar uses the same
        // neutral surface scrim as StatusBar and the content cards, so the
        // wallpaper shows through and all bars read consistently — the page's
        // own barColor (e.g. "transparent" on Beans/Equipment/Recipes) only
        // applies when the glass chrome is off.
        color: Theme.glassChrome
               ? Theme.chromeFill(Theme.surfaceColor)
               : root.barColor
        // opacity < 1 forces the scrim through the alpha pass; without it this
        // bar renders opaque and the wallpaper can't show through. See
        // docs/CLAUDE_MD/QML_GOTCHAS.md "Translucent element renders opaque".
        opacity: Theme.glassChrome ? 0.99 : 1.0
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
            id: titleRow
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
            id: rightTextLabel
            visible: root.rightText !== ""
            text: root.rightText
            color: root.contentColor
            font: Theme.bodyFont
            elide: Text.ElideRight
            Layout.maximumWidth: root.width * 0.4
        }
    }
}
