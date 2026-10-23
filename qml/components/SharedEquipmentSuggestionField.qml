import QtQuick

// Complete suggestion input used by the shared equipment form: the shared text
// field, its shared inline/accessibility chrome, and the shared browse dialog.
Item {
    id: root

    required property EquipmentDialogStyle equipmentStyle
    required property EquipmentDialogText strings
    property string label: ""
    property string accessibleName: label
    property string text: ""
    property var suggestions: []
    property var descriptions: ({})
    property Item nextFocusItem: null
    property Item previousFocusItem: null
    readonly property alias textField: suggestionInput.textField
    readonly property alias popupVisible: suggestionInput.popupVisible

    signal textEdited(string text)
    signal suggestionSelected(string text)

    implicitHeight: suggestionInput.implicitHeight
    implicitWidth: suggestionInput.implicitWidth

    SharedSuggestionField {
        id: suggestionInput
        anchors.fill: parent
        controlStyle: root.equipmentStyle.formControlStyle
        label: root.label
        accessibleName: root.accessibleName
        text: root.text
        suggestions: root.suggestions
        descriptions: root.descriptions
        accessibilityMode: root.equipmentStyle.buttonInteraction.accessibilityEnabled
        textRightPadding: accessibilityMode
                          ? controlStyle.scaled(12) : controlStyle.scaled(68)
        actionRowHeight: actionChrome.actionRowHeight
        noMatchesText: root.strings.noMatches
        nextFocusItem: root.nextFocusItem
        previousFocusItem: root.previousFocusItem

        onTextEdited: function(value) { root.textEdited(value) }
        onSuggestionSelected: function(value) { root.suggestionSelected(value) }
        onKeyboardShowRequested: root.equipmentStyle.keyboardInteraction.showRequested()
        onKeyboardHideRequested: root.equipmentStyle.keyboardInteraction.hideRequested()
        onBrowseRequested: suggestionsDialog.open()
        onCloseBrowseRequested: suggestionsDialog.close()
    }

    SharedSuggestionFieldChrome {
        id: actionChrome
        anchors.fill: parent
        field: suggestionInput
        buttonStyle: root.equipmentStyle.buttonStyle
        interaction: root.equipmentStyle.buttonInteraction
        accessibilityMode: root.equipmentStyle.buttonInteraction.accessibilityEnabled
        clearLabel: root.strings.clearText
        openLabel: root.strings.openSuggestions
    }

    SharedSelectionDialog {
        id: suggestionsDialog
        dialogStyle: root.equipmentStyle.dialogStyle
        buttonInteraction: root.equipmentStyle.buttonInteraction
        title: root.accessibleName.length > 0 ? root.accessibleName : root.label
        options: root.suggestions
        currentValue: root.text
        emptyStateText: root.strings.noSuggestions
        selectedLabel: root.strings.selected
        noneLabel: root.strings.none
        cancelLabel: root.strings.cancelSelection
        onSelected: function(index, value) { suggestionInput.selectSuggestion(value) }
    }
}
