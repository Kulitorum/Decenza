import QtQuick
import QtQuick.Controls
import Decenza

Rectangle {
    id: statusBarRoot

    // Parse the statusBar zone + its per-zone options from layout config so the
    // top bar honors distribution / alignment / style like every other bar zone
    // (composable-status-bar — no longer a special-cased renderer).
    property var _layout: {
        try { return JSON.parse(LayoutService.layoutConfiguration) || ({}) }
        catch (e) { return ({}) }
    }
    property var statusBarItems: (_layout.zones && _layout.zones.statusBar) || []
    property var zoneOpts: (_layout.zoneOptions && _layout.zoneOptions.statusBar) || ({})

    // Default keeps the surface background; a non-standard style overrides it.
    // With the glass chrome on, go semi-transparent (keeping
    // the bar's own hue as a scrim) so the image extends behind the bar
    // instead of stopping at its edge.
    readonly property color _opaqueColor: (zoneOpts.style && zoneOpts.style !== "standard")
           ? Theme.zoneBackgroundColor(zoneOpts.style)
           : Theme.surfaceColor
    color: Theme.glassChrome
           ? Theme.chromeFill(_opaqueColor)
           : _opaqueColor

    LayoutBarZone {
        anchors.fill: parent
        anchors.leftMargin: Theme.chartMarginSmall
        anchors.rightMargin: Theme.spacingLarge + (fullscreenButton.visible ? fullscreenButton.width : 0)
        zoneName: "statusBar"
        items: statusBarRoot.statusBarItems
        distribution: statusBarRoot.zoneOpts.distribution || "packed"
        alignment: statusBarRoot.zoneOpts.alignment || "center"
        zoneStyle: statusBarRoot.zoneOpts.style || "standard"
    }

    AccessibleButton {
        id: fullscreenButton
        visible: BrowserDisplay.isBrowser && !BrowserDisplay.standalone
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingSmall
        anchors.verticalCenter: parent.verticalCenter
        width: Theme.scaled(44)
        height: Math.min(parent.height, Theme.scaled(44))
        leftPadding: 0
        rightPadding: 0
        icon.source: BrowserDisplay.fullscreen ? "qrc:/icons/fullscreen-exit.svg" : "qrc:/icons/fullscreen.svg"
        icon.width: Theme.scaled(22)
        icon.height: Theme.scaled(22)
        tintIcon: true
        accessibleName: BrowserDisplay.fullscreen
            ? TranslationManager.translate("browser.exitFullscreen", "Exit fullscreen")
            : TranslationManager.translate("browser.enterFullscreen", "Enter fullscreen")
        ToolTip.visible: hovered
        ToolTip.text: accessibleName
        onClicked: {
            if (BrowserDisplay.fullscreenSupported) BrowserDisplay.toggleFullscreen()
            else (fullscreenHelp.ensure() as DecenzaDialog)?.open()
        }
    }

    Connections {
        target: BrowserDisplay
        function onFullscreenFailed() {
            const message = TranslationManager.translate("browser.fullscreenFailed",
                "The browser could not change fullscreen mode. Please try again.")
            fullscreenError.show(message)
            if (AccessibilityManager.enabled) AccessibilityManager.announce(message, true)
        }
    }
    StatusToast { id: fullscreenError; parent: Overlay.overlay; error: true }
    OnDemandLoader {
        id: fullscreenHelp
        sourceComponent: DecenzaDialog {
            id: fullscreenHelpDialog
            parent: Overlay.overlay
            title: TranslationManager.translate("browser.fullscreen", "Fullscreen")
            anchors.centerIn: Overlay.overlay
            width: Math.min(Theme.scaled(430), Overlay.overlay.width - Theme.scaled(24))
            standardButtons: Dialog.Ok
            contentItem: Text {
                width: fullscreenHelpDialog.availableWidth
                text: TranslationManager.translate("browser.homeScreenHint", "Fullscreen is not available in this browser. On iPhone, use Safari’s Share → Add to Home Screen, enable Open as Web App, then open Decenza from its icon. Turn off Portrait Orientation Lock to rotate to landscape.")
                color: Theme.textColor
                font: Theme.bodyFont
                wrapMode: Text.WordWrap
            }
        }
    }
}
