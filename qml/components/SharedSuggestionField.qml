// Bound lets the popup delegate resolve ids from this component. Its injected model
// roles are declared required below so they remain available with Bound enabled.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Window

// Shared editing and autocomplete logic. Host-specific buttons and the browse-all
// dialog are provided by a wrapper through the signals below.
Item {
    id: root

    required property FormControlStyle controlStyle

    // Window.window is an attached property and is not tied to a host singleton.
    readonly property var hostWindow: Window.window

    property string label: ""
    property string text: ""
    property var suggestions: []
    property var descriptions: ({})
    property string accessibleName: ""
    property string noMatchesText: "No matches - press Enter to add"
    property color fieldColor: controlStyle.insetBackgroundColor
    property bool accessibilityMode: false
    readonly property bool _accessibilityMode: accessibilityMode
    property Item nextFocusItem: null
    property Item previousFocusItem: null
    property real textRightPadding: controlStyle.scaled(68)
    property real actionRowHeight: 0
    readonly property alias textField: textInput
    readonly property alias popupVisible: suggestionPopup.visible

    implicitHeight: (root.label.length > 0 ? fieldLabel.height + root.controlStyle.scaled(2) : 0)
                    + root.controlStyle.scaled(36) + root.actionRowHeight

    // A caller may update text from a binding. Keep the internal TextField in sync
    // without writing to this property from keystroke handlers (which would erase
    // the caller's binding).
    onTextChanged: {
        if (textInput.text !== root.text) {
            textInput.text = root.text
            if (!textInput.activeFocus)
                textInput.cursorPosition = 0
        }
    }

    property bool justSelected: false
    property bool isActivelyTyping: false

    signal textEdited(string text)
    signal suggestionSelected(string text)
    signal inputFocused(Item field)
    signal inputBlurred()
    signal keyboardShowRequested()
    signal keyboardHideRequested()
    signal browseRequested()
    signal closeBrowseRequested()

    function clearText() {
        root.justSelected = false
        root.isActivelyTyping = false
        textInput.text = ""
        root.textEdited("")
        suggestionPopup.close()
    }

    function closeSuggestions() {
        suggestionPopup.close()
        root.closeBrowseRequested()
    }

    function selectSuggestion(selectedText: string): void {
        root.justSelected = true
        root.isActivelyTyping = false
        root.closeSuggestions()
        textInput.text = selectedText
        root.textEdited(selectedText)
        root.suggestionSelected(selectedText)

        // Preserve the native workaround for QTBUG-146020: the iOS tap recognizer
        // queued a focus-view read after dispatch (Qt 6.10.3
        // qtbase/src/plugins/platforms/ios/qiostextinputoverlay.mm:982).
        // Keep the deferred transfer to the non-input container, rather than
        // letting focus traversal select another text field.
        Qt.callLater(function() {
            root.forceActiveFocus()
            root.keyboardHideRequested()
        })
    }

    function openSuggestionsDialog() {
        root.isActivelyTyping = false
        suggestionPopup.close()
        Qt.callLater(function() {
            root.forceActiveFocus()
            root.keyboardHideRequested()
            root.browseRequested()
        })
    }

    function closeSuggestionsIfBlurred() {
        if (!textInput.activeFocus && !root.justSelected)
            suggestionPopup.close()
        root.justSelected = false
    }

    function getFilteredSuggestions() {
        var query = textInput.displayText
        if (!root.isActivelyTyping || !query || query.length === 0)
            return root.suggestions

        var filter = query.toLowerCase()
        var filtered = []
        for (let i = 0; i < root.suggestions.length; i++) {
            if (root.suggestions[i].toLowerCase().indexOf(filter) !== -1)
                filtered.push(root.suggestions[i])
        }
        return filtered
    }

    onVisibleChanged: {
        if (!root.visible)
            root.closeSuggestions()
    }

    Text {
        id: fieldLabel
        anchors.left: parent.left
        anchors.top: parent.top
        text: root.label
        color: root.controlStyle.textColor
        font.pixelSize: root.controlStyle.scaled(14)
        visible: root.label.length > 0
    }

    SharedTextField {
        id: textInput
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: root.label.length > 0 ? fieldLabel.bottom : parent.top
        anchors.topMargin: root.label.length > 0 ? root.controlStyle.scaled(2) : 0
        height: root.controlStyle.scaled(36)
        controlStyle: root.controlStyle
        fieldColor: root.fieldColor
        accessibilityMode: root.accessibilityMode
        text: root.text
        placeholder: root.label
        EnterKey.type: Qt.EnterKeyDone
        inputMethodHints: Qt.ImhNoPredictiveText
        rightPadding: root.textRightPadding
        KeyNavigation.tab: root.nextFocusItem
        KeyNavigation.backtab: root.previousFocusItem

        onKeyboardShowRequested: root.keyboardShowRequested()
        onKeyboardHideRequested: root.keyboardHideRequested()

        onTextEdited: {
            root.isActivelyTyping = true
            root.textEdited(text)
        }

        onDisplayTextChanged: {
            if (!activeFocus || root.justSelected)
                return
            if (displayText.length === 0) {
                suggestionPopup.close()
            } else {
                root.isActivelyTyping = true
                if (root.getFilteredSuggestions().length > 0)
                    suggestionPopup.open()
            }
        }

        onActiveFocusChanged: {
            if (activeFocus) {
                root.justSelected = false
                root.isActivelyTyping = false
                root.inputFocused(textInput)
            } else {
                root.textEdited(text)
                root.inputBlurred()
                Qt.callLater(root.closeSuggestionsIfBlurred)
            }
        }

        Keys.onReturnPressed: {
            if (suggestionPopup.visible && suggestionList.count > 0) {
                let matches = root.getFilteredSuggestions()
                let pick = -1
                if (suggestionList.currentIndex >= 0)
                    pick = suggestionList.currentIndex
                else if (matches.length === 1)
                    pick = 0
                else {
                    let exact = false
                    for (let i = 0; i < matches.length; i++) {
                        if (matches[i].toLowerCase() === text.toLowerCase()) {
                            exact = true
                            break
                        }
                    }
                    if (!exact && matches.length > 0)
                        pick = 0
                }
                if (pick >= 0 && pick < matches.length) {
                    root.selectSuggestion(matches[pick])
                    focus = false
                    return
                }
            }

            root.textEdited(text)
            suggestionPopup.close()
            focus = false
        }

        Keys.onEscapePressed: {
            suggestionPopup.close()
            focus = false
        }

        Keys.onDownPressed: {
            if (suggestionPopup.visible && suggestionList.count > 0)
                suggestionList.currentIndex = Math.min(suggestionList.currentIndex + 1,
                                                       suggestionList.count - 1)
        }

        Keys.onUpPressed: {
            if (suggestionPopup.visible && suggestionList.count > 0)
                suggestionList.currentIndex = Math.max(suggestionList.currentIndex - 1, 0)
        }

        Accessible.role: Accessible.EditableText
        Accessible.name: root.accessibleName.length > 0 ? root.accessibleName : root.label
        Accessible.description: echoMode === TextInput.Normal ? text : ""
    }

    Popup {
        id: suggestionPopup
        x: textInput.x
        y: {
            var _visible = visible  // Re-evaluate on open; mapToItem is not reactive.
            var below = textInput.y + textInput.height
            var win = root.hostWindow
            if (win) {
                let fieldTopGlobal = textInput.mapToItem(null, 0, 0).y
                let fieldBottomGlobal = fieldTopGlobal + textInput.height
                let spaceBelow = win.height - fieldBottomGlobal
                let spaceAbove = fieldTopGlobal
                if (implicitHeight + root.controlStyle.scaled(4) > spaceBelow && spaceAbove > spaceBelow)
                    return textInput.y - implicitHeight - root.controlStyle.scaled(2)
            }
            return below
        }
        width: textInput.width
        implicitHeight: Math.min(suggestionList.contentHeight + 2, root.controlStyle.scaled(250))
        padding: 1
        closePolicy: Popup.CloseOnPressOutside

        background: Rectangle {
            color: root.controlStyle.surfaceColor
            border.color: root.controlStyle.borderColor
            radius: root.controlStyle.scaled(4)
        }

        contentItem: ListView {
            id: suggestionList
            clip: true
            model: root.getFilteredSuggestions()
            currentIndex: -1
            // A fresh filter has no deliberate keyboard selection. ListView may
            // select row zero when its model changes (Qt 6.12
            // qtdeclarative/src/quick/items/qquickitemview.cpp:1169); reset before Return
            // decides between an exact free-text value and a selected suggestion.
            onModelChanged: currentIndex = -1

            delegate: ItemDelegate {
                id: suggestionDelegate
                required property var modelData
                required property int index

                width: suggestionList.width
                height: suggestionDelegate.itemDesc.length > 0
                    ? root.controlStyle.scaled(58) : root.controlStyle.scaled(44)
                highlighted: suggestionDelegate.index === suggestionList.currentIndex
                property string itemText: suggestionDelegate.modelData
                property string itemDesc: root.descriptions
                    && root.descriptions[suggestionDelegate.modelData] !== undefined
                    ? String(root.descriptions[suggestionDelegate.modelData]) : ""

                contentItem: Column {
                    spacing: root.controlStyle.scaled(1)
                    Text {
                        text: suggestionDelegate.itemText
                        color: root.controlStyle.textColor
                        font.pixelSize: root.controlStyle.scaled(18)
                        leftPadding: root.controlStyle.scaled(12)
                        width: suggestionDelegate.width - root.controlStyle.scaled(12)
                        elide: Text.ElideRight
                    }
                    Text {
                        visible: suggestionDelegate.itemDesc.length > 0
                        text: suggestionDelegate.itemDesc
                        color: root.controlStyle.textSecondaryColor
                        font.pixelSize: root.controlStyle.scaled(13)
                        leftPadding: root.controlStyle.scaled(12)
                        width: suggestionDelegate.width - root.controlStyle.scaled(12)
                        elide: Text.ElideRight
                    }
                }

                Accessible.role: Accessible.Button
                Accessible.name: suggestionDelegate.itemDesc.length > 0
                    ? suggestionDelegate.itemText + ", " + suggestionDelegate.itemDesc
                    : suggestionDelegate.itemText
                Accessible.focusable: true
                Accessible.onPressAction: root.selectSuggestion(suggestionDelegate.itemText)

                background: Rectangle {
                    color: suggestionDelegate.highlighted || suggestionDelegate.hovered
                        ? root.controlStyle.primaryColor : "transparent"
                    opacity: suggestionDelegate.highlighted || suggestionDelegate.hovered ? 0.2 : 1
                }

                onClicked: root.selectSuggestion(suggestionDelegate.itemText)
            }

            Text {
                anchors.centerIn: parent
                text: root.noMatchesText
                color: root.controlStyle.textSecondaryColor
                font.pixelSize: root.controlStyle.scaled(14)
                visible: suggestionList.count === 0 && textInput.displayText.length > 0
            }
        }
    }
}
