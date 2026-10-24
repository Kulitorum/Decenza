import QtQuick
import Decenza

// Native translation and accessibility-service adapter for the shared picker.
SharedSelectionDialog {
    dialogStyle: NativeDialogStyle {}
    buttonInteraction: NativeButtonInteraction {}
    selectedLabel: TranslationManager.translate("combobox.selected", "Selected")
    noneLabel: TranslationManager.translate("combobox.empty", "None")
    cancelLabel: TranslationManager.translate("combobox.cancel", "Cancel")
}
