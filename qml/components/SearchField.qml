import QtQuick
import QtQuick.Layouts
import Decenza

// The list-search field shared by Recipes, Beans and Shot History: the field, its
// clear button, and a debounce. The clear (×) sits inside the field; with a screen
// reader on it moves to a separate button beside it, where TalkBack can reach it.
RowLayout {
    id: root

    property alias field: input
    property alias placeholder: input.placeholder
    property alias accessibleName: input.accessibleName
    property int debounceMs: 250

    // A user edit, at once (trimmed, repeats dropped): for state a new query resets.
    signal edited(string text)
    // The query to run, after debounceMs without further edits.
    signal queryChanged(string query)

    // Sets the text without edited/queryChanged, for a page restoring a search
    // it has already applied.
    function setTextSilently(text) {
        _silent = true
        input.text = text
        _lastEdited = String(text).trim()
        _silent = false
    }

    // Empties the field and reports the empty query at once.
    function clear() {
        input.text = ""
        input.focus = false
        debounce.stop()
        root.queryChanged("")
    }

    property string _lastEdited: ""
    property bool _silent: false

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
            const text = displayText.trim()
            if (root._silent || text === root._lastEdited)
                return
            root._lastEdited = text
            root.edited(text)
            debounce.restart()
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
        accessibleName: TranslationManager.translate("shothistory.clearsearch", "Clear search")
        icon.source: "qrc:/icons/cross.svg"
        onClicked: root.clear()
    }

    Timer {
        id: debounce
        interval: root.debounceMs
        onTriggered: root.queryChanged(input.displayText.trim())
    }
}
