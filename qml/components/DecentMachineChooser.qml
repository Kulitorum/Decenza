import QtQuick
import Decenza

// Shared by the native root shell and the browser shell so an account link that
// needs a machine choice has one identical picker and selection behavior.
SelectionDialog {
    id: chooser
    property var serials: []
    signal serialChosen(string serial)

    title: TranslationManager.translate("decent.machine.chooseTitle", "Which Decent machine is this?")

    function openWith(serialValues, displayLabels) {
        serials = serialValues || []
        options = displayLabels || []
        open()
    }

    onSelected: function(index, value) {
        if (index >= 0 && index < serials.length)
            serialChosen(serials[index])
    }
}
