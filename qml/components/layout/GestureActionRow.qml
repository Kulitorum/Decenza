// ONE gesture-selector row: a label ("Long:"), the currently-assigned action, and a tap
// that opens the action picker.
//
// It existed three times in CustomEditorPopup — click, long-press, double-click — as
// ~40 identical lines each differing in a label and a property name. The built-in action
// widgets now need the same row (layout-widget-gesture-overrides), which would have made
// it five copies, so it is a component instead. Both editors' rows are this file.
//
// `reserved` is the state a one-slot widget's other gesture is in: shown, explained, and
// not editable, because that gesture is what keeps the widget's page reachable. Presenting
// it rather than hiding it is the difference between "you may not" and "nothing here".

import QtQuick
import QtQuick.Layouts
import Decenza

Rectangle {
    id: row

    // "Click:" / "Long:" / "DblClk:"
    required property string gestureLabel
    // Stored action id, "" when unset.
    required property string actionId
    // Human-readable label for actionId, resolved by the owner (it knows whether to
    // expand a parameterized action).
    required property string actionLabel
    // For accessibility: "Long press action" etc.
    property string accessibleLabel: gestureLabel
    // Reserved slot: this gesture is spoken for and cannot be reassigned.
    property bool reserved: false
    // What the reserved gesture opens, already translated ("Opens Steam").
    property string reservedLabel: ""

    signal picked()

    Layout.fillWidth: true
    Layout.preferredHeight: Theme.scaled(28)
    radius: Theme.scaled(4)
    color: Theme.backgroundColor
    border.color: row.reserved ? Theme.borderColor
                               : (row.actionId ? Theme.primaryColor : Theme.borderColor)
    border.width: 1
    opacity: row.reserved ? 0.6 : 1.0

    Accessible.role: Accessible.Button
    Accessible.name: row.accessibleLabel + ", "
                     + (row.reserved ? row.reservedLabel : row.actionLabel)
    Accessible.description: row.reserved
        ? TranslationManager.translate("gesturerow.accessible.reserved",
              "Reserved so this widget's page stays reachable. Clear the other gesture to free it.")
        : ""
    Accessible.focusable: !row.reserved
    Accessible.onPressAction: if (!row.reserved) row.picked()

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.scaled(4)
        anchors.rightMargin: Theme.scaled(4)

        Text {
            text: row.gestureLabel
            color: Theme.textSecondaryColor
            font: Theme.captionFont
            Accessible.ignored: true
        }
        Text {
            Layout.fillWidth: true
            text: row.reserved ? row.reservedLabel : row.actionLabel
            color: row.reserved ? Theme.textSecondaryColor
                                : (row.actionId ? Theme.primaryColor : Theme.textColor)
            font: Theme.captionFont
            elide: Text.ElideRight
            Accessible.ignored: true
        }
    }

    MouseArea {
        anchors.fill: parent
        enabled: !row.reserved
        onClicked: row.picked()
    }
}
