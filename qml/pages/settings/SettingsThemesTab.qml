// The colour-category, colour-swatch and preset-theme Repeater delegates read this
// file's ids (`themesTab`, `colorRepeater`, `presetRepeater`); Bound makes them
// statically resolvable. Each declares every injected role it uses required in the
// same edit -- without that, Bound stops role injection and the whole colour list and
// preset row render blank at RUNTIME, silently.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

KeyboardAwareContainer {
    id: themesTab
    textFields: [hexField]

    // Signal to request opening save theme dialog (handled by parent)
    signal openSaveThemeDialog()
    signal themeOperationFailed(string message)

    property string pendingClipboardRequestId: ""
    property string pendingClipboardOperation: ""
    property string pendingClipboardColorName: ""
    property string pendingClipboardPalette: ""
    property int pendingClipboardRevision: -1
    property string clipboardMessage: ""
    property bool clipboardPasteFallback: false
    property bool clipboardCopySucceeded: false

    property string pendingThemeRequestId: ""
    property string pendingThemeOperation: ""
    readonly property bool themeOperationBusy: pendingThemeRequestId.length > 0
        || paletteWriteRequestId.length > 0 || hasPaletteDraft
        || pendingClipboardRequestId.length > 0

    function startClipboardOperation(operation) {
        if (pendingClipboardRequestId.length > 0)
            return false
        const requestId = LocalClipboard.newRequestId()
        pendingClipboardRequestId = requestId
        pendingClipboardOperation = operation
        clipboardMessage = ""
        if (operation === "readText") {
            pendingClipboardColorName = selectedColorName
            pendingClipboardPalette = editingPalette
            pendingClipboardRevision = ThemeManagementService.revision
        }
        if (operation === "writeText")
            clipboardCopySucceeded = false
        const accepted = operation === "writeText"
            ? LocalClipboard.writeText(requestId, hexField.text)
            : LocalClipboard.readText(requestId)
        if (!accepted) {
            pendingClipboardRequestId = ""
            pendingClipboardOperation = ""
            clipboardMessage = TranslationManager.translate("settings.themes.clipboardRequestFailed",
                                                             "Could not start clipboard access.")
            return false
        }
        return true
    }

    function clipboardFailureText(operation, reason) {
        if (reason === "unsupported")
            return TranslationManager.translate("settings.themes.clipboardUnavailable",
                                                "This browser does not allow clipboard access here.")
        if (reason === "NotAllowedError" || reason === "SecurityError")
            return TranslationManager.translate("settings.themes.clipboardDenied",
                                                "Clipboard access was denied by the browser.")
        if (reason === "tooLarge")
            return TranslationManager.translate("settings.themes.clipboardTooLarge",
                                                "Clipboard text is too large to read safely.")
        return operation === "readText"
            ? TranslationManager.translate("settings.themes.clipboardReadFailed", "Could not read the clipboard.")
            : TranslationManager.translate("settings.themes.clipboardWriteFailed", "Could not copy to the clipboard.")
    }

    function applyClipboardColor(value) {
        var hex = String(value || "").trim()
        if (/^[0-9a-fA-F]{6}$/.test(hex))
            hex = "#" + hex
        if (!/^#[0-9a-fA-F]{6}$/.test(hex)) {
            clipboardMessage = TranslationManager.translate("settings.themes.clipboardInvalidColor",
                                                             "Clipboard text is not a six-digit hex color.")
            return false
        }
        hexField.text = hex
        _selecting = true
        colorEditor.setColor(hex)
        _selecting = false
        applyColorChange(colorEditor.color)
        return true
    }

    Connections {
        target: LocalClipboard
        function onOperationCompleted(requestId, operation, success, text, reason) {
            if (requestId !== themesTab.pendingClipboardRequestId
                    || operation !== themesTab.pendingClipboardOperation)
                return
            themesTab.pendingClipboardRequestId = ""
            themesTab.pendingClipboardOperation = ""
            if (!success) {
                themesTab.clipboardMessage = themesTab.clipboardFailureText(operation, reason)
                if (operation === "readText")
                    themesTab.clipboardPasteFallback = true
                return
            }
            themesTab.clipboardMessage = ""
            if (operation === "readText") {
                themesTab.clipboardPasteFallback = false
                if (themesTab.selectedColorName !== themesTab.pendingClipboardColorName
                        || themesTab.editingPalette !== themesTab.pendingClipboardPalette
                        || ThemeManagementService.revision !== themesTab.pendingClipboardRevision) {
                    themesTab.clipboardMessage = TranslationManager.translate(
                        "settings.themes.clipboardTargetChanged",
                        "The selected color changed before clipboard access finished. Try again.")
                    return
                }
                themesTab.applyClipboardColor(text)
            } else {
                themesTab.clipboardCopySucceeded = true
                copyResetTimer.restart()
            }
        }
    }

    function themeErrorMessage(reason) {
        if (reason === "conflict")
            return TranslationManager.translate("settings.themes.conflict", "Themes changed while this action was open. Try again.")
        if (reason === "invalidOperationOrName")
            return TranslationManager.translate("settings.themes.invalidOperation", "The theme name is invalid or the theme can no longer be changed.")
        if (reason === "disconnected" || reason === "unavailable")
            return TranslationManager.translate("common.settingsNotReady", "Settings are not connected yet.")
        if (reason === "invalidColor")
            return TranslationManager.translate("settings.themes.invalidColor", "The selected color could not be applied.")
        return TranslationManager.translate("common.error", "Operation failed")
    }

    function executeThemeOperation(operation, name, expectedRevision) {
        if (!ThemeManagementService.ready) {
            themeOperationFailed(TranslationManager.translate("common.settingsNotReady", "Settings are not connected yet."))
            return ""
        }
        if (themeOperationBusy) {
            themeOperationFailed(hasPaletteDraft
                ? TranslationManager.translate("settings.themes.pendingColorDraft", "Retry or discard the unfinished color change first.")
                : TranslationManager.translate("settings.themes.operationPending", "Wait for the current theme operation to finish."))
            return ""
        }
        const requestId = ThemeManagementService.newRequestId()
        pendingThemeRequestId = requestId
        pendingThemeOperation = operation
        const returnedId = ThemeManagementService.execute(operation, name, expectedRevision, requestId)
        if (returnedId !== requestId) {
            pendingThemeRequestId = ""
            pendingThemeOperation = ""
            themeOperationFailed(TranslationManager.translate("common.error", "Operation failed"))
            return ""
        }
        return requestId
    }

    Connections {
        target: ThemeManagementService
        function onOperationCompleted(requestId, operation, success, revision, result, reason) {
            if (requestId === themesTab.paletteWriteRequestId && operation === "setPaletteColor") {
                themesTab.paletteWriteRequestId = ""
                if (success) {
                    themesTab.replaceDraftsFromService(result.palettes)
                    themesTab.pendingPaletteMode = ""
                    themesTab.pendingPaletteColorName = ""
                    themesTab.pendingPaletteColorValue = ""
                } else {
                    themesTab.mergeDraftWithService(themesTab.pendingPaletteMode,
                                                    themesTab.pendingPaletteColorName,
                                                    themesTab.pendingPaletteColorValue,
                                                    result.palettes)
                    themesTab.themeOperationFailed(themesTab.themeErrorMessage(reason))
                }
                themesTab.selectColor(themesTab.selectedColorName)
                return
            }
            if (requestId === themesTab.pendingThemeRequestId
                    && operation === themesTab.pendingThemeOperation) {
                themesTab.pendingThemeRequestId = ""
                themesTab.pendingThemeOperation = ""
                if (success) {
                    if (operation === "random")
                        themesTab.replaceDraftsFromService(result.palettes)
                } else {
                    themesTab.themeOperationFailed(themesTab.themeErrorMessage(reason))
                }
            }
        }
    }

    // Currently selected color for editing
    property string selectedColorName: "primaryColor"
    property color selectedColorValue: Theme.primaryColor
    property string editingPalette: ThemeManagementService.darkMode ? "dark" : "light"
    property var paletteDrafts: ({ dark: {}, light: {} })
    property string paletteWriteRequestId: ""
    property string pendingPaletteMode: ""
    property string pendingPaletteColorName: ""
    property string pendingPaletteColorValue: ""
    property bool hasPaletteDraft: false
    property bool lastServiceDarkMode: ThemeManagementService.darkMode

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
            { name: "borderColor", display: "Border" },
            { name: "iconColor", display: "Icon" },
            { name: "bottomBarColor", display: "Bottom Bar" },
            { name: "actionButtonContentColor", display: "Action Button Content" }
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
            { name: "resistanceColor", display: "Resistance" },
            { name: "waterLevelColor", display: "Water Level" }
        ].concat(BelkaPortal.owned ? [
            { name: "portalEcColor", display: "PORTAL EC" },
            { name: "portalTemperatureColor", display: TranslationManager.translate("portal.outletTemperature", "PORTAL temperature") }
        ] : [])}
    ]

    // Bumped when editing palette changes, to force swatch re-evaluation
    property int _paletteVersion: 0

    function copyServicePalettes() {
        const remote = ThemeManagementService.palettes
        return { dark: Object.assign({}, remote.dark || {}),
                 light: Object.assign({}, remote.light || {}) }
    }

    function colorDraftValue(colorName) {
        const palette = paletteDrafts[editingPalette] || {}
        return palette[colorName] || ThemeManagementService.activeColors[colorName] || Theme[colorName] || "#ffffff"
    }

    function mergeDraftWithService(mode, colorName, colorValue, palettes) {
        const source = palettes || ThemeManagementService.palettes
        const drafts = Object.assign({}, paletteDrafts)
        const palette = Object.assign({}, paletteDrafts[mode] || {}, source[mode] || {})
        palette[colorName] = colorValue
        drafts[mode] = palette
        paletteDrafts = drafts
        hasPaletteDraft = true
        _paletteVersion++
    }

    function submitPaletteColor(mode, colorName, colorValue) {
        if (!ThemeManagementService.ready || paletteWriteRequestId.length > 0) {
            themeOperationFailed(TranslationManager.translate("common.settingsNotReady", "Settings are not connected yet."))
            return false
        }
        mergeDraftWithService(mode, colorName, colorValue)
        const requestId = ThemeManagementService.newRequestId()
        paletteWriteRequestId = requestId
        pendingPaletteMode = mode
        pendingPaletteColorName = colorName
        pendingPaletteColorValue = colorValue
        const returnedId = ThemeManagementService.setPaletteColor(mode, colorName, colorValue,
                                                                   ThemeManagementService.revision,
                                                                   requestId)
        if (returnedId !== requestId) {
            paletteWriteRequestId = ""
            themeOperationFailed(TranslationManager.translate("common.error", "Operation failed"))
            return false
        }
        return true
    }

    function replaceDraftsFromService(palettes) {
        const remote = palettes || ThemeManagementService.palettes
        paletteDrafts = { dark: Object.assign({}, remote.dark || {}),
                          light: Object.assign({}, remote.light || {}) }
        hasPaletteDraft = false
        if (lastServiceDarkMode !== ThemeManagementService.darkMode) {
            lastServiceDarkMode = ThemeManagementService.darkMode
            editingPalette = ThemeManagementService.darkMode ? "dark" : "light"
        }
        _paletteVersion++
        selectColor(selectedColorName)
    }

    function discardPaletteDraft() {
        if (paletteWriteRequestId.length > 0)
            return
        replaceDraftsFromService(ThemeManagementService.palettes)
        pendingPaletteMode = ""
        pendingPaletteColorName = ""
        pendingPaletteColorValue = ""
    }

    function getColorValue(colorName) {
        var _v = _paletteVersion  // reactive dependency
        return colorDraftValue(colorName)
    }

    // Guard to prevent intermediate color changes during selection
    property bool _selecting: false

    function selectColor(colorName) {
        _selecting = true
        selectedColorName = colorName
        selectedColorValue = getColorValue(colorName)
        colorEditor.setColor(selectedColorValue)
        hexField.text = Theme.colorToHex(selectedColorValue)
        _selecting = false
    }

    // Guard to prevent hex / colorEditor feedback loop
    property bool _updatingFromHex: false

    function applyColorChange(newColor) {
        const colorValue = Theme.colorToHex(newColor)
        submitPaletteColor(editingPalette, selectedColorName, colorValue)
    }

    function retryPaletteDraft() {
        if (hasPaletteDraft && pendingPaletteColorName.length > 0)
            submitPaletteColor(pendingPaletteMode, pendingPaletteColorName, pendingPaletteColorValue)
    }

    Component.onCompleted: {
        paletteDrafts = copyServicePalettes()
        if (ThemeManagementService.ready)
            selectColor(selectedColorName)
    }

    // Palette selection and unfinished color edits remain local to this tab instance.
    Connections {
        target: ThemeManagementService
        function onStateChanged() {
            if (!themesTab.hasPaletteDraft && themesTab.paletteWriteRequestId.length === 0)
            {
                themesTab.paletteDrafts = themesTab.copyServicePalettes()
                themesTab.selectColor(themesTab.selectedColorName)
            }
            if (themesTab.lastServiceDarkMode !== ThemeManagementService.darkMode
                    && !themesTab.hasPaletteDraft && themesTab.paletteWriteRequestId.length === 0) {
                themesTab.lastServiceDarkMode = ThemeManagementService.darkMode
                themesTab.editingPalette = ThemeManagementService.darkMode ? "dark" : "light"
                themesTab.selectColor(themesTab.selectedColorName)
            }
            themesTab._paletteVersion++
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

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
                // Route is /theme (singular) — see shotserver.cpp. The old /themes text
                // sent everyone who followed it to a 404.
                text: TranslationManager.translate("settings.themes.webBanner",
                    "For font sizes, CRT shaders, live preview, and more — use the web version (enable web server, then visit /theme)")
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
            SettingsCard {
                searchId: "themeColors"
                title: TranslationManager.translate("settings.search.themeColorsTitle", "Theme Colors")
                description: TranslationManager.translate("settings.search.themeColorsDesc", "Customize app color palette")
                keywords: ["color", "theme", "palette", "customize", "dark", "light"]
                showHeader: false
                fillContent: true
                contentMargins: Theme.spacingMedium
                spacing: Theme.spacingSmall
                Layout.maximumWidth: Math.max(0, themesTab.width * 0.4)

                Text {
                    text: TranslationManager.translate("settings.themes.theme", "Theme:") + " " + ThemeManagementService.activeThemeName
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
                            SettingsSearch.title: TranslationManager.translate("settings.search.themeColorsTitle", "Theme Colors")
                            model: themesTab.colorDefinitions

                            ColumnLayout {
                                id: categorySection
                                required property var modelData
                                required property int index

                                Layout.fillWidth: true
                                spacing: Theme.scaled(4)

                                // Category header
                                Text {
                                    text: categorySection.modelData.category
                                    color: Theme.textSecondaryColor
                                    font: Theme.labelFont
                                    topPadding: categorySection.index > 0 ? Theme.spacingSmall : 0
                                }

                                // Color swatches in this category
                                Repeater {
                                    id: colorRepeater
                                    property var colorList: categorySection.modelData.colors
                                    model: colorList.length

                                    ColorSwatch {
                                        required property int index

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

            // Right panel - Color editor (flickable)
            SettingsCard {
                searchId: "saveTheme"
                title: TranslationManager.translate("settings.search.saveThemeTitle", "Save Theme")
                description: TranslationManager.translate("settings.search.saveThemeDesc", "Save current color scheme as named theme")
                keywords: ["save", "theme", "preset", "custom"]
                // Its header ("Edit: <colour>") scrolls with the editor, inside the Flickable.
                showHeader: false
                fillContent: true
                contentMargins: Theme.spacingMedium
                clip: true

                Flickable {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
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
                                Layout.preferredWidth: Theme.scaled(32)
                                Layout.preferredHeight: Theme.scaled(32)
                                radius: Theme.scaled(6)
                                color: themesTab.selectedColorValue
                                border.color: Theme.borderColor
                                border.width: 1
                            }

                            StyledTextField {
                                id: hexField
                                Layout.preferredWidth: Theme.scaled(120)
                                enabled: !themesTab.themeOperationBusy
                                accessibleName: TranslationManager.translate("settings.themes.hexColor.accessible", "Hex color value")
                                text: Theme.colorToHex(themesTab.selectedColorValue)
                                font.family: Theme.monoFontFamily
                                font.pixelSize: Theme.bodyFont.pixelSize
                                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText

                                onTextEdited: {
                                    themesTab.clipboardPasteFallback = false
                                    themesTab.clipboardMessage = ""
                                    if (/^#?[0-9a-fA-F]{6}$/.test(text.trim()))
                                        themesTab.applyClipboardColor(text)
                                }
                            }

                            AccessibleButton {
                                id: copyBtn
                                text: themesTab.pendingClipboardOperation === "writeText"
                                    ? TranslationManager.translate("colorEditor.copying", "Copying…")
                                    : (themesTab.clipboardCopySucceeded
                                       ? TranslationManager.translate("colorEditor.copied", "Copied")
                                       : TranslationManager.translate("colorEditor.copy", "Copy"))
                                accessibleName: TranslationManager.translate("colorEditor.copyAccessible", "Copy color hex to clipboard")
                                implicitWidth: Theme.scaled(50)
                                enabled: !themesTab.themeOperationBusy
                                onClicked: themesTab.startClipboardOperation("writeText")
                                Timer {
                                    id: copyResetTimer
                                    interval: 1000
                                    onTriggered: themesTab.clipboardCopySucceeded = false
                                }
                            }

                            AccessibleButton {
                                SettingsSearch.title: TranslationManager.translate("settings.search.pasteColor", "Paste color")
                                text: themesTab.pendingClipboardOperation === "readText"
                                    ? TranslationManager.translate("colorEditor.readingClipboard", "Reading…")
                                    : (themesTab.clipboardPasteFallback
                                       ? TranslationManager.translate("colorEditor.pasteKeyboardHint", "Paste with keyboard")
                                       : TranslationManager.translate("colorEditor.paste", "Paste"))
                                accessibleName: themesTab.clipboardPasteFallback
                                    ? TranslationManager.translate("colorEditor.pasteKeyboardHint", "Paste with keyboard")
                                    : TranslationManager.translate("colorEditor.pasteAccessible", "Paste color hex from clipboard")
                                implicitWidth: Theme.scaled(themesTab.clipboardPasteFallback ? 112 : 50)
                                enabled: !themesTab.themeOperationBusy
                                onClicked: {
                                    if (themesTab.clipboardPasteFallback) {
                                        hexField.forceActiveFocus()
                                        hexField.selectAll()
                                    } else {
                                        themesTab.startClipboardOperation("readText")
                                    }
                                }
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: themesTab.clipboardMessage.length > 0
                            text: themesTab.clipboardMessage
                            color: Theme.errorColor
                            wrapMode: Text.Wrap
                            Accessible.name: text
                        }

                        ColorEditor {
                            id: colorEditor
                            enabled: !themesTab.themeOperationBusy
                            SettingsSearch.title: TranslationManager.translate("settings.search.colorEditor", "Color picker")
                            Layout.fillWidth: true
                            Layout.preferredHeight: Theme.scaled(140)

                            // Guard to prevent saving during initialization
                            property bool initialized: false

                            Component.onCompleted: {
                                setColor(themesTab.selectedColorValue)
                                Qt.callLater(function() { initialized = true })
                            }

                            onColorChanged: {
                                if (initialized && !themesTab._selecting) {
                                    themesTab.applyColorChange(colorEditor.color)
                                }
                                if (!themesTab._updatingFromHex && !themesTab._selecting) {
                                    hexField.text = Theme.colorToHex(colorEditor.color)
                                }
                            }
                        }

                        AccessibleButton {
                            visible: themesTab.hasPaletteDraft && themesTab.paletteWriteRequestId.length === 0
                            enabled: ThemeManagementService.ready
                            Layout.fillWidth: true
                            text: TranslationManager.translate("settings.themes.retryColor", "Retry color change")
                            accessibleName: TranslationManager.translate("settings.themes.retryColor", "Retry color change")
                            onClicked: themesTab.retryPaletteDraft()
                        }

                        AccessibleButton {
                            visible: themesTab.hasPaletteDraft && themesTab.paletteWriteRequestId.length === 0
                            enabled: ThemeManagementService.ready
                            Layout.fillWidth: true
                            text: TranslationManager.translate("settings.themes.discardColor", "Discard unfinished color change")
                            accessibleName: TranslationManager.translate("settings.themes.discardColor", "Discard unfinished color change")
                            onClicked: themesTab.discardPaletteDraft()
                        }

                        // Preset themes - wrapping flow
                        Flow {
                            Layout.fillWidth: true
                            spacing: Theme.spacingSmall

                            Repeater {
                                id: presetRepeater
                                SettingsSearch.title: TranslationManager.translate("settings.search.presetThemes", "Preset themes")
                                model: ThemeManagementService.presets

                                Rectangle {
                                    id: presetChip
                                    required property var modelData

                                    height: Theme.scaled(36)
                                    width: presetRow.width + (modelData.isBuiltIn ? 0 : deleteBtn.width + 4)
                                    color: modelData.primaryColor
                                    radius: Theme.buttonRadius
                                    border.color: ThemeManagementService.activeThemeName === modelData.name ? Theme.primaryContrastColor : "transparent"
                                    border.width: 2

                                    Accessible.role: Accessible.Button
                                    Accessible.name: ThemeManagementService.activeThemeName === modelData.name
                                        ? modelData.name + ", " + TranslationManager.translate("accessibility.selected", "selected")
                                        : modelData.name
                                    Accessible.focusable: true
                                    Accessible.onPressAction: applyThemeArea.clicked(null)

                                    MouseArea {
                                        id: applyThemeArea
                                        anchors.left: parent.left
                                        anchors.top: parent.top
                                        anchors.bottom: parent.bottom
                                        anchors.right: deleteBtn.visible ? deleteBtn.left : parent.right
                                        enabled: ThemeManagementService.ready && !themesTab.themeOperationBusy
                                        onClicked: themesTab.executeThemeOperation("apply", presetChip.modelData.name,
                                                                                   ThemeManagementService.revision)
                                    }

                                    Row {
                                        id: presetRow
                                        anchors.left: parent.left
                                        anchors.verticalCenter: parent.verticalCenter
                                        leftPadding: Theme.scaled(12)
                                        rightPadding: presetChip.modelData.isBuiltIn ? 12 : 4

                                        Text {
                                            text: presetChip.modelData.name
                                            color: Theme.primaryContrastColor
                                            font: Theme.labelFont
                                            anchors.verticalCenter: parent.verticalCenter
                                            Accessible.ignored: true
                                        }
                                    }

                                    // Delete button for user themes
                                    Rectangle {
                                        id: deleteBtn
                                        visible: !presetChip.modelData.isBuiltIn
                                        enabled: ThemeManagementService.ready && !themesTab.themeOperationBusy
                                        width: Theme.scaled(24)
                                        height: Theme.scaled(24)
                                        radius: Theme.scaled(12)
                                        color: deleteArea.pressed ? Qt.darker(parent.color, 1.3) : Qt.darker(parent.color, 1.15)
                                        anchors.right: parent.right
                                        anchors.rightMargin: Theme.scaled(6)
                                        anchors.verticalCenter: parent.verticalCenter

                                        Accessible.role: Accessible.Button
                                        Accessible.name: TranslationManager.translate("settings.themes.delete", "Delete theme") + " " + presetChip.modelData.name
                                        Accessible.focusable: true
                                        Accessible.onPressAction: deleteArea.clicked(null)

                                        Text {
                                            text: "x"
                                            color: Theme.primaryContrastColor
                                            font.pixelSize: Theme.scaled(12)
                                            font.bold: true
                                            anchors.centerIn: parent
                                            Accessible.ignored: true
                                        }

                                        MouseArea {
                                            id: deleteArea
                                            anchors.fill: parent
                                            onClicked: {
                                                themesTab.executeThemeOperation("delete", presetChip.modelData.name,
                                                                                ThemeManagementService.revision)
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

                                Text {
                                    id: saveText
                                    text: "+ " + TranslationManager.translate("settings.themes.save", "Save")
                                    color: Theme.textColor
                                    font: Theme.labelFont
                                    anchors.centerIn: parent
                                    Accessible.ignored: true
                                }

                                AccessibleMouseArea {
                                    id: saveThemeArea
                                    anchors.fill: parent
                                    enabled: ThemeManagementService.ready && !themesTab.themeOperationBusy
                                    accessibleName: TranslationManager.translate("settings.themes.save", "Save") + " " + TranslationManager.translate("settings.themes.accessible.currenttheme", "current theme")
                                    SettingsSearch.title: TranslationManager.translate("settings.search.saveThemeTitle", "Save Theme")
                                    onAccessibleClicked: themesTab.openSaveThemeDialog()
                                }
                            }
                        }

                        // Random theme button
                        AccessibleButton {
                            id: randomThemeBtn
                            Layout.fillWidth: true
                            enabled: ThemeManagementService.ready && !themesTab.themeOperationBusy
                            text: TranslationManager.translate("settings.themes.randomTheme", "Random Theme")
                            accessibleName: TranslationManager.translate("settings.themes.randomThemeAccessible", "Apply a random color theme")
                            onClicked: themesTab.executeThemeOperation("random", "",
                                                                       ThemeManagementService.revision)
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
                                color: Theme.primaryContrastColor
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

}
