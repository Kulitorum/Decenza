import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import "../../components"

Page {
    id: stringBrowserPage
    objectName: "stringBrowserPage"
    background: Rectangle { color: Theme.backgroundColor }

    property bool isEditing: false
    property int editingIndex: -1
    property bool isEnglish: TranslationManager.currentLanguage === "en"

    Component.onCompleted: {
        root.currentPageTitle = isEnglish ? "String Customizer" : "Translation Browser"
        stringModel.refresh()
    }
    StackView.onActivated: {
        root.currentPageTitle = isEnglish ? "String Customizer" : "Translation Browser"
    }

    // Grouped string list model
    ListModel {
        id: stringModel

        property int filterMode: 0  // 0=All, 1=Missing/Uncustomized, 2=AI Generated, 3=Customized (English only)
        property string searchFilter: ""

        function refresh() {
            clear()
            var groups = TranslationManager.getGroupedStrings()
            var search = searchFilter.toLowerCase()

            for (var i = 0; i < groups.length; i++) {
                var group = groups[i]

                // Filter by mode
                if (filterMode === 1 && group.isTranslated) continue  // Missing/Uncustomized
                if (filterMode === 2 && !group.isAiGenerated) continue  // AI Generated
                if (filterMode === 3 && !group.isTranslated) continue  // Customized (English)

                // Filter by search
                if (search && !group.fallback.toLowerCase().includes(search) &&
                    !(group.translation && group.translation.toLowerCase().includes(search)) &&
                    !(group.aiTranslation && group.aiTranslation.toLowerCase().includes(search))) {
                    continue
                }

                append(group)
            }
        }

        onFilterModeChanged: refresh()
        onSearchFilterChanged: refresh()
    }

    // Refresh when translations change
    Connections {
        target: TranslationManager
        function onTranslationsChanged() {
            // Save the fallback key of the first visible item (before refresh)
            var savedKey = ""
            var firstVisibleIndex = stringListView.indexAt(0, stringListView.contentY + 10)
            if (firstVisibleIndex >= 0 && firstVisibleIndex < stringModel.count) {
                savedKey = stringModel.get(firstVisibleIndex).fallback
            }

            stringModel.refresh()

            // Find and scroll back to the saved item
            if (savedKey) {
                for (var i = 0; i < stringModel.count; i++) {
                    if (stringModel.get(i).fallback === savedKey) {
                        stringListView.positionViewAtIndex(i, ListView.Beginning)
                        break
                    }
                }
            }
        }
    }

    // Main content area - properly positioned between status bar and bottom bar
    Item {
        id: contentArea
        anchors.fill: parent
        anchors.topMargin: Theme.pageTopMargin
        anchors.bottomMargin: Theme.bottomBarHeight
        anchors.leftMargin: Theme.spacingSmall
        anchors.rightMargin: Theme.spacingSmall

        // Header section - hidden when editing
        Column {
            id: headerSection
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            spacing: Theme.spacingSmall
            visible: !isEditing

            // Top row: Language name + progress + AI Translate button
            Item {
                width: parent.width
                height: Theme.scaled(40)

                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: TranslationManager.getLanguageDisplayName(TranslationManager.currentLanguage)
                    font: Theme.subtitleFont
                    color: Theme.textColor
                }

                // Progress + AI button grouped together on right
                Row {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.scaled(12)

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: {
                            var total = TranslationManager.uniqueStringCount()
                            var untranslated = TranslationManager.uniqueUntranslatedCount()
                            var translated = total - untranslated
                            return translated + "/" + total + " (" + Math.round((translated / Math.max(1, total)) * 100) + "%)"
                        }
                        font: Theme.labelFont
                        color: Theme.textSecondaryColor

                        Accessible.role: Accessible.StaticText
                        Accessible.name: {
                            var total = TranslationManager.uniqueStringCount()
                            var untranslated = TranslationManager.uniqueUntranslatedCount()
                            var translated = total - untranslated
                            var percent = Math.round((translated / Math.max(1, total)) * 100)
                            return "Translation progress: " + percent + " percent. " + translated + " of " + total + " strings translated."
                        }
                    }

                    // Clear All AI button (hidden for English)
                    Rectangle {
                        visible: !isEnglish
                        width: Theme.scaled(80)
                        height: Theme.scaled(36)
                        anchors.verticalCenter: parent.verticalCenter
                        color: clearAiArea.pressed ? Qt.darker(Theme.warningColor, 1.2) : Theme.warningColor
                        radius: Theme.buttonRadius

                        Text {
                            anchors.centerIn: parent
                            text: "Clear AI"
                            font: Theme.bodyFont
                            color: "white"
                        }

                        MouseArea {
                            id: clearAiArea
                            anchors.fill: parent
                            onClicked: clearAiPopup.open()
                        }
                    }

                    // AI Translate button (hidden for English)
                    Rectangle {
                        visible: !isEnglish
                        width: Theme.scaled(120)
                        height: Theme.scaled(36)
                        anchors.verticalCenter: parent.verticalCenter
                        color: aiButtonArea.pressed ? Qt.darker(Theme.primaryColor, 1.2) : Theme.primaryColor
                        radius: Theme.buttonRadius
                        opacity: !TranslationManager.autoTranslating ? 1.0 : 0.5

                        Accessible.role: Accessible.Button
                        Accessible.name: TranslationManager.autoTranslating ? "AI translation in progress" : "AI Translate all missing strings"
                        Accessible.description: "Use artificial intelligence to automatically translate all untranslated strings"
                        Accessible.focusable: true

                        Text {
                            anchors.centerIn: parent
                            text: TranslationManager.autoTranslating ? "Translating..." : "AI Translate"
                            font: Theme.bodyFont
                            color: "white"
                        }

                        MouseArea {
                            id: aiButtonArea
                            anchors.fill: parent
                            onClicked: {
                                if (TranslationManager.autoTranslating) return
                                if (!TranslationManager.canAutoTranslate()) {
                                    apiKeyPopup.open()
                                } else if (TranslationManager.uniqueUntranslatedCount() === 0) {
                                    allTranslatedPopup.open()
                                } else {
                                    TranslationManager.autoTranslate()
                                }
                            }
                        }
                    }
                }
            }

            // Search and filter row
            Item {
                width: parent.width
                height: Theme.scaled(36)

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: filterRow.left
                    anchors.rightMargin: Theme.spacingSmall
                    height: parent.height
                    color: Theme.surfaceColor
                    radius: Theme.buttonRadius
                    border.width: 1
                    border.color: searchField.activeFocus ? Theme.primaryColor : Theme.borderColor

                    Row {
                        anchors.fill: parent
                        anchors.margins: Theme.scaled(8)
                        spacing: Theme.scaled(8)

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "\u{1F50D}"
                            font.pixelSize: Theme.scaled(14)
                            color: Theme.textSecondaryColor
                        }

                        TextInput {
                            id: searchField
                            width: parent.width - Theme.scaled(40)
                            anchors.verticalCenter: parent.verticalCenter
                            font: Theme.bodyFont
                            color: Theme.textColor
                            clip: true

                            Accessible.role: Accessible.EditableText
                            Accessible.name: "Search strings"
                            Accessible.description: "Type to filter the list of translatable strings"
                            Accessible.focusable: true

                            Text {
                                anchors.fill: parent
                                text: "Search strings..."
                                font: parent.font
                                color: Theme.textSecondaryColor
                                visible: !parent.text && !parent.activeFocus
                                verticalAlignment: Text.AlignVCenter
                            }

                            onTextChanged: stringModel.searchFilter = text
                        }

                        Text {
                            id: clearSearchButton
                            anchors.verticalCenter: parent.verticalCenter
                            text: "\u{2715}"
                            font.pixelSize: Theme.scaled(14)
                            color: Theme.textSecondaryColor
                            visible: searchField.text !== ""

                            Accessible.role: Accessible.Button
                            Accessible.name: "Clear search"
                            Accessible.focusable: true

                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: Theme.scaled(-8)
                                onClicked: searchField.text = ""
                            }
                        }
                    }
                }

                Row {
                    id: filterRow
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.scaled(4)

                    Repeater {
                        model: isEnglish ? [
                            { text: "All", mode: 0, description: "Show all strings" },
                            { text: "Custom", mode: 3, description: "Show only customized strings" }
                        ] : [
                            { text: "All", mode: 0, description: "Show all strings" },
                            { text: "Missing", mode: 1, description: "Show only untranslated strings" },
                            { text: "AI", mode: 2, description: "Show only AI-generated translations" }
                        ]

                        Rectangle {
                            width: Theme.scaled(70)
                            height: Theme.scaled(36)
                            color: stringModel.filterMode === modelData.mode ? Theme.primaryColor : Theme.surfaceColor
                            radius: Theme.buttonRadius
                            border.width: stringModel.filterMode === modelData.mode ? 0 : 1
                            border.color: Theme.borderColor

                            Accessible.role: Accessible.RadioButton
                            Accessible.name: modelData.text + " filter"
                            Accessible.description: modelData.description
                            Accessible.checked: stringModel.filterMode === modelData.mode
                            Accessible.focusable: true

                            Text {
                                anchors.centerIn: parent
                                text: modelData.text
                                font: Theme.labelFont
                                color: stringModel.filterMode === modelData.mode ? "white" : Theme.textColor
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: stringModel.filterMode = modelData.mode
                            }
                        }
                    }
                }
            }

            // String count
            Text {
                text: stringModel.count + " unique strings"
                font: Theme.labelFont
                color: Theme.textSecondaryColor

                Accessible.role: Accessible.StaticText
                Accessible.name: stringModel.count + " unique strings shown"
            }
        }

        // Column headers - hidden when editing
        Rectangle {
            id: columnHeaders
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: headerSection.bottom
            anchors.topMargin: Theme.spacingSmall
            height: Theme.scaled(28)
            color: Theme.surfaceColor
            radius: Theme.scaled(4)
            visible: !isEditing

            // Calculate widths to match delegate columns
            readonly property real headerRowWidth: width - Theme.scaled(16)  // same as delegate rowWidth
            readonly property real col1Width: isEnglish ? headerRowWidth * 0.5 : headerRowWidth * 0.35
            readonly property real col2Width: headerRowWidth * 0.3
            readonly property real col3Width: isEnglish ? headerRowWidth * 0.5 - Theme.scaled(8) : headerRowWidth * 0.35 - Theme.scaled(8)

            Item {
                anchors.fill: parent
                anchors.margins: Theme.scaled(8)

                Text {
                    id: headerCol1
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: columnHeaders.col1Width
                    text: "English"
                    font.family: Theme.labelFont.family
                    font.pixelSize: Theme.labelFont.pixelSize
                    font.bold: true
                    color: Theme.textSecondaryColor
                }

                Text {
                    id: headerCol2
                    visible: !isEnglish
                    anchors.left: headerCol1.right
                    anchors.leftMargin: Theme.scaled(8)
                    anchors.verticalCenter: parent.verticalCenter
                    width: columnHeaders.col2Width
                    text: "AI Translation"
                    font.family: Theme.labelFont.family
                    font.pixelSize: Theme.labelFont.pixelSize
                    font.bold: true
                    color: Theme.primaryColor
                }

                Text {
                    anchors.left: isEnglish ? headerCol1.right : headerCol2.right
                    anchors.leftMargin: Theme.scaled(8)
                    anchors.verticalCenter: parent.verticalCenter
                    width: columnHeaders.col3Width
                    text: isEnglish ? "Custom Text" : "Final Translation"
                    font.family: Theme.labelFont.family
                    font.pixelSize: Theme.labelFont.pixelSize
                    font.bold: true
                    color: Theme.successColor
                }
            }
        }

        // String list
        ListView {
            id: stringListView
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: isEditing ? parent.top : columnHeaders.bottom
            anchors.topMargin: isEditing ? 0 : Theme.spacingSmall
            anchors.bottom: parent.bottom
            clip: true
            model: stringModel
            spacing: Theme.scaled(2)

            Accessible.role: Accessible.List
            Accessible.name: isEnglish ? "String customization list" : "Translation list"
            Accessible.description: stringModel.count + " strings. Swipe to navigate, double tap to edit."


            // Center editing item in visible area (accounting for keyboard)
            function centerOnItem(idx) {
                if (idx < 0 || idx >= count) return

                // First ensure item is instantiated
                positionViewAtIndex(idx, ListView.Center)

                // Then adjust for keyboard after a frame
                var pageHeight = stringListView.height * 2  // Approximate page height
                Qt.callLater(function() {
                    var item = itemAtIndex(idx)
                    if (!item) return

                    // Get keyboard height
                    var kbRect = Qt.inputMethod.keyboardRectangle
                    var kbHeight = kbRect.height / Screen.devicePixelRatio
                    if (kbHeight <= 0) {
                        // Estimate keyboard at 40% of page height
                        kbHeight = pageHeight * 0.4
                    }

                    // Calculate how much keyboard overlaps with ListView
                    // Keyboard covers from bottom of screen, bottom bar is below ListView
                    // So overlap = keyboard height - bottom bar height (if keyboard extends above bottom bar)
                    var overlap = Math.max(0, kbHeight - Theme.bottomBarHeight)

                    // Visible height is ListView height minus keyboard overlap
                    var visibleHeight = height - overlap
                    if (visibleHeight < Theme.scaled(100)) {
                        visibleHeight = height * 0.4  // Sanity fallback
                    }

                    // Get item center position in content coordinates
                    var itemCenter = item.y + item.height / 2

                    // Target: put item center at center of visible area
                    var visibleCenter = visibleHeight / 2
                    var targetY = itemCenter - visibleCenter

                    // Clamp to valid range
                    var maxY = Math.max(0, contentHeight - height)
                    contentY = Math.max(0, Math.min(targetY, maxY))
                })
            }

            delegate: Rectangle {
                id: delegateRoot
                width: stringListView.width
                // Calculate height based on tallest column content
                height: Math.max(Theme.scaled(48), rowHeight + Theme.scaled(16))
                color: index % 2 === 0 ? Theme.surfaceColor : Qt.darker(Theme.surfaceColor, 1.05)
                radius: Theme.scaled(4)

                // Functions at delegate level where singletons are accessible
                function setEditing(editing, idx) {
                    isEditing = editing
                    editingIndex = idx
                }

                function saveAndExitEditing(fallbackKey, newText, oldText) {
                    if (newText !== oldText) {
                        TranslationManager.setGroupTranslation(fallbackKey, newText)
                        // Clear AI translation when user manually edits (non-English)
                        if (newText !== "" && TranslationManager.currentLanguage !== "en") {
                            TranslationManager.clearAiTranslation(fallbackKey)
                        }
                    }
                    setEditing(false, -1)
                }

                // Accessibility: announce the English text and translation status
                Accessible.role: Accessible.ListItem
                Accessible.name: {
                    var name = "English: " + model.fallback + ". "
                    if (isEnglish) {
                        if (model.translation) {
                            name += "Custom text: " + model.translation
                        } else {
                            name += "No customization. Double tap to customize."
                        }
                    } else {
                        if (model.translation) {
                            name += "Translation: " + model.translation
                            if (model.isAiGenerated) {
                                name += ". AI generated."
                            }
                        } else if (model.aiTranslation) {
                            name += "AI suggestion: " + model.aiTranslation + ". Double tap to edit or accept."
                        } else {
                            name += "Not translated. Double tap to translate."
                        }
                    }
                    return name
                }
                Accessible.focusable: true
                Accessible.onPressAction: finalInput.forceActiveFocus()

                // Calculate the row width for column sizing
                readonly property real rowWidth: width - Theme.scaled(16)  // margins
                readonly property real col1Width: isEnglish ? rowWidth * 0.5 : rowWidth * 0.35
                readonly property real col2Width: rowWidth * 0.3
                readonly property real col3Width: isEnglish ? rowWidth * 0.5 - Theme.scaled(8) : rowWidth * 0.35 - Theme.scaled(8)

                // Hidden text elements to measure required height
                Text {
                    id: measureCol1
                    width: col1Width
                    text: model.fallback
                    font: Theme.bodyFont
                    wrapMode: Text.Wrap
                    visible: false
                }
                Text {
                    id: measureCol2
                    width: col2Width - Theme.scaled(8)
                    text: model.aiTranslation || "-"
                    font: Theme.bodyFont
                    wrapMode: Text.Wrap
                    visible: false
                }
                Text {
                    id: measureCol3
                    width: col3Width - Theme.scaled(8)
                    // Use live text from TextEdit when editing this row, otherwise model value
                    text: (finalInput.activeFocus ? finalInput.text : model.translation) || "Tap..."
                    font: Theme.bodyFont
                    wrapMode: Text.Wrap
                    visible: false
                }

                // Calculate max height needed
                readonly property real rowHeight: isEnglish
                    ? Math.max(measureCol1.implicitHeight, measureCol3.implicitHeight)
                    : Math.max(measureCol1.implicitHeight, measureCol2.implicitHeight, measureCol3.implicitHeight)

                // Content row
                Item {
                    id: contentRow
                    anchors.fill: parent
                    anchors.margins: Theme.scaled(8)

                    // English/Default column
                    Text {
                        id: englishText
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: col1Width
                        text: model.fallback
                        font: Theme.bodyFont
                        color: Theme.textColor
                        wrapMode: Text.Wrap
                        verticalAlignment: Text.AlignVCenter
                    }

                    // AI Translation column (hidden for English)
                    Rectangle {
                        id: aiColumn
                        visible: !isEnglish
                        anchors.left: englishText.right
                        anchors.leftMargin: Theme.scaled(8)
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: col2Width
                        color: aiCopyArea.pressed ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.2) : "transparent"
                        radius: Theme.scaled(4)

                        Accessible.role: Accessible.Button
                        Accessible.name: model.aiTranslation ? "AI suggestion: " + model.aiTranslation + ". Tap to use this translation." : "No AI translation available"
                        Accessible.focusable: model.aiTranslation ? true : false

                        Text {
                            anchors.left: parent.left
                            anchors.right: aiClearButton.left
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            anchors.margins: Theme.scaled(4)
                            text: model.aiTranslation || "-"
                            font: Theme.bodyFont
                            color: model.aiTranslation ? Theme.primaryColor : Theme.textSecondaryColor
                            wrapMode: Text.Wrap
                            verticalAlignment: Text.AlignVCenter
                            opacity: model.aiTranslation ? 1.0 : 0.5
                        }

                        // Clear AI translation button
                        Text {
                            id: aiClearButton
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.rightMargin: Theme.scaled(4)
                            width: model.aiTranslation ? Theme.scaled(20) : 0
                            visible: model.aiTranslation && model.aiTranslation !== ""
                            text: "\u2715"
                            font.pixelSize: Theme.scaled(14)
                            color: aiClearArea.pressed ? Theme.warningColor : Theme.textSecondaryColor
                            horizontalAlignment: Text.AlignHCenter

                            Accessible.role: Accessible.Button
                            Accessible.name: "Clear AI translation"
                            Accessible.focusable: true

                            MouseArea {
                                id: aiClearArea
                                anchors.fill: parent
                                anchors.margins: Theme.scaled(-8)
                                onClicked: {
                                    TranslationManager.clearAiTranslation(model.fallback)
                                }
                            }
                        }

                        MouseArea {
                            id: aiCopyArea
                            anchors.left: parent.left
                            anchors.right: aiClearButton.left
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            onClicked: {
                                if (model.aiTranslation && model.aiTranslation !== "") {
                                    TranslationManager.copyAiToFinal(model.fallback)
                                }
                            }
                        }
                    }

                    // Final/Custom column (editable)
                    Rectangle {
                        id: finalColumn
                        anchors.left: isEnglish ? englishText.right : aiColumn.right
                        anchors.leftMargin: Theme.scaled(8)
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        color: finalInput.activeFocus ? Qt.rgba(Theme.successColor.r, Theme.successColor.g, Theme.successColor.b, 0.1) : "transparent"
                        radius: Theme.scaled(4)
                        border.width: finalInput.activeFocus ? 2 : 0
                        border.color: Theme.successColor

                        // AI indicator badge (not for English)
                        Rectangle {
                            visible: !isEnglish && model.isAiGenerated
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: Theme.scaled(2)
                            width: Theme.scaled(16)
                            height: Theme.scaled(16)
                            radius: Theme.scaled(8)
                            color: Theme.primaryColor
                            z: 1

                            Text {
                                anchors.centerIn: parent
                                text: "AI"
                                font.pixelSize: Theme.scaled(8)
                                font.bold: true
                                color: "white"
                            }
                        }

                        // Use TextEdit for multi-line support
                        TextEdit {
                            id: finalInput
                            anchors.fill: parent
                            anchors.margins: Theme.scaled(4)
                            anchors.rightMargin: (!isEnglish && model.isAiGenerated) ? Theme.scaled(20) : Theme.scaled(4)
                            text: model.translation || ""
                            font: Theme.bodyFont
                            color: model.translation ? Theme.successColor : Theme.textSecondaryColor
                            wrapMode: TextEdit.Wrap
                            verticalAlignment: Text.AlignVCenter
                            readOnly: !activeFocus

                            Accessible.role: Accessible.EditableText
                            Accessible.focusable: true
                            Accessible.name: isEnglish
                                ? "Custom text for: " + model.fallback
                                : "Translation for: " + model.fallback
                            Accessible.description: {
                                if (isEnglish) {
                                    return model.translation
                                        ? "Current custom text: " + model.translation + ". Edit to change."
                                        : "No custom text set. Type to customize this string."
                                } else {
                                    return model.translation
                                        ? "Current translation: " + model.translation + ". Edit to change."
                                        : "Not translated. Type your translation here."
                                }
                            }

                            Text {
                                anchors.fill: parent
                                visible: !parent.text && !parent.activeFocus
                                text: isEnglish ? "Tap to customize..." : "Tap to translate..."
                                font: parent.font
                                color: Theme.textSecondaryColor
                                verticalAlignment: Text.AlignVCenter
                                wrapMode: Text.Wrap
                                opacity: 0.5
                            }

                            onActiveFocusChanged: {
                                if (activeFocus) {
                                    delegateRoot.setEditing(true, index)
                                    centerTimer.start()
                                } else {
                                    // Save on focus lost
                                    var newText = text.trim()
                                    if (newText !== (model.translation || "")) {
                                        TranslationManager.setGroupTranslation(model.fallback, newText)
                                    }
                                }
                            }

                            Keys.onReturnPressed: function(event) {
                                // Shift+Enter for newline, Enter alone to confirm
                                if (event.modifiers & Qt.ShiftModifier) {
                                    event.accepted = false  // Let it insert newline
                                } else {
                                    exitEditing()
                                }
                            }
                            Keys.onEnterPressed: function(event) {
                                if (event.modifiers & Qt.ShiftModifier) {
                                    event.accepted = false
                                } else {
                                    exitEditing()
                                }
                            }

                            function exitEditing() {
                                var newText = text.trim()
                                delegateRoot.saveAndExitEditing(model.fallback, newText, model.translation || "")
                                focus = false
                                Qt.inputMethod.hide()
                            }
                        }

                        // Tap area for easier activation
                        MouseArea {
                            anchors.fill: parent
                            visible: !finalInput.activeFocus
                            onClicked: finalInput.forceActiveFocus()
                        }
                    }
                }
            }

            Timer {
                id: centerTimer
                interval: 150
                onTriggered: stringListView.centerOnItem(editingIndex)
            }
        }
    }

    // AI Progress overlay
    Rectangle {
        anchors.fill: parent
        visible: TranslationManager.autoTranslating
        color: Qt.rgba(0, 0, 0, 0.7)
        z: 10

        MouseArea {
            anchors.fill: parent
            onClicked: {}
        }

        Rectangle {
            anchors.centerIn: parent
            width: Theme.scaled(340)
            height: progressContent.height + Theme.scaled(48)
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 2
            border.color: "white"

            Column {
                id: progressContent
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Theme.scaled(24)
                spacing: Theme.scaled(16)

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "AI Translation"
                    font: Theme.titleFont
                    color: Theme.textColor
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "Translating strings..."
                    font: Theme.bodyFont
                    color: Theme.textSecondaryColor
                }

                Text {
                    width: parent.width
                    text: TranslationManager.lastTranslatedText || "Starting..."
                    font: Theme.labelFont
                    color: Theme.primaryColor
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideMiddle
                }

                Rectangle {
                    width: parent.width
                    height: Theme.scaled(8)
                    color: Theme.backgroundColor
                    radius: Theme.scaled(4)

                    Rectangle {
                        width: parent.width * (TranslationManager.autoTranslateProgress / Math.max(1, TranslationManager.autoTranslateTotal))
                        height: parent.height
                        color: Theme.primaryColor
                        radius: Theme.scaled(4)
                        Behavior on width { NumberAnimation { duration: 200 } }
                    }
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: TranslationManager.autoTranslateProgress + " / " + TranslationManager.autoTranslateTotal
                    font: Theme.bodyFont
                    color: Theme.textColor
                }

                Rectangle {
                    width: parent.width
                    height: Theme.scaled(40)
                    color: cancelArea.pressed ? Qt.darker(Theme.warningColor, 1.2) : Theme.warningColor
                    radius: Theme.buttonRadius

                    Text {
                        anchors.centerIn: parent
                        text: "Cancel"
                        font: Theme.bodyFont
                        color: "white"
                    }

                    MouseArea {
                        id: cancelArea
                        anchors.fill: parent
                        onClicked: TranslationManager.cancelAutoTranslate()
                    }
                }
            }
        }
    }

    // API Key missing popup
    Popup {
        id: apiKeyPopup
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        dim: true
        padding: Theme.scaled(20)
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 2
            border.color: Theme.warningColor
        }

        contentItem: Column {
            spacing: Theme.scaled(16)
            width: Theme.scaled(320)

            Text {
                width: parent.width
                text: "AI Provider Not Configured"
                font: Theme.subtitleFont
                color: Theme.warningColor
                horizontalAlignment: Text.AlignHCenter
            }

            Text {
                width: parent.width
                text: "To use AI translation, configure an AI provider with a valid API key.\n\nGo to Settings → AI to set up:\n• OpenAI (GPT-4o-mini)\n• Anthropic (Claude)\n• Google (Gemini)\n• Ollama (local)"
                font: Theme.bodyFont
                color: Theme.textColor
                wrapMode: Text.Wrap
            }

            Row {
                width: parent.width
                spacing: Theme.scaled(8)

                Rectangle {
                    width: (parent.width - Theme.scaled(8)) / 2
                    height: Theme.scaled(40)
                    color: cancelApiArea.pressed ? Qt.darker(Theme.surfaceColor, 1.2) : Theme.surfaceColor
                    radius: Theme.buttonRadius
                    border.width: 1
                    border.color: Theme.borderColor

                    Text {
                        anchors.centerIn: parent
                        text: "Cancel"
                        font: Theme.bodyFont
                        color: Theme.textColor
                    }

                    MouseArea {
                        id: cancelApiArea
                        anchors.fill: parent
                        onClicked: apiKeyPopup.close()
                    }
                }

                Rectangle {
                    width: (parent.width - Theme.scaled(8)) / 2
                    height: Theme.scaled(40)
                    color: goAiArea.pressed ? Qt.darker(Theme.primaryColor, 1.2) : Theme.primaryColor
                    radius: Theme.buttonRadius

                    Text {
                        anchors.centerIn: parent
                        text: "Go to AI Settings"
                        font: Theme.bodyFont
                        color: "white"
                    }

                    MouseArea {
                        id: goAiArea
                        anchors.fill: parent
                        onClicked: {
                            apiKeyPopup.close()
                            pageStack.pop()
                            pageStack.pop()
                            pageStack.push("../AISettingsPage.qml")
                        }
                    }
                }
            }
        }
    }

    // All translated popup
    Popup {
        id: allTranslatedPopup
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        dim: true
        padding: Theme.scaled(20)
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 2
            border.color: Theme.successColor
        }

        contentItem: Column {
            spacing: Theme.scaled(16)
            width: Theme.scaled(280)

            Text {
                width: parent.width
                text: "All Done!"
                font: Theme.subtitleFont
                color: Theme.successColor
                horizontalAlignment: Text.AlignHCenter
            }

            Text {
                width: parent.width
                text: "All strings have already been translated."
                font: Theme.bodyFont
                color: Theme.textColor
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
            }

            Rectangle {
                width: parent.width
                height: Theme.scaled(40)
                color: okArea.pressed ? Qt.darker(Theme.successColor, 1.2) : Theme.successColor
                radius: Theme.buttonRadius

                Text {
                    anchors.centerIn: parent
                    text: "OK"
                    font: Theme.bodyFont
                    color: "white"
                }

                MouseArea {
                    id: okArea
                    anchors.fill: parent
                    onClicked: allTranslatedPopup.close()
                }
            }
        }
    }

    // Clear AI translations confirmation popup
    Popup {
        id: clearAiPopup
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        dim: true
        padding: Theme.scaled(20)
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 2
            border.color: Theme.warningColor
        }

        contentItem: Column {
            spacing: Theme.scaled(16)
            width: Theme.scaled(320)

            Text {
                width: parent.width
                text: "Clear All AI Translations?"
                font: Theme.subtitleFont
                color: Theme.warningColor
                horizontalAlignment: Text.AlignHCenter
            }

            Text {
                width: parent.width
                text: "This will delete all AI-generated translations for " + TranslationManager.getLanguageDisplayName(TranslationManager.currentLanguage) + ".\n\nHuman-edited translations will be preserved.\n\nYou can then re-translate with AI Translate."
                font: Theme.bodyFont
                color: Theme.textColor
                wrapMode: Text.Wrap
            }

            Row {
                width: parent.width
                spacing: Theme.scaled(8)

                Rectangle {
                    width: (parent.width - Theme.scaled(8)) / 2
                    height: Theme.scaled(40)
                    color: cancelClearArea.pressed ? Qt.darker(Theme.surfaceColor, 1.2) : Theme.surfaceColor
                    radius: Theme.buttonRadius
                    border.width: 1
                    border.color: Theme.borderColor

                    Text {
                        anchors.centerIn: parent
                        text: "Cancel"
                        font: Theme.bodyFont
                        color: Theme.textColor
                    }

                    MouseArea {
                        id: cancelClearArea
                        anchors.fill: parent
                        onClicked: clearAiPopup.close()
                    }
                }

                Rectangle {
                    width: (parent.width - Theme.scaled(8)) / 2
                    height: Theme.scaled(40)
                    color: confirmClearArea.pressed ? Qt.darker(Theme.warningColor, 1.2) : Theme.warningColor
                    radius: Theme.buttonRadius

                    Text {
                        anchors.centerIn: parent
                        text: "Clear All"
                        font: Theme.bodyFont
                        color: "white"
                    }

                    MouseArea {
                        id: confirmClearArea
                        anchors.fill: parent
                        onClicked: {
                            TranslationManager.clearAllAiTranslations()
                            clearAiPopup.close()
                        }
                    }
                }
            }
        }
    }

    // Bottom bar - anchored to page bottom
    BottomBar {
        id: bottomBar
        title: isEnglish ? "String Customizer" : "Translation Browser"
        onBackClicked: {
            if (isEditing) {
                isEditing = false
                editingIndex = -1
                stringListView.forceActiveFocus()
                Qt.inputMethod.hide()
            } else {
                pageStack.pop()
            }
        }
    }
}
