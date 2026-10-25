pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

DecenzaDialog {
    id: root
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - Theme.scaled(32) : Theme.scaled(680), Theme.scaled(680))
    height: Math.min(parent ? parent.height - Theme.scaled(80) : Theme.scaled(640), Theme.scaled(640))
    modal: true
    SettingsSearch.overlay: true
    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.color: Theme.borderColor
        border.width: 1
    }

    property bool busy: false
    property string selectedColorName: ""
    readonly property bool available: ThemeManagementService.ready && !busy
    readonly property var editor: ThemeManagementService.editorSettings
    readonly property bool editingFonts: editorTabs.currentIndex === 0
    property string resetOperation: ""
    property var resetRevision: 0
    signal settingRequested(string operation, var values, var expectedRevision)

    function send(operation, values) {
        if (available)
            settingRequested(operation, values, ThemeManagementService.revision)
    }

    function confirmReset(operation) {
        resetOperation = operation
        resetRevision = ThemeManagementService.revision
        resetConfirm.open()
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingMedium

        Tr {
            key: "settings.themes.advanced"
            fallback: "Advanced theme controls"
            font: Theme.subtitleFont
            color: Theme.textColor
        }

        TabBar {
            id: editorTabs
            Layout.fillWidth: true
            StyledTabButton {
                text: TranslationManager.translate("settings.themes.fontSizes", "Font sizes")
                accessibleName: text
            }
            StyledTabButton {
                text: TranslationManager.translate("settings.themes.screenEffects", "Screen effects")
                accessibleName: text
            }
        }

        RowLayout {
            visible: !root.editingFonts
            Layout.fillWidth: true
            Tr {
                key: "settings.themes.enableCrt"
                fallback: "CRT / Pip-Boy"
                font: Theme.bodyFont
                color: Theme.textColor
                Layout.fillWidth: true
            }
            StyledSwitch {
                accessibleName: TranslationManager.translate("settings.themes.enableCrtAccessible", "Enable CRT screen effect")
                checked: root.editor.shader === "crt"
                enabled: root.available
                onToggled: root.send("setShader", {value: checked ? "crt" : ""})
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            ColumnLayout {
                width: parent.width
                spacing: Theme.spacingSmall

                Repeater {
                    model: root.editingFonts ? ThemeManagementService.fontControls
                         : root.editor.shader === "crt" ? ThemeManagementService.shaderControls : []
                    delegate: GridLayout {
                        id: parameterRow
                        columns: width < Theme.scaled(480) ? 1 : 2
                        required property var modelData
                        readonly property string controlLabel: TranslationManager.translate(modelData.labelKey, modelData.label)
                        Layout.fillWidth: true
                        rowSpacing: Theme.spacingSmall
                        columnSpacing: Theme.spacingMedium

                        Text {
                            text: parameterRow.controlLabel
                            font: Theme.bodyFont
                            color: Theme.textColor
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                        ValueInput {
                            Layout.preferredWidth: parameterRow.columns === 1 ? -1 : Theme.scaled(230)
                            Layout.fillWidth: parameterRow.columns === 1
                            accessibleName: root.editingFonts
                                ? TranslationManager.translate("settings.themes.fontSizeAccessible", "%1 font size").arg(parameterRow.controlLabel)
                                : parameterRow.controlLabel
                            enabled: root.available
                            from: parameterRow.modelData.minimum
                            to: parameterRow.modelData.maximum
                            stepSize: parameterRow.modelData.step
                            decimals: stepSize < 0.01 ? 3 : stepSize < 0.1 ? 2 : stepSize < 1 ? 1 : 0
                            suffix: root.editingFonts ? " px" : ""
                            value: !ThemeManagementService.ready ? parameterRow.modelData.defaultValue
                                 : root.editingFonts ? root.editor.fontSizes[parameterRow.modelData.id]
                                 : root.editor.shaderParams[parameterRow.modelData.id] ?? parameterRow.modelData.defaultValue
                            onValueCommitted: function(nextValue) {
                                root.send(root.editingFonts ? "setFontSize" : "setShaderParam",
                                          {name: parameterRow.modelData.id, value: nextValue})
                            }
                        }
                    }
                }
            }
        }

        AccessibleButton {
            Layout.fillWidth: true
            text: root.editingFonts
                ? TranslationManager.translate("settings.themes.resetFonts", "Reset font sizes")
                : TranslationManager.translate("settings.themes.resetCrt", "Reset CRT parameters")
            accessibleName: text
            enabled: root.available && (root.editingFonts || root.editor.shader === "crt")
            onClicked: root.editingFonts ? root.confirmReset("resetFonts") : root.send("resetShader", {})
        }

        GridLayout {
            Layout.fillWidth: true
            columns: root.width < Theme.scaled(520) ? 1 : 3
            AccessibleButton {
                Layout.fillWidth: true
                text: TranslationManager.translate("settings.themes.flashColor", "Flash selected colour")
                accessibleName: text
                enabled: root.available && root.selectedColorName.length > 0
                onClicked: root.send("flashColor", {name: root.selectedColorName})
            }
            AccessibleButton {
                text: TranslationManager.translate("settings.themes.resetTheme", "Reset theme")
                accessibleName: TranslationManager.translate("settings.themes.resetThemeAccessible", "Reset theme colours and font sizes")
                enabled: root.available
                onClicked: root.confirmReset("resetTheme")
            }
            AccessibleButton {
                Layout.alignment: Qt.AlignRight
                text: TranslationManager.translate("common.close", "Close")
                accessibleName: text
                onClicked: root.close()
            }
        }
    }

    DecenzaDialog {
        id: resetConfirm
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(parent ? parent.width - Theme.scaled(32) : Theme.scaled(420), Theme.scaled(420))
        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.borderColor
            border.width: 1
        }
        contentItem: ColumnLayout {
            spacing: Theme.spacingMedium
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: root.resetOperation === "resetFonts"
                    ? TranslationManager.translate("settings.themes.confirmResetFonts", "Reset all font sizes? Theme colours will stay unchanged.")
                    : TranslationManager.translate("settings.themes.confirmResetTheme", "Reset theme colours and font sizes to their defaults?")
                font: Theme.bodyFont
                color: Theme.textColor
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                AccessibleButton {
                    text: TranslationManager.translate("common.button.cancel", "Cancel")
                    accessibleName: text
                    onClicked: resetConfirm.close()
                }
                AccessibleButton {
                    text: TranslationManager.translate("common.reset", "Reset")
                    accessibleName: text
                    enabled: root.available
                    onClicked: {
                        root.settingRequested(root.resetOperation, {}, root.resetRevision)
                        resetConfirm.close()
                    }
                }
            }
        }
    }
}
