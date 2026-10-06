import QtQuick
import QtQuick.Layouts
import Decenza

// The search field shared by list pages and pickers: the field and its clear
// button. The clear (×) sits inside the field; in accessibility mode
// (AccessibilityManager.enabled) it moves to a separate button beside it, where
// TalkBack can reach it.
RowLayout {
    id: root

    property alias field: input
    property alias placeholder: input.placeholder
    property alias accessibleName: input.accessibleName

    // The query changed (trimmed): on every user edit, the IME's in-progress
    // word included, and on clear(). Not on setTextSilently().
    signal queryChanged(string query)

    // Sets the text without queryChanged, for a page restoring a search it has
    // already applied.
    function setTextSilently(text) {
        _silent = true
        input.clear()   // also drops an IME preedit, which setting text keeps
        input.text = text
        _lastQuery = String(text).trim()
        _silent = false
    }

    // Empties the field, IME preedit included, and reports the empty query.
    function clear() {
        input.clear()
        input.focus = false
    }

    property string _lastQuery: ""
    property bool _silent: false

    // A nested layout fills height unless told otherwise (qquicklayout_p.h:238-239),
    // and the field below fills ours: unset, Shot History's search took the list's height.
    Layout.fillHeight: false
    spacing: Theme.spacingSmall

    StyledTextField {
        id: input
        Layout.fillWidth: true
        Layout.fillHeight: true   // follows a height the page sets on the SearchField
        rightPadding: inlineClear.visible ? Theme.scaled(36) : Theme.scaled(12)
        // displayText includes the IME preedit, so the query follows each keystroke
        // even on IMEs that ignore ImhNoPredictiveText (Gboard does).
        inputMethodHints: Qt.ImhNoPredictiveText
        onDisplayTextChanged: {
            const query = displayText.trim()
            if (root._silent || query === root._lastQuery)
                return
            root._lastQuery = query
            root.queryChanged(query)
        }

        Item {
            id: inlineClear
            width: Theme.scaled(20)
            height: Theme.scaled(20)
            visible: input.displayText.length > 0 && !AccessibilityManager.enabled
            anchors.right: parent.right
            anchors.rightMargin: Theme.scaled(10)
            anchors.verticalCenter: parent.verticalCenter

            ColoredIcon {
                anchors.centerIn: parent
                source: "qrc:/icons/cross.svg"
                iconWidth: Theme.scaled(14)
                iconHeight: Theme.scaled(14)
                iconColor: Theme.textSecondaryColor
            }

            MouseArea {
                anchors.fill: parent
                anchors.margins: -Theme.scaled(6)
                onClicked: root.clear()
            }
        }
    }

    AccessibleButton {
        visible: input.displayText.length > 0 && AccessibilityManager.enabled
        accessibleName: TranslationManager.translate("common.accessible.clearSearch", "Clear search")
        icon.source: "qrc:/icons/cross.svg"
        onClicked: root.clear()
    }
}
