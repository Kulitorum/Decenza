pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

DecenzaDialog {
    id: root
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(Theme.scaled(460), parent ? parent.width * 0.95 : Theme.scaled(460))
    height: Math.min(form.implicitHeight + padding * 2, parent ? parent.height * 0.92 : Theme.scaled(650))
    modal: true
    focus: true
    padding: Theme.spacingLarge
    closePolicy: writer.pending ? Popup.NoAutoClose : Popup.CloseOnEscape
    browserUnloadDirty: writer.pending || JSON.stringify(root.snapshot()) !== root.initialSnapshot
    onOpened: nameField.forceActiveFocus()

    background: Rectangle {
        color: Theme.dialogBackgroundColor
        radius: Theme.cardRadius
        border.color: Theme.borderColor
        border.width: 1
    }

    property bool allowRatio: false
    property bool recipeEditing: false
    property bool allowDelete: false
    property real espressoTargetG: 0
    property int presetIndex: -1
    property double capturedRevision: 0
    property string capturedName: ""
    property var capturedPreset: ({})
    property string vesselName: ""
    property string amountMode: "fixed"
    property real waterRatio: 2.5
    property int fixedAmount: 100
    property string measureMode: "weight"
    property real temperatureC: 90
    property int flowRate: 40
    property string saveError: ""
    property string initialSnapshot: ""
    property var submittedSnapshot: ({})
    readonly property var preview: RecipeContext.waterAmountPreview(root.snapshot(), root.espressoTargetG)
    readonly property bool validName: vesselName.trim().length > 0 && !writer.nameTaken(vesselName, presetIndex)
    readonly property bool validAmount: amountMode === "ratio" ? preview.valid : fixedAmount >= 10
        && fixedAmount <= (measureMode === "volume" ? 255 : 500)

    signal saved(var vessel, int index)
    signal removed()

    function snapshot() {
        const value = {name: root.vesselName.trim(), hasWater: true, vesselName: root.vesselName.trim(),
            amountMode: root.amountMode, mode: root.amountMode === "ratio" ? "weight" : root.measureMode,
            flowRate: root.flowRate, temperature: root.temperatureC, temperatureC: root.temperatureC}
        if (root.amountMode === "ratio") value.waterToEspressoRatio = root.waterRatio
        else value.volume = root.fixedAmount
        return value
    }

    function openFor(vessel, index) {
        root.presetIndex = index
        root.capturedRevision = Number(RecipeContext.revision)
        root.capturedPreset = writer.presetAt(index)
        root.capturedName = String(root.capturedPreset.name || "")
        root.vesselName = String(vessel.name || vessel.vesselName || "")
        root.amountMode = root.allowRatio ? (vessel.amountMode || (index < 0 ? "ratio" : "fixed")) : "fixed"
        root.waterRatio = Number(vessel.waterToEspressoRatio ?? 2.5)
        const incomingPreview = RecipeContext.waterAmountPreview(vessel, root.espressoTargetG)
        root.fixedAmount = Number(vessel.volume ?? (incomingPreview.valid ? incomingPreview.amountG : 100))
        root.measureMode = String(vessel.mode || "weight")
        root.temperatureC = Number(vessel.temperatureC ?? vessel.temperature ?? RecipeContext.preferences.waterTemperature ?? 90)
        root.flowRate = Number(vessel.flowRate ?? 40)
        root.saveError = ""
        nameField.text = root.vesselName
        root.initialSnapshot = JSON.stringify(root.snapshot())
        root.open()
    }

    function saveVessel() {
        Keyboard.commit()
        root.vesselName = nameField.text
        if (!root.validName || !root.validAmount || writer.pending) return
        root.submittedSnapshot = root.snapshot()
        const fields = {name: root.vesselName.trim(), temperature: root.temperatureC, flowRate: root.flowRate,
            volume: root.amountMode === "ratio" ? Number(root.capturedPreset.volume ?? root.preview.amountG) : root.fixedAmount,
            mode: root.amountMode === "ratio" ? String(root.capturedPreset.mode || "weight") : root.measureMode}
        if (root.presetIndex >= 0) {
            fields.index = root.presetIndex
            fields.expectedName = root.capturedName
            writer.submit("update", fields, root.capturedRevision)
        } else {
            writer.add(fields.name, fields, root.capturedRevision)
        }
    }

    SharedPresetDraft {
        id: writer
        domain: "waterVessel"
        onCompleted: function(operation, success, reason) {
            if (!success) {
                root.saveError = TranslationManager.translate("water.vessel.saveFailed",
                    "Could not save this vessel. Settings may have changed; cancel and reopen to reload them.")
                return
            }
            if (operation === "remove") {
                root.removed()
                root.close()
                return
            }
            const index = operation === "add" ? writer.presets.length - 1 : root.presetIndex
            root.saved(root.submittedSnapshot, index)
            root.close()
        }
    }

    contentItem: KeyboardAwareContainer {
        textFields: [nameField]
        inOverlay: true
        targetFlickable: editorFlickable
        implicitHeight: form.implicitHeight
        Flickable {
            id: editorFlickable
            anchors.fill: parent
            contentHeight: form.implicitHeight
            clip: true
            ScrollBar.vertical: ScrollBar {}
            ColumnLayout {
                id: form
                enabled: !writer.pending
                width: parent.width
                spacing: Theme.spacingMedium
                Label {
                    Layout.fillWidth: true
                    text: root.presetIndex < 0 ? TranslationManager.translate("water.vessel.add", "Add vessel")
                        : TranslationManager.translate("water.vessel.edit", "Edit vessel")
                    color: Theme.textColor
                    font: Theme.subtitleFont
                }
                StyledTextField {
                    id: nameField
                    Layout.fillWidth: true
                    placeholder: TranslationManager.translate("hotwater.placeholder.vesselName", "Vessel name")
                    accessibleName: placeholder
                    onTextEdited: root.vesselName = text
                }
                Label {
                    visible: root.vesselName.trim().length > 0 && !root.validName
                    Layout.fillWidth: true
                    text: TranslationManager.translate("hotwater.vesselNameInUse", "That name is already used by another vessel — choose a different one.")
                    wrapMode: Text.WordWrap
                    color: Theme.warningColor
                    font: Theme.captionFont
                }
                RowLayout {
                    visible: root.allowRatio
                    AccessibleButton {
                        text: TranslationManager.translate("water.amount.fixed", "Fixed amount")
                        accessibleName: text
                        primary: root.amountMode === "fixed"
                        onClicked: {
                            if (root.amountMode === "ratio" && root.preview.valid) root.fixedAmount = root.preview.amountG
                            root.amountMode = "fixed"
                            root.measureMode = "weight"
                        }
                    }
                    AccessibleButton {
                        text: TranslationManager.translate("water.amount.ratio", "Ratio to espresso")
                        accessibleName: text
                        primary: root.amountMode === "ratio"
                        onClicked: root.amountMode = "ratio"
                    }
                }
                AccessibleButton {
                    visible: root.allowRatio && root.amountMode === "ratio"
                    text: Number(root.waterRatio).toFixed(1) + "×"
                    accessibleName: TranslationManager.translate("water.ratio.edit", "Edit added-water ratio")
                    onClicked: waterRatioPicker.open()
                }
                RowLayout {
                    visible: root.amountMode === "fixed"
                    AccessibleButton {
                        text: root.measureMode === "weight" ? TranslationManager.translate("hotwater.label.weight", "Weight")
                            : TranslationManager.translate("hotwater.label.volume", "Volume")
                        accessibleName: TranslationManager.translate("water.amount.unit", "Change water amount unit")
                        onClicked: {
                            root.measureMode = root.measureMode === "weight" ? "volume" : "weight"
                            if (root.measureMode === "volume") root.fixedAmount = Math.min(255, root.fixedAmount)
                        }
                    }
                    ValueInput {
                        Layout.fillWidth: true
                        value: root.fixedAmount
                        from: 10
                        to: root.measureMode === "volume" ? 255 : 500
                        stepSize: 10
                        fineStepSize: 1
                        suffix: root.measureMode === "volume" ? " ml" : " g"
                        accessibleName: TranslationManager.translate("water.amount.added", "Added water")
                        onValueModified: function(value) { root.fixedAmount = value }
                    }
                }
                Label {
                    visible: root.allowRatio && root.amountMode === "ratio"
                    Layout.fillWidth: true
                    text: root.preview.valid ? TranslationManager.translate("water.ratio.preview", "%1× espresso yield · %2 g added water")
                        .arg(Number(root.waterRatio).toFixed(1)).arg(root.preview.amountG)
                        : TranslationManager.translate("water.ratio.invalid", "Set an espresso target and a ratio giving 10–500 g of added water.")
                    color: root.preview.valid ? Theme.textSecondaryColor : Theme.warningColor
                    wrapMode: Text.WordWrap
                    font: Theme.bodyFont
                }
                RowLayout {
                    Label {
                        text: TranslationManager.translate("hotwater.label.temperature", "Temperature")
                        color: Theme.textColor
                        font: Theme.bodyFont
                    }
                    ValueInput {
                        Layout.fillWidth: true
                        value: Theme.cToDisplay(root.temperatureC)
                        from: Theme.cToDisplay(40)
                        to: Theme.cToDisplay(100)
                        stepSize: 1
                        suffix: Theme.tempUnitSuffix()
                        accessibleName: TranslationManager.translate("hotwater.label.temperature", "Temperature")
                        onValueModified: function(value) { root.temperatureC = Theme.displayToC(value) }
                    }
                }
                RowLayout {
                    Label {
                        text: TranslationManager.translate("hotwater.label.flowRate", "Flow Rate")
                        color: Theme.textColor
                        font: Theme.bodyFont
                    }
                    ValueInput {
                        Layout.fillWidth: true
                        value: root.flowRate / 10
                        from: 0.5
                        to: 10
                        stepSize: 0.1
                        suffix: " ml/s"
                        accessibleName: TranslationManager.translate("hotwater.label.flowRate", "Flow Rate")
                        onValueModified: function(value) { root.flowRate = Math.round(value * 10) }
                    }
                }
                Label {
                    visible: root.recipeEditing
                    Layout.fillWidth: true
                    text: TranslationManager.translate("water.vessel.recipeSaveHint", "Save the vessel here, then save the recipe to keep its drink settings.")
                    wrapMode: Text.WordWrap
                    color: Theme.textSecondaryColor
                    font: Theme.captionFont
                }
                Label {
                    visible: root.saveError.length > 0
                    Layout.fillWidth: true
                    text: root.saveError
                    wrapMode: Text.WordWrap
                    color: Theme.errorColor
                    font: Theme.bodyFont
                }
                RowLayout {
                    AccessibleButton {
                        visible: root.allowDelete && root.presetIndex >= 0
                        text: TranslationManager.translate("hotwater.button.delete", "Delete")
                        accessibleName: TranslationManager.translate("hotWater.deleteVesselPreset", "Delete this water vessel preset")
                        destructive: true
                        enabled: RecipeContext.ready && !writer.pending
                        onClicked: writer.remove(root.presetIndex, root.capturedName, root.capturedRevision)
                    }
                    AccessibleButton {
                        text: TranslationManager.translate("common.button.cancel", "Cancel")
                        accessibleName: text
                        enabled: !writer.pending
                        onClicked: root.close()
                    }
                    Item { Layout.fillWidth: true }
                    AccessibleButton {
                        text: TranslationManager.translate("common.button.save", "Save")
                        accessibleName: text
                        primary: true
                        enabled: RecipeContext.ready && !writer.pending && root.validName && root.validAmount
                        onClicked: root.saveVessel()
                    }
                }
            }
        }
    }
    WaterRatioDialog {
        id: waterRatioPicker
        compareRatio: root.waterRatio
        onRatioPicked: function(value) { root.waterRatio = value }
    }
}
