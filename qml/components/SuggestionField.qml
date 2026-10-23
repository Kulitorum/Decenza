import QtQuick
import Decenza

// Native host adapter for the shared editing/filtering control and its shared
// action chrome. Native browse presentation is the same SelectionDialog used
// elsewhere in the app.
SharedSuggestionField {
    id: root

    NativeFormControlStyle { id: nativeFormStyle }
    NativeButtonStyle { id: nativeButtonStyle }
    NativeButtonInteraction { id: nativeInteraction }
    NativeDialogStyle { id: nativeDialogStyle }

    controlStyle: nativeFormStyle
    accessibilityMode: AccessibilityManager.enabled
    textRightPadding: root.accessibilityMode ? Theme.scaled(12) : Theme.scaled(68)
    actionRowHeight: actionChrome.actionRowHeight
    noMatchesText: TranslationManager.translate(
        "suggestionfield.nomatches", "No matches - press Enter to add")

    onKeyboardShowRequested: Keyboard.show()
    onKeyboardHideRequested: Keyboard.hide()
    onBrowseRequested: suggestionsDialog.open()
    onCloseBrowseRequested: suggestionsDialog.close()

    SharedSuggestionFieldChrome {
        id: actionChrome
        anchors.fill: parent
        field: root
        buttonStyle: nativeButtonStyle
        interaction: nativeInteraction
        accessibilityMode: root.accessibilityMode
        clearLabel: TranslationManager.translate("suggestionfield.clear", "Clear text")
        openLabel: TranslationManager.translate("suggestionfield.openDropdown", "Open suggestions")
    }

    SharedSelectionDialog {
        id: suggestionsDialog
        dialogStyle: nativeDialogStyle
        buttonInteraction: nativeInteraction
        title: root.accessibleName.length > 0 ? root.accessibleName : root.label
        options: root.suggestions
        currentValue: root.text
        emptyStateText: TranslationManager.translate(
            "suggestionfield.nosuggestions", "No suggestions available")
        selectedLabel: TranslationManager.translate("combobox.selected", "Selected")
        noneLabel: TranslationManager.translate("combobox.empty", "None")
        cancelLabel: TranslationManager.translate("combobox.cancel", "Cancel")
        onSelected: function(index, value) { root.selectSuggestion(value) }
    }
}
