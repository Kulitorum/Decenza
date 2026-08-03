import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import Decenza

// Round gear button that opens the graph options menu, for the shot detail, post-shot review
// and shot comparison pages.
//
// Those three pages each carried their own copy of this button, and each copy toggled
// advanced curves directly. The button now opens GraphOptionsDialog, where advanced curves
// sits alongside the flow scale — so the three copies collapse to this one and the option
// set can grow without every page needing an edit.
Rectangle {
    id: optionsButton

    Layout.preferredWidth: Theme.scaled(36)
    Layout.preferredHeight: Theme.scaled(36)
    Layout.alignment: Qt.AlignVCenter
    radius: Theme.scaled(18)
    // Tinted while advanced curves are on, which is what the button meant before it became a
    // menu — the state stays glanceable without opening it.
    color: Settings.graph.advancedMode ? Theme.accentColor : Theme.cardBackgroundColor
    border.color: Theme.borderColor
    border.width: Theme.scaled(1)

    Accessible.ignored: true

    Image {
        anchors.centerIn: parent
        source: "qrc:/icons/settings.svg"
        sourceSize.width: Theme.scaled(18)
        sourceSize.height: Theme.scaled(18)

        layer.enabled: true
        layer.smooth: true
        layer.effect: MultiEffect {
            colorization: 1.0
            colorizationColor: Settings.graph.advancedMode ? Theme.primaryContrastColor : Theme.textColor
        }
    }

    AccessibleMouseArea {
        anchors.fill: parent
        // Announces opening a menu, not toggling a mode: activating it no longer changes
        // anything by itself.
        accessibleName: TranslationManager.translate("graph.options.open", "Graph options")
        accessibleItem: optionsButton
        accessibleRole: Accessible.Button
        onAccessibleClicked: optionsMenu.open()
    }

    GraphOptionsDialog {
        id: optionsMenu
        parent: Overlay.overlay
    }
}
