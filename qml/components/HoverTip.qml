// The ToolTip lives in a Component, so Bound makes `root` statically resolvable inside it.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Decenza

// A themed tooltip for a control: after a short hover on desktop, or at once on a
// long press (touch has no hover). Built the first time it is shown — controls
// carry one each and it is rarely opened (#1976) — and kept once built.
Loader {
    id: root

    property string text: ""
    property bool shown: false
    // Long press: show without the hover delay.
    property bool immediate: false

    anchors.fill: parent
    active: false
    onShownChanged: if (shown && text !== "") active = true

    sourceComponent: Component {
        ToolTip {
            id: tip
            text: root.text
            visible: root.shown && root.text !== ""
            delay: root.immediate ? 0 : 500
            width: Math.min(Theme.scaled(280), Theme.windowWidth * 0.7)

            contentItem: Text {
                text: tip.text
                font: Theme.captionFont
                color: Theme.textColor
                wrapMode: Text.Wrap
            }

            background: Rectangle {
                color: Theme.surfaceColor
                border.color: Theme.borderColor
                border.width: Theme.scaled(1)
                radius: Theme.cardRadius
            }
        }
    }
}
