import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza
import "../../components"

KeyboardAwareContainer {
    id: themesTab
    textFields: [hexField]

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
            { name: "primaryContrastColor", display: "Primary Contrast" },
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
            { name: "weightColor", display: "Weight" },
            { name: "weightFlowColor", display: "Weight Flow" },
            { name: "resistanceColor", display: "Resistance" }
        ]}
    ]

    // Bumped when editing palette changes, to force swatch re-evaluation
    property int _paletteVersion: 0

    function getColorValue(colorName) {
        var _v = _paletteVersion  // reactive dependency
        var editColors = Settings.editingPaletteColors()
        return editColors[colorName] || Theme[colorName] || "#ffffff"
    }

    function selectColor(colorName) {
        selectedColorName = colorName
        selectedColorValue = getColorValue(colorName)
        colorEditor.setColor(selectedColorValue)
        hexField.text = selectedColorValue.toString().substring(0, 7)
    }

    // Guard to prevent hex / colorEditor feedback loop
    property bool _updatingFromHex: false

    function applyColorChange(newColor) {
        Settings.setEditingPaletteColor(selectedColorName, newColor.toString())
        selectedColorValue = newColor
    }

    // Refresh all swatches and selected color when editing palette changes
    Connections {
        target: Settings
        function onEditingPaletteChanged() {
            themesTab._paletteVersion++
            themesTab.selectColor(themesTab.selectedColorName)
        }
        function onCustomThemeColorsChanged() {
            themesTab._paletteVersion++
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Theme mode selector + palette toggle
        RowLayout {
            Layout.fillWidth: true
            Layout.bottomMargin: Theme.spacingSmall
            spacing: Theme.spacingMedium

            // Mode selector: Dark / Light / Follow System
            Rectangle {
                implicitWidth: modeRow.implicitWidth
                implicitHeight: Theme.scaled(36)
                radius: Theme.buttonRadius
                color: "transparent"
                border.color: Theme.borderColor
                border.width: 1
                clip: true

                Row {
                    id: modeRow
                    anchors.fill: parent

                    Repeater {
                        model: [
                            { value: "dark", label: TranslationManager.translate("settings.themes.dark", "Dark") },
                            { value: "light", label: TranslationManager.translate("settings.themes.light", "Light") },
                            { value: "system", label: TranslationManager.translate("settings.themes.system", "System") }
                        ]

                        Rectangle {
                            width: modeLabel.implicitWidth + Theme.scaled(24)
                            height: parent.height
                            color: Settings.themeMode === modelData.value ? Theme.primaryColor : Theme.surfaceColor
                            // Only left border as separator between segments
                            Rectangle {
                                visible: index > 0
                                anchors.left: parent.left
                                width: 1
                                height: parent.height
                                color: Theme.borderColor
                            }

                            Text {
                                id: modeLabel
                                text: modelData.label
                                color: Settings.themeMode === modelData.value ? Theme.primaryContrastColor : Theme.textColor
                                font: Theme.labelFont
                                anchors.centerIn: parent
                                Accessible.ignored: true
                            }

                            Accessible.role: Accessible.Button
                            Accessible.name: modelData.label
                            Accessible.focusable: true
                            Accessible.onPressAction: modeArea.clicked(null)

                            MouseArea {
                                id: modeArea
                                anchors.fill: parent
                                onClicked: Settings.themeMode = modelData.value
                            }
                        }
                    }
                }
            }

            Item { Layout.fillWidth: true }

            // Palette toggle: which palette to edit
            Rectangle {
                implicitWidth: palRow.implicitWidth
                implicitHeight: Theme.scaled(36)
                radius: Theme.buttonRadius
                color: "transparent"
                border.color: Theme.borderColor
                border.width: 1
                clip: true

                Row {
                    id: palRow
                    anchors.fill: parent

                    Repeater {
                        model: [
                            { value: "dark", label: TranslationManager.translate("settings.themes.editDark", "Dark Palette") },
                            { value: "light", label: TranslationManager.translate("settings.themes.editLight", "Light Palette") }
                        ]

                        Rectangle {
                            width: palLabel.implicitWidth + Theme.scaled(24)
                            height: parent.height
                            color: Settings.editingPalette === modelData.value ? Theme.primaryColor : Theme.surfaceColor

                            Rectangle {
                                visible: index > 0
                                anchors.left: parent.left
                                width: 1
                                height: parent.height
                                color: Theme.borderColor
                            }

                            Text {
                                id: palLabel
                                text: modelData.label
                                color: Settings.editingPalette === modelData.value ? Theme.primaryContrastColor : Theme.textColor
                                font: Theme.labelFont
                                anchors.centerIn: parent
                                Accessible.ignored: true
                            }

                            Accessible.role: Accessible.Button
                            Accessible.name: modelData.label
                            Accessible.focusable: true
                            Accessible.onPressAction: palArea.clicked(null)

                            MouseArea {
                                id: palArea
                                anchors.fill: parent
                                onClicked: Settings.editingPalette = modelData.value
                            }
                        }
                    }
                }
            }
        }

        // Web version banner
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: bannerText.height + Theme.scaled(16)
            color: Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.1)
            radius: Theme.cardRadius
            border.color: Theme.primaryColor
            border.width: 1

            Text {
                id: bannerText
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.margins: Theme.scaled(12)
                text: TranslationManager.translate("settings.themes.webBanner",
                    "For CRT shaders, live preview, and more — use the web version (enable web server, then visit /themes)")
                color: Theme.textColor
                font: Theme.captionFont
                wrapMode: Text.WordWrap
                lineHeight: 1.3
            }
        }

        // Main editor area
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: Theme.spacingSmall
            spacing: Theme.spacingMedium

            // Left panel - Color list
            Rectangle {
                Layout.fillWidth: true
                Layout.maximumWidth: Math.max(0, themesTab.width * 0.4)
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

                }
            }

            // Right panel - Color editor (flickable)
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.surfaceColor
                radius: Theme.cardRadius
                clip: true

                Flickable {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingMedium
                    contentHeight: rightColumn.height
                    flickableDirection: Flickable.VerticalFlick
                    boundsBehavior: Flickable.StopAtBounds

                    ColumnLayout {
                        id: rightColumn
                        width: parent.width
                        spacing: Theme.spacingSmall

                        Text {
                            text: TranslationManager.translate("settings.themes.edit", "Edit:") + " " + themesTab.selectedColorName
                            color: Theme.textColor
                            font: Theme.subtitleFont
                        }

                        // Hex color input row
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingSmall

                            Rectangle {
                                width: Theme.scaled(32)
                                height: Theme.scaled(32)
                                radius: Theme.scaled(6)
                                color: themesTab.selectedColorValue
                                border.color: Theme.borderColor
                                border.width: 1
                            }

                            StyledTextField {
                                id: hexField
                                Layout.preferredWidth: Theme.scaled(120)
                                text: themesTab.selectedColorValue.toString().substring(0, 7)
                                font.family: "monospace"
                                font.pixelSize: Theme.bodyFont.pixelSize
                                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText

                                onTextEdited: {
                                    var hex = text.trim()
                                    if (/^#[0-9a-fA-F]{6}$/.test(hex)) {
                                        themesTab._updatingFromHex = true
                                        colorEditor.setColor(hex)
                                        themesTab.applyColorChange(colorEditor.color)
                                        themesTab._updatingFromHex = false
                                    }
                                }
                            }
                        }

                        ColorEditor {
                            id: colorEditor
                            Layout.fillWidth: true
                            Layout.preferredHeight: Theme.scaled(140)

                            // Guard to prevent saving during initialization
                            property bool initialized: false

                            Component.onCompleted: {
                                setColor(themesTab.selectedColorValue)
                                Qt.callLater(function() { initialized = true })
                            }

                            onColorChanged: {
                                if (initialized) {
                                    themesTab.applyColorChange(colorEditor.color)
                                }
                                if (!themesTab._updatingFromHex) {
                                    hexField.text = colorEditor.color.toString().substring(0, 7)
                                }
                            }
                        }

                        // Preset themes - wrapping flow
                        Flow {
                            Layout.fillWidth: true
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
                                    border.width: 2

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

                                        Accessible.role: Accessible.Button
                                        Accessible.name: TranslationManager.translate("settings.themes.delete", "Delete theme") + " " + modelData.name
                                        Accessible.focusable: true
                                        Accessible.onPressAction: deleteArea.clicked(null)

                                        Text {
                                            text: "x"
                                            color: "white"
                                            font.pixelSize: Theme.scaled(12)
                                            font.bold: true
                                            anchors.centerIn: parent
                                            Accessible.ignored: true
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
                                border.width: 1

                                Accessible.role: Accessible.Button
                                Accessible.name: TranslationManager.translate("settings.themes.save", "Save") + " " + TranslationManager.translate("settings.themes.accessible.currenttheme", "current theme")
                                Accessible.focusable: true
                                Accessible.onPressAction: saveThemeArea.clicked(null)

                                Text {
                                    id: saveText
                                    text: "+ " + TranslationManager.translate("settings.themes.save", "Save")
                                    color: Theme.textColor
                                    font: Theme.labelFont
                                    anchors.centerIn: parent
                                    Accessible.ignored: true
                                }

                                MouseArea {
                                    id: saveThemeArea
                                    anchors.fill: parent
                                    onClicked: themesTab.openSaveThemeDialog()
                                }
                            }
                        }

                        // Random theme button
                        AccessibleButton {
                            id: randomThemeBtn
                            Layout.fillWidth: true
                            text: TranslationManager.translate("settings.themes.randomTheme", "Random Theme")
                            accessibleName: TranslationManager.translate("settings.themes.randomThemeAccessible", "Apply a random color theme")
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
                                opacity: randomThemeBtn.pressed ? 0.8 : 1.0
                            }
                            contentItem: Text {
                                text: randomThemeBtn.text
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
        }
    }

    // Function to refresh preset themes (called by parent after saving)
    function refreshPresets() {
        presetRepeater.model = Settings.getPresetThemes()
    }
}
