import QtQuick
import QtQuick.Controls
import Decenza

// Autocomplete text field that shows filtered suggestions as you type
Item {
    id: root

    property string label: ""
    property string text: ""
    property var suggestions: []  // List of existing values from database
    property string accessibleName: ""  // Explicit accessible name for screen readers (overrides label)
    property alias textField: textInput  // Expose internal text input for KeyboardAwareContainer registration

    signal textEdited(string text)
    signal suggestionSelected(string text)  // Emitted when user picks from dropdown (not on keystroke)
    signal inputFocused(Item field)  // Emitted when text input gets focus (for keyboard handling)
    signal inputBlurred()  // Emitted when text input loses focus

    // Accessibility mode: show buttons below text field instead of overlapping
    readonly property bool _accessibilityMode: typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled

    implicitHeight: (root.label.length > 0 ? fieldLabel.height + Theme.scaled(2) : 0)
                    + Theme.scaled(48)
                    + (_accessibilityMode && (textInput.text.length > 0 || suggestions.length > 0)
                       ? Theme.scaled(44) + Theme.scaled(4) : 0)

    // Sync textInput when root.text changes from parent binding
    onTextChanged: {
        if (textInput.text !== text) {
            textInput.text = text
        }
    }

    // Handle selection from suggestion list (called from delegate)
    function selectSuggestion(selectedText) {
        justSelected = true
        isActivelyTyping = false  // Reset - this is a selection, not typing
        suggestionPopup.close()
        suggestionsDialog.close()
        textInput.text = selectedText
        // Don't set root.text directly - emit signal and let parent update via binding
        root.textEdited(selectedText)
        root.suggestionSelected(selectedText)
        textInput.focus = false
        Qt.inputMethod.hide()
    }

    // Close dialog when field becomes invisible (page popped, tab switched)
    onVisibleChanged: if (!visible) suggestionsDialog.close()

    // Open the suggestions dialog (for arrow button and accessibility)
    function openSuggestionsDialog() {
        isActivelyTyping = false  // Show all suggestions
        suggestionPopup.close()   // Close typing popup before opening modal dialog
        suggestionsDialog.open()
    }

    // Track if user is actively typing (vs just focusing with existing text)
    property bool isActivelyTyping: false

    // Filter suggestions based on current input
    function getFilteredSuggestions() {
        // Show all suggestions when not actively typing (just focused with existing text)
        if (!isActivelyTyping || !textInput.text || textInput.text.length === 0) {
            return suggestions
        }
        var filter = textInput.text.toLowerCase()
        var filtered = []
        for (var i = 0; i < suggestions.length; i++) {
            if (suggestions[i].toLowerCase().indexOf(filter) !== -1) {
                filtered.push(suggestions[i])
            }
        }
        return filtered
    }

    Text {
        id: fieldLabel
        anchors.left: parent.left
        anchors.top: parent.top
        text: root.label
        color: Theme.textColor
        font.pixelSize: Theme.scaled(14)
        visible: root.label.length > 0
    }

    // Text input with dropdown
    StyledTextField {
        id: textInput
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: root.label.length > 0 ? fieldLabel.bottom : parent.top
        anchors.topMargin: root.label.length > 0 ? Theme.scaled(2) : 0
        height: Theme.scaled(48)
        text: root.text
        placeholder: root.label
        EnterKey.type: Qt.EnterKeyDone

        // Make room for buttons on the right (only in normal mode)
        rightPadding: root._accessibilityMode ? Theme.scaled(12) : Theme.scaled(84)

        onTextEdited: {
            // User is actively typing (not just focusing)
            isActivelyTyping = true
            // Don't set root.text here - that breaks the parent binding!
            // Just emit the signal and let parent update via its binding
            root.textEdited(text)
            // Show dropdown when typing if we have matching suggestions (but not after selection)
            if (!justSelected && text.length > 0 && getFilteredSuggestions().length > 0) {
                suggestionPopup.open()
            }
        }

        onActiveFocusChanged: {
            if (activeFocus) {
                justSelected = false  // Reset so typing works again
                isActivelyTyping = false  // Reset - show all suggestions initially
                root.inputFocused(textInput)
                // Show suggestions when focused (popup for typing, not dialog)
                if (suggestions.length > 0 && !root._accessibilityMode) {
                    suggestionPopup.open()
                }
            } else {
                // Emit textEdited to ensure value is committed when losing focus
                // Don't set root.text here - that breaks the parent binding!
                root.textEdited(text)
                root.inputBlurred()
                // Defer popup close — if the user clicked a popup item,
                // selectSuggestion() will have set justSelected = true by now
                Qt.callLater(closeSuggestionsIfBlurred)
            }
        }

        Keys.onReturnPressed: {
            // Accept current text and close
            // Don't set root.text = text - that breaks the parent binding!
            root.textEdited(text)
            suggestionPopup.close()
            focus = false
        }

        Keys.onEscapePressed: {
            suggestionPopup.close()
            focus = false
        }

        Keys.onDownPressed: {
            if (suggestionPopup.visible && suggestionList.count > 0) {
                suggestionList.currentIndex = Math.min(suggestionList.currentIndex + 1, suggestionList.count - 1)
            }
        }

        Keys.onUpPressed: {
            if (suggestionPopup.visible && suggestionList.count > 0) {
                suggestionList.currentIndex = Math.max(suggestionList.currentIndex - 1, 0)
            }
        }

        Accessible.role: Accessible.EditableText
        Accessible.name: root.accessibleName.length > 0 ? root.accessibleName : root.label
        Accessible.description: text
    }

    // Inline buttons row (normal mode only — hidden in accessibility mode)
    Row {
        id: inlineButtons
        visible: !root._accessibilityMode
        anchors.right: textInput.right
        anchors.rightMargin: Theme.scaled(4)
        anchors.verticalCenter: textInput.verticalCenter
        spacing: Theme.scaled(4)

        // Clear button (X in circle) - only when there's text
        Rectangle {
            visible: textInput.text.length > 0
            width: Theme.scaled(36)
            height: Theme.scaled(36)
            radius: Theme.scaled(18)
            color: clearArea.pressed ? Theme.surfaceColor : Theme.backgroundColor
            border.color: Theme.textSecondaryColor
            border.width: 1
            Accessible.ignored: true

            Text {
                anchors.centerIn: parent
                text: "\u00D7"  // multiplication sign
                color: Theme.textSecondaryColor
                font.pixelSize: Theme.scaled(21)
                Accessible.ignored: true
            }

            MouseArea {
                id: clearArea
                anchors.fill: parent
                onClicked: {
                    justSelected = false
                    isActivelyTyping = false
                    textInput.text = ""
                    root.textEdited("")
                    if (suggestions.length > 0) {
                        suggestionPopup.open()
                    }
                }
            }
        }

        // Dropdown arrow button
        Rectangle {
            width: Theme.scaled(36)
            height: Theme.scaled(36)
            radius: Theme.scaled(18)
            color: arrowArea.pressed ? Theme.surfaceColor : "transparent"
            Accessible.ignored: true

            Text {
                anchors.centerIn: parent
                text: suggestionPopup.visible ? "\u25B2" : "\u25BC"
                color: Theme.textSecondaryColor
                font.pixelSize: Theme.scaled(18)
                Accessible.ignored: true
            }

            MouseArea {
                id: arrowArea
                anchors.fill: parent
                onClicked: {
                    if (suggestionPopup.visible) {
                        suggestionPopup.close()
                    } else {
                        root.openSuggestionsDialog()
                    }
                }
            }
        }
    }

    // Accessibility mode: separate row of labeled buttons below the text field
    Row {
        id: a11yButtons
        visible: root._accessibilityMode && (textInput.text.length > 0 || suggestions.length > 0)
        anchors.left: textInput.left
        anchors.right: textInput.right
        anchors.top: textInput.bottom
        anchors.topMargin: Theme.scaled(4)
        spacing: Theme.scaled(8)
        height: Theme.scaled(44)

        AccessibleButton {
            visible: textInput.text.length > 0
            width: visible ? implicitWidth : 0
            height: Theme.scaled(44)
            text: TranslationManager.translate("suggestionfield.clear", "Clear text")
            accessibleName: TranslationManager.translate("suggestionfield.clear", "Clear text")

            onClicked: {
                justSelected = false
                isActivelyTyping = false
                textInput.text = ""
                root.textEdited("")
                // After clearing, open suggestions dialog so user can browse
                if (suggestions.length > 0) {
                    root.openSuggestionsDialog()
                }
            }
        }

        AccessibleButton {
            visible: suggestions.length > 0
            width: visible ? implicitWidth : 0
            height: Theme.scaled(44)
            text: TranslationManager.translate("suggestionfield.openDropdown", "Open suggestions")
            accessibleName: TranslationManager.translate("suggestionfield.openDropdown", "Open suggestions")

            onClicked: {
                root.openSuggestionsDialog()
            }
        }
    }

    // Track if we just selected an item (to prevent reopening)
    property bool justSelected: false

    // Close popup after focus loss — deferred so selectSuggestion() can set justSelected first
    function closeSuggestionsIfBlurred() {
        if (!textInput.activeFocus && !justSelected) {
            suggestionPopup.close()
        }
        justSelected = false
    }

    // Typing-driven autocomplete popup (kept for live filtering while typing)
    Popup {
        id: suggestionPopup
        x: textInput.x
        y: textInput.y + textInput.height
        width: textInput.width
        implicitHeight: Math.min(suggestionList.contentHeight + 2, Theme.scaled(250))
        padding: 1
        closePolicy: Popup.CloseOnPressOutside

        background: Rectangle {
            color: Theme.surfaceColor
            border.color: Theme.borderColor
            radius: Theme.scaled(4)
        }

        contentItem: ListView {
            id: suggestionList
            clip: true
            model: getFilteredSuggestions()
            currentIndex: -1

            // Store reference to root for delegate access
            property var suggestionRoot: root

            delegate: ItemDelegate {
                id: suggestionDelegate
                width: suggestionList.width
                height: Theme.scaled(44)
                highlighted: index === suggestionList.currentIndex

                // Store reference to avoid scope issues
                property string itemText: modelData

                contentItem: Text {
                    text: suggestionDelegate.itemText
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(18)
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: Theme.scaled(12)
                }

                background: Rectangle {
                    color: highlighted || hovered ? Theme.primaryColor : "transparent"
                    opacity: highlighted || hovered ? 0.2 : 1
                }

                onClicked: {
                    var listView = suggestionDelegate.ListView.view
                    if (listView && listView.suggestionRoot) {
                        listView.suggestionRoot.selectSuggestion(suggestionDelegate.itemText)
                    }
                }
            }

            // Show message when no matches
            Text {
                anchors.centerIn: parent
                text: TranslationManager.translate("suggestionfield.nomatches", "No matches - press Enter to add")
                color: Theme.textSecondaryColor
                font.pixelSize: Theme.scaled(14)
                visible: suggestionList.count === 0 && textInput.text.length > 0
            }
        }
    }

    SelectionDialog {
        id: suggestionsDialog
        title: root.accessibleName.length > 0 ? root.accessibleName : root.label
        options: root.suggestions
        currentValue: root.text
        emptyStateText: TranslationManager.translate("suggestionfield.nosuggestions", "No suggestions available")
        onSelected: function(index, value) { root.selectSuggestion(value) }
    }
}
