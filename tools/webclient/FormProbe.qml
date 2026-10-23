pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

Item {
    id: form
    implicitHeight: formContents.implicitHeight

    property string draftName: "Feasibility equipment"
    property string draftGrinder: "Varia VS6"
    property string previewName: "Feasibility equipment"
    property string previewGrinder: "Varia VS6"
    property string lastAction: "No changes applied."

    FormControlStyle {
        id: fieldStyle
        font: Qt.font({family: "Decenza Sans", pixelSize: 16})
        textColor: "#20262c"
        textSecondaryColor: "#52606d"
        primaryColor: "#285db1"
        surfaceColor: "#ffffff"
        insetBackgroundColor: "#f6f8fa"
        borderColor: "#a8b2bc"
    }

    ColumnLayout {
        id: formContents
        width: parent.width
        spacing: 12

        Label { text: "Shared equipment form controls"; font.bold: true }
        Label {
            Layout.fillWidth: true
            text: "The text field and autocomplete are the same components used by native Decenza. Apply and Cancel affect this local preview only."
            wrapMode: Text.WordWrap
        }
        Label { text: "Equipment name"; Accessible.ignored: true }
        SharedTextField {
            id: nameField
            Layout.fillWidth: true
            controlStyle: fieldStyle
            text: form.draftName
            placeholder: "Equipment name"
            accessibleName: "Equipment name"
            onTextEdited: form.draftName = text
            onKeyboardShowRequested: Keyboard.show()
            onKeyboardHideRequested: Keyboard.hide()
            KeyNavigation.tab: grinderField.textField
            KeyNavigation.backtab: cancelButton
        }
        SharedSuggestionField {
            id: grinderField
            Layout.fillWidth: true
            controlStyle: fieldStyle
            label: "Grinder"
            accessibleName: "Grinder"
            text: form.draftGrinder
            suggestions: ["Varia VS6", "Niche Zero", "Niche Duo", "Graph"]
            descriptions: ({
                "Varia VS6": "58 mm flat burrs",
                "Niche Zero": "63 mm conical burrs",
                "Niche Duo": "83 mm flat burrs"
            })
            textRightPadding: fieldStyle.scaled(12)
            onTextEdited: function(value) { form.draftGrinder = value }
            onSuggestionSelected: function(value) { form.lastAction = "Selected: " + value }
            onKeyboardShowRequested: Keyboard.show()
            onKeyboardHideRequested: Keyboard.hide()
            onBrowseRequested: browseDialog.open()
            onCloseBrowseRequested: browseDialog.close()
            nextFocusItem: clearButton
            previousFocusItem: nameField
        }
        Flow {
            Layout.fillWidth: true
            spacing: 8
            Button {
                id: clearButton
                text: "Clear grinder"
                Accessible.name: text
                activeFocusOnTab: true
                onClicked: grinderField.clearText()
                KeyNavigation.tab: browseButton
                KeyNavigation.backtab: grinderField.textField
            }
            Button {
                id: browseButton
                text: "Browse grinders"
                Accessible.name: text
                activeFocusOnTab: true
                onClicked: grinderField.openSuggestionsDialog()
                KeyNavigation.tab: applyButton
                KeyNavigation.backtab: clearButton
            }
            Button {
                id: applyButton
                text: "Apply to preview"
                Accessible.name: text
                activeFocusOnTab: true
                onClicked: {
                    Keyboard.commit()
                    form.previewName = nameField.text
                    form.previewGrinder = grinderField.textField.text
                    form.lastAction = "Applied to local preview; nothing saved on the host."
                }
                KeyNavigation.tab: cancelButton
                KeyNavigation.backtab: browseButton
            }
            Button {
                id: cancelButton
                text: "Cancel edits"
                Accessible.name: text
                activeFocusOnTab: true
                onClicked: {
                    Keyboard.commit()
                    grinderField.closeSuggestions()
                    form.draftName = form.previewName
                    form.draftGrinder = form.previewGrinder
                    form.lastAction = "Edits cancelled; preview retained."
                }
                KeyNavigation.tab: nameField
                KeyNavigation.backtab: applyButton
            }
        }
        Label {
            text: form.lastAction
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: summary.implicitHeight + 24
            color: "#20262c"
            radius: 8
            EquipmentSummaryRenderer {
                id: summary
                x: 12
                y: 12
                width: parent.width - 24
                function escaped(value: string): string {
                    return value.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
                }
                titleRichText: escaped(form.previewName)
                grinderBurrsRichText: escaped(form.previewGrinder)
                basketLineRichText: "Basket: 18 g"
                bodyFont: fieldStyle.font
                labelFont: fieldStyle.font
                captionFont: fieldStyle.font
                titlePixelSize: 18
                primaryTextColor: "#f1f2f3"
                secondaryTextColor: "#b9c2cc"
            }
        }

    }

    // The host owns browse-all orchestration. Native SuggestionField keeps its
    // existing SelectionDialog; this diagnostic host supplies a Qt dialog.
    Dialog {
        id: browseDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent ? parent.width - 32 : 340, 400)
        title: "Browse grinders"
        modal: true
        focus: true
        standardButtons: Dialog.Cancel
        property var snapshot: []
        onAboutToShow: snapshot = grinderField.suggestions.slice()
        onClosed: browseButton.forceActiveFocus()
        contentItem: ListView {
            implicitHeight: 200
            model: browseDialog.snapshot
            clip: true
            delegate: ItemDelegate {
                id: browseDelegate
                required property string modelData
                width: ListView.view.width
                text: modelData
                Accessible.role: Accessible.Button
                Accessible.name: text
                Accessible.focusable: true
                Accessible.onPressAction: grinderField.selectSuggestion(browseDelegate.modelData)
                activeFocusOnTab: true
                onClicked: grinderField.selectSuggestion(modelData)
            }
        }
    }
}
