import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../../components"

Item {
    id: themesTab

    // Signal to request opening save theme dialog (handled by parent)
    signal openSaveThemeDialog()

    // Currently selected color for editing
    property string selectedColorName: "primaryColor"
    property color selectedColorValue: Theme.primaryColor

    // Color definitions with display names and categories
    property var colorDefinitions: [
        { category: "Core UI", colors: [
            { name: "backgroundColor", display: "Background" },
            { name: "surfaceColor", display: "Surface" },
            { name: "primaryColor", display: "Primary" },
            { name: "secondaryColor", display: "Secondary" },
            { name: "textColor", display: "Text" },
            { name: "textSecondaryColor", display: "Text Secondary" },
            { name: "accentColor", display: "Accent" },
            { name: "borderColor", display: "Border" }
        ]},
        { category: "Status", colors: [
            { name: "successColor", display: "Success" },
            { name: "warningColor", display: "Warning" },
            { name: "errorColor", display: "Error" }
        ]},
        { category: "Chart", colors: [
            { name: "pressureColor", display: "Pressure" },
            { name: "pressureGoalColor", display: "Pressure Goal" },
            { name: "flowColor", display: "Flow" },
            { name: "flowGoalColor", display: "Flow Goal" },
            { name: "temperatureColor", display: "Temperature" },
            { name: "temperatureGoalColor", display: "Temp Goal" },
            { name: "weightColor", display: "Weight" }
        ]}
    ]

    function getColorValue(colorName) {
        return Theme[colorName] || "#ffffff"
    }

    function selectColor(colorName) {
        selectedColorName = colorName
        selectedColorValue = getColorValue(colorName)
        colorEditor.setColor(selectedColorValue)
    }

    function applyColorChange(newColor) {
        Settings.setThemeColor(selectedColorName, newColor.toString())
        selectedColorValue = newColor
    }

    RowLayout {
        anchors.fill: parent
        spacing: Theme.spacingMedium

        // Left panel - Color list
        Rectangle {
            Layout.preferredWidth: parent.width * 0.4
            Layout.fillHeight: true
            color: Theme.surfaceColor
            radius: Theme.cardRadius

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacingMedium
                spacing: Theme.spacingSmall

                Text {
                    text: TranslationManager.translate("settings.themes.theme", "Theme:") + " " + Settings.activeThemeName
                    color: Theme.textColor
                    font: Theme.subtitleFont
                }

                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    ScrollBar.vertical.policy: ScrollBar.AsNeeded
                    contentWidth: availableWidth
                    clip: true

                    ColumnLayout {
                        width: parent.width
                        spacing: Theme.spacingSmall

                        Repeater {
                            model: themesTab.colorDefinitions

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: Theme.scaled(4)

                                // Category header
                                Text {
                                    text: modelData.category
                                    color: Theme.textSecondaryColor
                                    font: Theme.labelFont
                                    topPadding: index > 0 ? Theme.spacingSmall : 0
                                }

                                // Color swatches in this category
                                Repeater {
                                    id: colorRepeater
                                    property var colorList: modelData.colors
                                    model: colorList.length

                                    ColorSwatch {
                                        property var colorData: colorRepeater.colorList[index]
                                        Layout.fillWidth: true
                                        colorName: colorData.name
                                        displayName: colorData.display
                                        colorValue: themesTab.getColorValue(colorData.name)
                                        selected: themesTab.selectedColorName === colorData.name
                                        onClicked: themesTab.selectColor(colorData.name)
                                    }
                                }
                            }
                        }
                    }
                }

                // Bottom buttons
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSmall

                    Button {
                        text: TranslationManager.translate("settings.themes.reset", "Reset")
                        onClicked: Settings.resetThemeToDefault()
                        background: Rectangle {
                            color: Theme.errorColor
                            radius: Theme.buttonRadius
                            opacity: parent.pressed ? 0.8 : 1.0
                        }
                        contentItem: Text {
                            text: parent.text
                            color: "white"
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }

                    Item { Layout.fillWidth: true }
                }
            }
        }

        // Right panel - Color editor
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.surfaceColor
            radius: Theme.cardRadius

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacingMedium
                spacing: Theme.spacingSmall

                Text {
                    text: TranslationManager.translate("settings.themes.edit", "Edit:") + " " + themesTab.selectedColorName
                    color: Theme.textColor
                    font: Theme.subtitleFont
                }

                ColorEditor {
                    id: colorEditor
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(140)

                    Component.onCompleted: setColor(themesTab.selectedColorValue)

                    onColorChanged: {
                        themesTab.applyColorChange(colorEditor.color)
                    }
                }

                // Preset themes in horizontal scroll
                ScrollView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.scaled(44)
                    ScrollBar.horizontal.policy: ScrollBar.AsNeeded
                    ScrollBar.vertical.policy: ScrollBar.AlwaysOff
                    contentHeight: availableHeight
                    clip: true

                    Row {
                        height: parent.height
                        spacing: Theme.spacingSmall

                        Repeater {
                            id: presetRepeater
                            model: Settings.getPresetThemes()

                            Rectangle {
                                height: Theme.scaled(36)
                                width: presetRow.width + (modelData.isBuiltIn ? 0 : deleteBtn.width + 4)
                                color: modelData.primaryColor
                                radius: Theme.buttonRadius
                                border.color: Settings.activeThemeName === modelData.name ? "white" : "transparent"
                                border.width: Theme.scaled(2)

                                Row {
                                    id: presetRow
                                    anchors.left: parent.left
                                    anchors.verticalCenter: parent.verticalCenter
                                    leftPadding: Theme.scaled(12)
                                    rightPadding: modelData.isBuiltIn ? 12 : 4

                                    Text {
                                        text: modelData.name
                                        color: "white"
                                        font: Theme.labelFont
                                        anchors.verticalCenter: parent.verticalCenter

                                        MouseArea {
                                            anchors.fill: parent
                                            anchors.margins: -8
                                            onClicked: Settings.applyPresetTheme(modelData.name)
                                        }
                                    }
                                }

                                // Delete button for user themes
                                Rectangle {
                                    id: deleteBtn
                                    visible: !modelData.isBuiltIn
                                    width: Theme.scaled(24)
                                    height: Theme.scaled(24)
                                    radius: Theme.scaled(12)
                                    color: deleteArea.pressed ? Qt.darker(parent.color, 1.3) : Qt.darker(parent.color, 1.15)
                                    anchors.right: parent.right
                                    anchors.rightMargin: Theme.scaled(6)
                                    anchors.verticalCenter: parent.verticalCenter

                                    Text {
                                        text: "x"
                                        color: "white"
                                        font.pixelSize: Theme.scaled(12)
                                        font.bold: true
                                        anchors.centerIn: parent
                                    }

                                    MouseArea {
                                        id: deleteArea
                                        anchors.fill: parent
                                        onClicked: {
                                            Settings.deleteUserTheme(modelData.name)
                                            presetRepeater.model = Settings.getPresetThemes()
                                        }
                                    }
                                }
                            }
                        }

                        // Save current theme button
                        Rectangle {
                            height: Theme.scaled(36)
                            width: saveText.width + 24
                            color: Theme.surfaceColor
                            radius: Theme.buttonRadius
                            border.color: Theme.borderColor
                            border.width: Theme.scaled(1)

                            Text {
                                id: saveText
                                text: "+ " + TranslationManager.translate("settings.themes.save", "Save")
                                color: Theme.textColor
                                font: Theme.labelFont
                                anchors.centerIn: parent
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: themesTab.openSaveThemeDialog()
                            }
                        }
                    }
                }

                // Random theme button
                Button {
                    Layout.fillWidth: true
                    property string buttonText: TranslationManager.translate("settings.themes.randomTheme", "Random Theme")
                    text: buttonText
                    onClicked: {
                        var randomHue = Math.random() * 360
                        var randomSat = 65 + Math.random() * 20  // 65-85%
                        var randomLight = 50 + Math.random() * 10  // 50-60%
                        var palette = Settings.generatePalette(randomHue, randomSat, randomLight)
                        Settings.customThemeColors = palette
                        Settings.setActiveThemeName("Custom")
                    }
                    background: Rectangle {
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0.0; color: "#ff6b6b" }
                            GradientStop { position: 0.25; color: "#ffd93d" }
                            GradientStop { position: 0.5; color: "#6bcb77" }
                            GradientStop { position: 0.75; color: "#4d96ff" }
                            GradientStop { position: 1.0; color: "#9b59b6" }
                        }
                        radius: Theme.buttonRadius
                        opacity: parent.pressed ? 0.8 : 1.0
                    }
                    contentItem: Text {
                        text: parent.buttonText
                        color: "white"
                        font.pixelSize: Theme.bodyFont.pixelSize
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }
    }

    // Function to refresh preset themes (called by parent after saving)
    function refreshPresets() {
        presetRepeater.model = Settings.getPresetThemes()
    }
}
