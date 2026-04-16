import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza
import ".."

Dialog {
    id: popup

    property string itemId: ""
    property string zoneName: ""
    property string pageContext: "idle"

    // Current values (set before opening)
    property string textContent: "Text"
    property string textAlign: "center"
    property string textAction: ""
    property string textLongPressAction: ""
    property string textDoubleclickAction: ""
    property string textEmoji: ""
    property string textBackgroundColor: ""
    property bool textHideBackground: false
    property bool showEmojiPicker: false

    signal saved()

    modal: true
    closePolicy: Dialog.CloseOnEscape
    padding: Theme.spacingSmall

    // Wide popup at top of window to avoid keyboard
    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Theme.spacingSmall
    width: parent.width - Theme.spacingSmall * 2
    height: Math.min(mainColumn.implicitHeight + Theme.scaled(28) + Theme.scaled(4) + padding * 2, parent.height * 0.85)

    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.color: Theme.borderColor
        border.width: 1
    }

    onClosed: {
        showEmojiPicker = false
        actionPickerPopup.close()
        profilePickerPopup.close()
    }

    // Detect malformed HTML (tags inside attribute values) and strip to plain text
    function sanitizeHtml(html) {
        if (!html || html.indexOf("<") < 0) return html
        var inTag = false
        var inQuote = false
        for (var i = 0; i < html.length; i++) {
            var ch = html[i]
            if (inQuote) {
                if (ch === '"') inQuote = false
                else if (ch === '<') {
                    console.warn("[CustomEditorPopup] Malformed HTML detected, stripping tags")
                    return html.replace(/<[^>]*>/g, "")
                }
            } else if (inTag) {
                if (ch === '"') inQuote = true
                else if (ch === '>') inTag = false
            } else {
                if (ch === '<') inTag = true
            }
        }
        return html
    }

    function openForItem(id, zone, props) {
        itemId = id
        zoneName = zone
        textAlign = props.align || "center"
        textAction = props.action || ""
        textLongPressAction = props.longPressAction || ""
        textDoubleclickAction = props.doubleclickAction || ""
        textEmoji = props.emoji || ""
        textBackgroundColor = props.backgroundColor || ""
        textHideBackground = props.hideBackground || false
        showEmojiPicker = false

        // Load segments if available, otherwise fall back to HTML content
        var segments = props.segments
        console.log("[CustomEditorPopup] openForItem id:", id, "has segments:", segments ? segments.length : 0, "content:", (props.content || "").substring(0, 80))
        if (segments && segments.length > 0) {
            // Break the text binding before loading segments into the document
            contentInput.text = ""
            formatter.fromSegments(segments)
            console.log("[CustomEditorPopup] Loaded from segments")
        } else {
            // Legacy item — load HTML into TextArea directly
            var rawContent = props.content || "Text"
            textContent = sanitizeHtml(rawContent)
            if (textContent !== rawContent) {
                console.warn("[CustomEditorPopup] Auto-saved sanitized content for item:", id)
                Settings.setItemProperty(id, "content", textContent)
            }
            contentInput.text = textContent
            console.log("[CustomEditorPopup] Loaded from HTML content:", textContent.substring(0, 80))
        }
        open()
    }

    function doSave() {
        Qt.inputMethod.commit()
        // Extract segments from document and compile to HTML
        var segments = formatter.toSegments()
        var html = formatter.segmentsToHtml(segments)
        console.log("[CustomEditorPopup] doSave segments:", JSON.stringify(segments))
        console.log("[CustomEditorPopup] doSave html:", html)
        textContent = html || "Text"

        Settings.setItemProperty(itemId, "content", textContent)
        Settings.setItemProperty(itemId, "segments", segments)
        Settings.setItemProperty(itemId, "align", textAlign)
        Settings.setItemProperty(itemId, "action", textAction)
        Settings.setItemProperty(itemId, "longPressAction", textLongPressAction)
        Settings.setItemProperty(itemId, "doubleclickAction", textDoubleclickAction)
        Settings.setItemProperty(itemId, "emoji", textEmoji)
        Settings.setItemProperty(itemId, "backgroundColor", textBackgroundColor)
        Settings.setItemProperty(itemId, "hideBackground", textHideBackground)
        saved()
        close()
    }

    function insertVariable(token) {
        var pos = contentInput.cursorPosition
        contentInput.insert(pos, token)
        contentInput.forceActiveFocus()
    }

    // Variable substitution for preview
    function substitutePreview(text) {
        if (!text) return ""
        var result = text
        result = result.replace(/%TEMP%/g, "92.3")
        result = result.replace(/%STEAM_TEMP%/g, "155\u00B0")
        result = result.replace(/%PRESSURE%/g, "9.0")
        result = result.replace(/%FLOW%/g, "2.1")
        result = result.replace(/%WATER%/g, "78")
        result = result.replace(/%WATER_ML%/g, "850")
        result = result.replace(/%STATE%/g, "Idle")
        result = result.replace(/%WEIGHT%/g, "36.2")
        result = result.replace(/%SHOT_TIME%/g, "28.5")
        result = result.replace(/%VOLUME%/g, "42")
        result = result.replace(/%TARGET_WEIGHT%/g, "36.0")
        result = result.replace(/%PROFILE%/g, "Adaptive v2")
        result = result.replace(/%TARGET_TEMP%/g, "93.0")
        result = result.replace(/%RATIO%/g, "2.0")
        result = result.replace(/%DOSE%/g, "18.0")
        result = result.replace(/%SCALE%/g, "Lunar")
        result = result.replace(/%GRIND%/g, "1.8.0")
        result = result.replace(/%GRINDER%/g, "Niche Zero")
        result = result.replace(/%MACHINE_READY%/g, "Ready")
        result = result.replace(/%MACHINE_READY_COLOR%/g, Theme.successColor)
        result = result.replace(/%CONNECTED%/g, "Online")
        result = result.replace(/%CONNECTED_COLOR%/g, Theme.successColor)
        result = result.replace(/%DEVICES%/g, "Machine + Scale")
        var now = new Date()
        result = result.replace(/%TIME%/g, Qt.formatTime(now, Settings.use12HourTime ? "h:mmap" : "hh:mm"))
        result = result.replace(/%DATE%/g, Qt.formatDate(now, "yyyy-MM-dd"))
        // Convert any emoji Unicode to <img> tags to avoid CoreText crash on macOS
        return Theme.replaceEmojiWithImg(result, Theme.bodyFont.pixelSize)
    }

    // Helper to get action label
    function getActionLabel(actionId) {
        if (!actionId) return TranslationManager.translate("customeditor.action.none", "None")
        // Handle parameterized actions like command:loadProfile:<name>
        if (actionId.indexOf("command:loadProfile:") === 0) {
            var profileName = actionId.substring("command:loadProfile:".length)
            var profiles = ProfileManager.availableProfiles
            for (var j = 0; j < profiles.length; j++) {
                if (profiles[j].name === profileName)
                    return TranslationManager.translate("customaction.command.loadProfile", "Load Profile") + ": " + profiles[j].title
            }
            return TranslationManager.translate("customaction.command.loadProfile", "Load Profile") + ": " + profileName
        }
        var actions = getFilteredActions()
        for (var i = 0; i < actions.length; i++) {
            if (actions[i].id === actionId) return actions[i].label
        }
        return actionId
    }

    contentItem: Item {
        HideKeyboardButton {
            anchors.right: parent.right
            anchors.rightMargin: Theme.scaled(8)
            anchors.top: parent.top
            anchors.topMargin: Theme.scaled(4)
            z: 10
        }

        Flickable {
            id: editorFlickable
            anchors.fill: parent
            anchors.bottomMargin: buttonRow.height + Theme.scaled(4)
            contentWidth: width
            contentHeight: mainColumn.implicitHeight
            clip: true
            flickableDirection: Flickable.VerticalFlick
            boundsBehavior: Flickable.StopAtBounds

            // Dismiss keyboard when tapping outside the text input area
            TapHandler {
                onTapped: function(eventPoint) {
                    if (contentInput.activeFocus) {
                        var pos = eventPoint.position
                        var mapped = editorFlickable.mapToItem(inputFlickable, pos.x, pos.y)
                        if (mapped.x >= 0 && mapped.y >= 0 &&
                            mapped.x <= inputFlickable.width && mapped.y <= inputFlickable.height) {
                            return  // Tap inside text input, don't dismiss
                        }
                        contentInput.focus = false
                        Qt.inputMethod.hide()
                    }
                }
            }

            ScrollBar.vertical: ScrollBar {
                policy: editorFlickable.contentHeight > editorFlickable.height
                        ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
            }

            ColumnLayout {
                id: mainColumn
                width: parent.width
                spacing: Theme.scaled(4)

            // === ROW 1: Icon/Emoji selector ===
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(8)

                Text {
                    text: TranslationManager.translate("customeditor.label.icon", "Icon")
                    color: Theme.textSecondaryColor
                    font: Theme.captionFont
                    Layout.alignment: Qt.AlignVCenter
                }

                // Current icon preview
                Rectangle {
                    width: Theme.scaled(40)
                    height: Theme.scaled(40)
                    radius: Theme.scaled(6)
                    color: Theme.backgroundColor
                    border.color: Theme.borderColor
                    border.width: 1

                    // Emoji/icon preview
                    Image {
                        visible: popup.textEmoji !== ""
                        anchors.centerIn: parent
                        source: visible ? Theme.emojiToImage(popup.textEmoji) : ""
                        sourceSize.width: Theme.scaled(28)
                        sourceSize.height: Theme.scaled(28)
                    }

                    // Empty state
                    Text {
                        visible: popup.textEmoji === ""
                        anchors.centerIn: parent
                        text: "—"
                        color: Theme.textSecondaryColor
                        font: Theme.captionFont
                    }
                }

                // Pick / Clear buttons
                Rectangle {
                    Layout.preferredWidth: pickText.implicitWidth + Theme.scaled(16)
                    height: Theme.scaled(28)
                    radius: Theme.scaled(6)
                    color: pickMa.pressed ? Theme.primaryColor : Theme.backgroundColor
                    border.color: Theme.primaryColor
                    border.width: 1
                    Accessible.role: Accessible.Button
                    Accessible.name: popup.showEmojiPicker ? TranslationManager.translate("customeditor.button.hidePicker", "Hide Picker") : TranslationManager.translate("customeditor.button.pickIcon", "Pick Icon")
                    Accessible.focusable: true
                    Accessible.onPressAction: pickMa.clicked(null)

                    Text {
                        id: pickText
                        anchors.centerIn: parent
                        text: popup.showEmojiPicker ? TranslationManager.translate("customeditor.button.hidePicker", "Hide Picker") : TranslationManager.translate("customeditor.button.pickIcon", "Pick Icon")
                        color: pickMa.pressed ? Theme.primaryContrastColor : Theme.primaryColor
                        font: Theme.captionFont
                        Accessible.ignored: true
                    }
                    MouseArea {
                        id: pickMa
                        anchors.fill: parent
                        onClicked: popup.showEmojiPicker = !popup.showEmojiPicker
                    }
                }

                Rectangle {
                    visible: popup.textEmoji !== ""
                    width: clearEmojiText.implicitWidth + Theme.scaled(16)
                    height: Theme.scaled(28)
                    radius: Theme.scaled(6)
                    color: clearEmojiMa.pressed ? Theme.errorColor : Theme.backgroundColor
                    border.color: Theme.errorColor
                    border.width: 1
                    Accessible.role: Accessible.Button
                    Accessible.name: TranslationManager.translate("customeditor.button.clearIcon", "Clear icon")
                    Accessible.focusable: true
                    Accessible.onPressAction: clearEmojiMa.clicked(null)

                    Text {
                        id: clearEmojiText
                        anchors.centerIn: parent
                        text: TranslationManager.translate("common.button.clear", "Clear")
                        color: clearEmojiMa.pressed ? Theme.primaryContrastColor : Theme.errorColor
                        font: Theme.captionFont
                        Accessible.ignored: true
                    }
                    MouseArea {
                        id: clearEmojiMa
                        anchors.fill: parent
                        onClicked: popup.textEmoji = ""
                    }
                }

                Item { Layout.fillWidth: true }
            }

            // Emoji picker (expandable)
            EmojiPicker {
                visible: popup.showEmojiPicker
                Layout.fillWidth: true
                selectedValue: popup.textEmoji
                onSelected: function(value) {
                    popup.textEmoji = value
                }
                onCleared: popup.textEmoji = ""
            }

            // === ROW 2: Content input + Preview ===
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(6)

                Rectangle {
                    Layout.fillWidth: true
                    height: Theme.scaled(112)
                    color: Theme.backgroundColor
                    radius: Theme.scaled(6)
                    border.color: contentInput.activeFocus ? Theme.primaryColor : Theme.borderColor
                    border.width: 1

                    Flickable {
                        id: inputFlickable
                        anchors.fill: parent
                        anchors.margins: Theme.scaled(4)
                        clip: true
                        flickableDirection: Flickable.VerticalFlick
                        boundsBehavior: Flickable.StopAtBounds

                        TextArea.flickable: TextArea {
                            id: contentInput
                            text: popup.textContent
                            textFormat: TextEdit.RichText
                            color: Theme.textColor
                            font: Theme.bodyFont
                            placeholderText: TranslationManager.translate("customeditor.placeholder.enterText", "Enter text...")
                            placeholderTextColor: Theme.textSecondaryColor
                            wrapMode: Text.Wrap
                            background: null
                            Accessible.role: Accessible.EditableText
                            Accessible.name: TranslationManager.translate("customeditor.placeholder.enterText", "Enter text")
                            Accessible.description: text
                            Accessible.focusable: true
                            topPadding: 0
                            bottomPadding: 0
                            leftPadding: 0
                            rightPadding: 0

                            // Selection is saved by DocumentFormatter.savedSelectionStart/End
                            onActiveFocusChanged: if (!activeFocus) Qt.inputMethod.hide()

                            DocumentFormatter {
                                id: formatter
                                document: contentInput.textDocument
                                selectionStart: contentInput.selectionStart
                                selectionEnd: contentInput.selectionEnd
                                cursorPosition: contentInput.cursorPosition
                            }
                        }

                        ScrollBar.vertical: ScrollBar {
                            width: Theme.scaled(4)
                            policy: inputFlickable.contentHeight > inputFlickable.height
                                    ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                        }
                    }
                }

                // Preview column: Full + Bar previews (1:1 match with CustomItem rendering)
                Column {
                    id: previewCol
                    spacing: Theme.scaled(4)

                    // Compile editor content to HTML with preview variable substitution
                    readonly property string previewHtml: {
                        var _ = contentInput.text  // Trigger re-evaluation on content changes
                        var segments = formatter.toSegments()
                        var html = formatter.segmentsToHtml(segments)
                        return popup.substitutePreview(html || "Text")
                    }
                    readonly property bool hasAction: popup.textAction !== "" || popup.textLongPressAction !== "" || popup.textDoubleclickAction !== ""
                    readonly property bool hasEmoji: popup.textEmoji !== ""
                    readonly property color fullBg: popup.textBackgroundColor || (hasAction ? "#555555" : Theme.surfaceColor)
                    readonly property color compactBg: popup.textBackgroundColor || "#555555"

                    // --- Full mode preview (exact match with CustomItem full mode) ---
                    Text {
                        text: TranslationManager.translate("customeditor.preview.full", "Full")
                        color: Theme.textSecondaryColor
                        font.family: Theme.captionFont.family
                        font.pixelSize: Theme.scaled(9)
                    }

                    Item {
                        width: previewCol.hasEmoji
                            ? Math.max(Theme.scaled(150), fpEmojiText.implicitWidth + Theme.scaled(24))
                            : (fpText.implicitWidth + Theme.scaled(16) + (previewCol.hasAction ? Theme.scaled(16) : 0))
                        height: previewCol.hasEmoji
                            ? Theme.scaled(120)
                            : (fpText.implicitHeight + Theme.scaled(16) + (previewCol.hasAction ? Theme.scaled(8) : 0))

                        Rectangle {
                            visible: !popup.textHideBackground && (previewCol.hasAction || previewCol.hasEmoji)
                            anchors.fill: parent
                            color: previewCol.fullBg
                            radius: Theme.cardRadius
                        }

                        // With emoji: icon above text
                        Column {
                            visible: previewCol.hasEmoji
                            anchors.centerIn: parent
                            spacing: Theme.spacingSmall

                            Image {
                                source: previewCol.hasEmoji ? Theme.emojiToImage(popup.textEmoji) : ""
                                sourceSize.width: Theme.scaled(48)
                                sourceSize.height: Theme.scaled(48)
                                anchors.horizontalCenter: parent.horizontalCenter
                            }

                            Text {
                                id: fpEmojiText
                                text: previewCol.previewHtml
                                textFormat: Text.RichText
                                color: Theme.textColor
                                font: Theme.bodyFont
                                horizontalAlignment: Text.AlignHCenter
                                anchors.horizontalCenter: parent.horizontalCenter
                            }
                        }

                        // Without emoji: text only
                        Text {
                            id: fpText
                            visible: !previewCol.hasEmoji
                            anchors.centerIn: parent
                            width: parent.width > 0 ? parent.width - (previewCol.hasAction ? Theme.scaled(24) : 0) : implicitWidth
                            text: previewCol.previewHtml
                            textFormat: Text.RichText
                            color: Theme.textColor
                            font: Theme.bodyFont
                            horizontalAlignment: popup.textAlign === "left" ? Text.AlignLeft
                                : popup.textAlign === "right" ? Text.AlignRight : Text.AlignHCenter
                            wrapMode: Text.Wrap
                        }
                    }

                    // --- Bar mode preview (exact match with CustomItem compact mode) ---
                    Text {
                        text: TranslationManager.translate("customeditor.preview.bar", "Bar")
                        color: Theme.textSecondaryColor
                        font.family: Theme.captionFont.family
                        font.pixelSize: Theme.scaled(9)
                    }

                    Item {
                        width: bpRow.implicitWidth + (previewCol.hasAction || popup.textBackgroundColor !== "" ? Theme.scaled(16) : 0)
                        height: Theme.bottomBarHeight

                        Rectangle {
                            visible: !popup.textHideBackground && (previewCol.hasAction || popup.textBackgroundColor !== "")
                            anchors.fill: parent
                            anchors.topMargin: Theme.spacingSmall
                            anchors.bottomMargin: Theme.spacingSmall
                            color: previewCol.compactBg
                            radius: Theme.cardRadius
                        }

                        RowLayout {
                            id: bpRow
                            anchors.centerIn: parent
                            spacing: Theme.spacingSmall

                            Image {
                                visible: previewCol.hasEmoji
                                source: visible ? Theme.emojiToImage(popup.textEmoji) : ""
                                sourceSize.width: Theme.scaled(28)
                                sourceSize.height: Theme.scaled(28)
                                Layout.alignment: Qt.AlignVCenter
                            }

                            Text {
                                text: previewCol.previewHtml
                                textFormat: Text.RichText
                                color: Theme.textColor
                                font: Theme.bodyFont
                                elide: Text.ElideRight
                                maximumLineCount: 1
                            }
                        }
                    }
                }
            }

            // === ROW 3: Three columns — Format/Color | Variables | Actions ===
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.scaled(6)

                // --- LEFT COLUMN: Format + Color ---
                ColumnLayout {
                    Layout.alignment: Qt.AlignTop
                    Layout.fillWidth: true
                    spacing: Theme.scaled(4)

                    // Format toolbar
                    Text {
                        text: TranslationManager.translate("customeditor.label.format", "Format")
                        color: Theme.textSecondaryColor
                        font: Theme.captionFont
                    }

                    RowLayout {
                        spacing: Theme.scaled(4)

                        // Bold
                        Rectangle {
                            width: Theme.scaled(30); height: Theme.scaled(30)
                            radius: Theme.scaled(4)
                            color: formatter.bold ? Theme.primaryColor : (boldMa.pressed ? Qt.darker(Theme.backgroundColor, 1.3) : Theme.backgroundColor)
                            border.color: Theme.borderColor; border.width: 1
                            Accessible.role: Accessible.Button
                            Accessible.name: TranslationManager.translate("customeditor.format.bold", "Bold") + (formatter.bold ? ", " + TranslationManager.translate("common.state.on", "on") : ", " + TranslationManager.translate("common.state.off", "off"))
                            Accessible.focusable: true
                            Accessible.onPressAction: boldMa.clicked(null)
                            Text {
                                anchors.centerIn: parent; text: "B"
                                color: formatter.bold ? Theme.primaryContrastColor : Theme.textColor; font.bold: true
                                font.pixelSize: Theme.captionFont.pixelSize
                                Accessible.ignored: true
                            }
                            MouseArea { id: boldMa; anchors.fill: parent; onClicked: formatter.toggleBold() }
                        }

                        // Italic
                        Rectangle {
                            width: Theme.scaled(30); height: Theme.scaled(30)
                            radius: Theme.scaled(4)
                            color: formatter.italic ? Theme.primaryColor : (italicMa.pressed ? Qt.darker(Theme.backgroundColor, 1.3) : Theme.backgroundColor)
                            border.color: Theme.borderColor; border.width: 1
                            Accessible.role: Accessible.Button
                            Accessible.name: TranslationManager.translate("customeditor.format.italic", "Italic") + (formatter.italic ? ", " + TranslationManager.translate("common.state.on", "on") : ", " + TranslationManager.translate("common.state.off", "off"))
                            Accessible.focusable: true
                            Accessible.onPressAction: italicMa.clicked(null)
                            Text {
                                anchors.centerIn: parent; text: "I"
                                color: formatter.italic ? Theme.primaryContrastColor : Theme.textColor; font.italic: true
                                font.pixelSize: Theme.captionFont.pixelSize
                                Accessible.ignored: true
                            }
                            MouseArea { id: italicMa; anchors.fill: parent; onClicked: formatter.toggleItalic() }
                        }

                        Rectangle { width: 1; height: Theme.scaled(20); color: Theme.borderColor }

                        // Font sizes
                        Repeater {
                            model: [
                                { label: "S", size: 12 },
                                { label: "M", size: 18 },
                                { label: "L", size: 28 },
                                { label: "XL", size: 48 }
                            ]
                            Rectangle {
                                readonly property bool isActive: formatter.currentFontSize === modelData.size
                                width: Theme.scaled(30); height: Theme.scaled(30)
                                radius: Theme.scaled(4)
                                color: isActive ? Theme.primaryColor : (sizeMa.pressed ? Qt.darker(Theme.backgroundColor, 1.3) : Theme.backgroundColor)
                                border.color: Theme.borderColor; border.width: 1
                                Accessible.role: Accessible.Button
                                Accessible.name: TranslationManager.translate("customeditor.format.fontSize", "Font size") + " " + modelData.label + (isActive ? ", " + TranslationManager.translate("combobox.selected", "selected") : "")
                                Accessible.focusable: true
                                Accessible.onPressAction: sizeMa.clicked(null)
                                Text {
                                    anchors.centerIn: parent; text: modelData.label
                                    color: parent.isActive ? Theme.primaryContrastColor : Theme.textColor; font: Theme.captionFont
                                    Accessible.ignored: true
                                }
                                MouseArea { id: sizeMa; anchors.fill: parent; onClicked: formatter.setFontSize(modelData.size) }
                            }
                        }
                    }

                    // Alignment
                    RowLayout {
                        spacing: Theme.scaled(4)

                        Repeater {
                            model: [
                                { label: "\u25C0", align: "left" },
                                { label: "\u25CF", align: "center" },
                                { label: "\u25B6", align: "right" }
                            ]
                            Rectangle {
                                width: Theme.scaled(30); height: Theme.scaled(30)
                                radius: Theme.scaled(4)
                                color: popup.textAlign === modelData.align ? Theme.primaryColor : Theme.backgroundColor
                                border.color: Theme.borderColor; border.width: 1
                                Accessible.role: Accessible.Button
                                Accessible.name: TranslationManager.translate("customeditor.format.align", "Align") + " " + modelData.align + (popup.textAlign === modelData.align ? ", " + TranslationManager.translate("combobox.selected", "selected") : "")
                                Accessible.focusable: true
                                Accessible.onPressAction: alignMa.clicked(null)
                                Text {
                                    anchors.centerIn: parent; text: modelData.label
                                    color: popup.textAlign === modelData.align ? Theme.primaryContrastColor : Theme.textColor
                                    font.pixelSize: Theme.scaled(10)
                                    Accessible.ignored: true
                                }
                                MouseArea { id: alignMa; anchors.fill: parent; onClicked: popup.textAlign = modelData.align }
                            }
                        }
                    }

                    // Color swatches
                    RowLayout {
                        spacing: Theme.scaled(6)

                        Text {
                            text: TranslationManager.translate("customeditor.label.color", "Color")
                            color: Theme.textSecondaryColor
                            font: Theme.captionFont
                        }

                        Rectangle {
                            width: Theme.scaled(26); height: Theme.scaled(26)
                            radius: Theme.scaled(13)
                            color: formatter.currentColor || "#ffffff"
                            border.color: Theme.borderColor; border.width: 1
                            Accessible.role: Accessible.Button
                            Accessible.name: TranslationManager.translate("customeditor.label.textColor", "Text color")
                            Accessible.focusable: true
                            Accessible.onPressAction: textColorMa.clicked(null)
                            MouseArea {
                                id: textColorMa
                                anchors.fill: parent
                                onClicked: {
                                    Qt.inputMethod.commit()
                                    colorPickerPopup.mode = "text"
                                    colorPickerPopup.initialColor = Qt.color(formatter.currentColor || "#ffffff")
                                    colorPickerPopup.open()
                                }
                            }
                        }

                        // Clear text color (reset selection to default)
                        Rectangle {
                            width: Theme.scaled(22); height: Theme.scaled(22)
                            radius: Theme.scaled(11)
                            color: clearFgMa.pressed ? Theme.errorColor : "transparent"
                            border.color: Theme.errorColor; border.width: 1
                            Accessible.role: Accessible.Button
                            Accessible.name: TranslationManager.translate("customeditor.button.clearTextColor", "Clear text color")
                            Accessible.focusable: true
                            Accessible.onPressAction: clearFgMa.clicked(null)
                            Text {
                                anchors.centerIn: parent
                                text: "\u00D7"
                                color: clearFgMa.pressed ? Theme.primaryContrastColor : Theme.errorColor
                                font.pixelSize: Theme.scaled(12)
                                Accessible.ignored: true
                            }
                            MouseArea {
                                id: clearFgMa
                                anchors.fill: parent
                                onClicked: formatter.clearFormatting()
                            }
                        }

                        Text {
                            text: TranslationManager.translate("customeditor.label.bg", "Bg")
                            color: Theme.textSecondaryColor
                            font: Theme.captionFont
                        }

                        Rectangle {
                            width: Theme.scaled(26); height: Theme.scaled(26)
                            radius: Theme.scaled(13)
                            color: popup.textBackgroundColor || Theme.backgroundColor
                            border.color: popup.textBackgroundColor ? Theme.primaryContrastColor : Theme.borderColor
                            border.width: popup.textBackgroundColor ? 2 : 1
                            Accessible.role: Accessible.Button
                            Accessible.name: TranslationManager.translate("customeditor.label.backgroundColor", "Background color")
                            Accessible.focusable: true
                            Accessible.onPressAction: bgColorMa.clicked(null)

                            Text {
                                visible: !popup.textBackgroundColor
                                anchors.centerIn: parent
                                text: "\u00D7"
                                color: Theme.textSecondaryColor
                                font.pixelSize: Theme.scaled(10)
                                Accessible.ignored: true
                            }

                            MouseArea {
                                id: bgColorMa
                                anchors.fill: parent
                                onClicked: {
                                    colorPickerPopup.mode = "bg"
                                    colorPickerPopup.initialColor = popup.textBackgroundColor
                                        ? Qt.color(popup.textBackgroundColor) : Qt.color("#333333")
                                    colorPickerPopup.open()
                                }
                            }
                        }

                        // Clear bg
                        Rectangle {
                            visible: popup.textBackgroundColor !== ""
                            width: Theme.scaled(22); height: Theme.scaled(22)
                            radius: Theme.scaled(11)
                            color: clearBgMa.pressed ? Theme.errorColor : "transparent"
                            border.color: Theme.errorColor; border.width: 1
                            Accessible.role: Accessible.Button
                            Accessible.name: TranslationManager.translate("customeditor.button.clearBgColor", "Clear background color")
                            Accessible.focusable: true
                            Accessible.onPressAction: clearBgMa.clicked(null)
                            Text {
                                anchors.centerIn: parent
                                text: "\u00D7"
                                color: clearBgMa.pressed ? Theme.primaryContrastColor : Theme.errorColor
                                font.pixelSize: Theme.scaled(12)
                                Accessible.ignored: true
                            }
                            MouseArea {
                                id: clearBgMa
                                anchors.fill: parent
                                onClicked: popup.textBackgroundColor = ""
                            }
                        }

                        // Hide background toggle
                        Rectangle {
                            width: noBgText.implicitWidth + Theme.scaled(12)
                            height: Theme.scaled(22)
                            radius: Theme.scaled(11)
                            color: popup.textHideBackground ? Theme.primaryColor : "transparent"
                            border.color: popup.textHideBackground ? Theme.primaryColor : Theme.borderColor
                            border.width: 1
                            Accessible.role: Accessible.Button
                            Accessible.name: TranslationManager.translate("customeditor.button.hideBackground", "Hide background") + (popup.textHideBackground ? ", " + TranslationManager.translate("common.state.on", "on") : ", " + TranslationManager.translate("common.state.off", "off"))
                            Accessible.focusable: true
                            Accessible.onPressAction: noBgMa.clicked(null)
                            Text {
                                id: noBgText
                                anchors.centerIn: parent
                                text: TranslationManager.translate("customeditor.button.noBg", "No Bg")
                                color: popup.textHideBackground ? Theme.primaryContrastColor : Theme.textSecondaryColor
                                font: Theme.captionFont
                                Accessible.ignored: true
                            }
                            MouseArea {
                                id: noBgMa
                                anchors.fill: parent
                                onClicked: popup.textHideBackground = !popup.textHideBackground
                            }
                        }
                    }
                }

                // Vertical separator
                Rectangle {
                    Layout.fillHeight: true
                    width: 1
                    color: Theme.borderColor
                }

                // --- CENTER COLUMN: Variables button ---
                ColumnLayout {
                    Layout.preferredWidth: Theme.scaled(85)
                    Layout.maximumWidth: Theme.scaled(100)
                    Layout.fillHeight: true
                    spacing: Theme.scaled(2)

                    Text {
                        text: TranslationManager.translate("customeditor.label.variables", "Variables")
                        color: Theme.textSecondaryColor
                        font: Theme.captionFont
                    }

                    AccessibleButton {
                        Layout.fillWidth: true
                        text: TranslationManager.translate("customeditor.insertVariable", "Insert...")
                        accessibleName: TranslationManager.translate("customeditor.insertVariable.accessible", "Insert variable")
                        onClicked: variablePickerDialog.open()
                    }
                }

                // Vertical separator
                Rectangle {
                    Layout.fillHeight: true
                    width: 1
                    color: Theme.borderColor
                }

                // --- RIGHT COLUMN: Actions (3 gesture selectors) ---
                ColumnLayout {
                    Layout.preferredWidth: Theme.scaled(110)
                    Layout.maximumWidth: Theme.scaled(130)
                    Layout.alignment: Qt.AlignTop
                    spacing: Theme.scaled(4)

                    Text {
                        text: TranslationManager.translate("customeditor.label.actions", "Actions")
                        color: Theme.textSecondaryColor
                        font: Theme.captionFont
                    }

                    // Click action selector
                    Rectangle {
                        Layout.fillWidth: true
                        height: Theme.scaled(28)
                        radius: Theme.scaled(4)
                        color: Theme.backgroundColor
                        border.color: popup.textAction ? Theme.primaryColor : Theme.borderColor
                        border.width: 1
                        Accessible.role: Accessible.Button
                        Accessible.name: TranslationManager.translate("customeditor.accessible.clickAction", "Click action") + ", " + popup.getActionLabel(popup.textAction)
                        Accessible.focusable: true
                        Accessible.onPressAction: clickActionMa.clicked(null)

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.scaled(4)
                            anchors.rightMargin: Theme.scaled(4)

                            Text {
                                text: TranslationManager.translate("customeditor.gesture.click", "Click:")
                                color: Theme.textSecondaryColor
                                font: Theme.captionFont
                                Accessible.ignored: true
                            }
                            Text {
                                Layout.fillWidth: true
                                text: popup.getActionLabel(popup.textAction)
                                color: popup.textAction ? Theme.primaryColor : Theme.textColor
                                font: Theme.captionFont
                                elide: Text.ElideRight
                                Accessible.ignored: true
                            }
                        }
                        MouseArea {
                            id: clickActionMa
                            anchors.fill: parent
                            onClicked: {
                                actionPickerPopup.gesture = "click"
                                actionPickerPopup.open()
                            }
                        }
                    }

                    // Long Press action selector
                    Rectangle {
                        Layout.fillWidth: true
                        height: Theme.scaled(28)
                        radius: Theme.scaled(4)
                        color: Theme.backgroundColor
                        border.color: popup.textLongPressAction ? Theme.primaryColor : Theme.borderColor
                        border.width: 1
                        Accessible.role: Accessible.Button
                        Accessible.name: TranslationManager.translate("customeditor.accessible.longPressAction", "Long press action") + ", " + popup.getActionLabel(popup.textLongPressAction)
                        Accessible.focusable: true
                        Accessible.onPressAction: longPressActionMa.clicked(null)

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.scaled(4)
                            anchors.rightMargin: Theme.scaled(4)

                            Text {
                                text: TranslationManager.translate("customeditor.gesture.long", "Long:")
                                color: Theme.textSecondaryColor
                                font: Theme.captionFont
                                Accessible.ignored: true
                            }
                            Text {
                                Layout.fillWidth: true
                                text: popup.getActionLabel(popup.textLongPressAction)
                                color: popup.textLongPressAction ? Theme.primaryColor : Theme.textColor
                                font: Theme.captionFont
                                elide: Text.ElideRight
                                Accessible.ignored: true
                            }
                        }
                        MouseArea {
                            id: longPressActionMa
                            anchors.fill: parent
                            onClicked: {
                                actionPickerPopup.gesture = "longpress"
                                actionPickerPopup.open()
                            }
                        }
                    }

                    // Double Click action selector
                    Rectangle {
                        Layout.fillWidth: true
                        height: Theme.scaled(28)
                        radius: Theme.scaled(4)
                        color: Theme.backgroundColor
                        border.color: popup.textDoubleclickAction ? Theme.primaryColor : Theme.borderColor
                        border.width: 1
                        Accessible.role: Accessible.Button
                        Accessible.name: TranslationManager.translate("customeditor.accessible.doubleClickAction", "Double click action") + ", " + popup.getActionLabel(popup.textDoubleclickAction)
                        Accessible.focusable: true
                        Accessible.onPressAction: dblClickActionMa.clicked(null)

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.scaled(4)
                            anchors.rightMargin: Theme.scaled(4)

                            Text {
                                text: TranslationManager.translate("customeditor.gesture.doubleClick", "DblClk:")
                                color: Theme.textSecondaryColor
                                font: Theme.captionFont
                                Accessible.ignored: true
                            }
                            Text {
                                Layout.fillWidth: true
                                text: popup.getActionLabel(popup.textDoubleclickAction)
                                color: popup.textDoubleclickAction ? Theme.primaryColor : Theme.textColor
                                font: Theme.captionFont
                                elide: Text.ElideRight
                                Accessible.ignored: true
                            }
                        }
                        MouseArea {
                            id: dblClickActionMa
                            anchors.fill: parent
                            onClicked: {
                                actionPickerPopup.gesture = "doubleclick"
                                actionPickerPopup.open()
                            }
                        }
                    }
                }
            }

            }
        }

        // === Buttons (outside ScrollView, always visible and clickable) ===
        RowLayout {
            id: buttonRow
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            spacing: Theme.scaled(4)

            Item { Layout.fillWidth: true }

            // Cancel
            Rectangle {
                Layout.preferredWidth: Theme.scaled(70)
                Layout.preferredHeight: Theme.scaled(28)
                radius: Theme.scaled(6)
                color: cancelMa.pressed ? Qt.darker(Theme.backgroundColor, 1.2) : Theme.backgroundColor
                border.color: Theme.borderColor; border.width: 1
                Accessible.role: Accessible.Button
                Accessible.name: TranslationManager.translate("common.button.cancel", "Cancel")
                Accessible.focusable: true
                Accessible.onPressAction: cancelMa.clicked(null)

                Text {
                    anchors.centerIn: parent
                    text: TranslationManager.translate("common.button.cancel", "Cancel")
                    color: Theme.textColor
                    font: Theme.captionFont
                    Accessible.ignored: true
                }
                MouseArea { id: cancelMa; anchors.fill: parent; onClicked: popup.close() }
            }

            // Save
            Rectangle {
                Layout.preferredWidth: Theme.scaled(70)
                Layout.preferredHeight: Theme.scaled(28)
                radius: Theme.scaled(6)
                color: saveMa.pressed ? Qt.darker(Theme.primaryColor, 1.2) : Theme.primaryColor
                Accessible.role: Accessible.Button
                Accessible.name: TranslationManager.translate("common.button.save", "Save")
                Accessible.focusable: true
                Accessible.onPressAction: saveMa.clicked(null)

                Text {
                    anchors.centerIn: parent
                    text: TranslationManager.translate("common.button.save", "Save")
                    color: Theme.primaryContrastColor
                    font: Theme.captionFont
                    Accessible.ignored: true
                }
                MouseArea { id: saveMa; anchors.fill: parent; onClicked: popup.doSave() }
            }
        }
    }

    // === Color picker popup (deferred via Loader) ===
    Dialog {
        id: colorPickerPopup
        property string mode: "text"  // "text" or "bg"
        property color initialColor: "#ffffff"
        // Selection is tracked by formatter.savedSelectionStart/End (survives focus loss)

        parent: Overlay.overlay
        modal: true
        closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside
        padding: Theme.spacingMedium

        x: Theme.spacingSmall
        y: Theme.spacingSmall
        width: parent.width - Theme.spacingSmall * 2
        height: parent.height * 0.38

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.borderColor
            border.width: 1
        }

        onOpened: cpLoader.active = true
        onClosed: cpLoader.active = false

        contentItem: Loader {
            id: cpLoader
            active: false
            onLoaded: {
                if (item && typeof item.setColor === "function")
                    item.setColor(colorPickerPopup.initialColor)
            }
            sourceComponent: ColumnLayout {
                function setColor(c) { cpEditorInner.setColor(c) }
                spacing: Theme.scaled(6)

                RowLayout {
                    spacing: Theme.spacingMedium
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ColorEditor {
                        id: cpEditorInner
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                    }

                    ColumnLayout {
                        Layout.alignment: Qt.AlignVCenter
                        spacing: Theme.scaled(8)

                        Rectangle {
                            width: Theme.scaled(48); height: Theme.scaled(48)
                            radius: Theme.scaled(8)
                            color: cpEditorInner.color
                            border.color: Theme.primaryContrastColor; border.width: 2
                            Layout.alignment: Qt.AlignHCenter
                        }

                        Text {
                            text: colorPickerPopup.mode === "text" ? TranslationManager.translate("customeditor.colorpicker.textColor", "Text Color") : TranslationManager.translate("customeditor.colorpicker.background", "Background")
                            color: Theme.textSecondaryColor
                            font: Theme.captionFont
                            Layout.alignment: Qt.AlignHCenter
                        }

                        Rectangle {
                            Layout.preferredWidth: Theme.scaled(80)
                            height: Theme.scaled(32)
                            radius: Theme.scaled(6)
                            color: cpApplyMa.pressed ? Qt.darker(Theme.primaryColor, 1.2) : Theme.primaryColor
                            Accessible.role: Accessible.Button
                            Accessible.name: TranslationManager.translate("common.button.apply", "Apply")
                            Accessible.focusable: true
                            Accessible.onPressAction: cpApplyMa.clicked(null)
                            Text { anchors.centerIn: parent; text: TranslationManager.translate("common.button.apply", "Apply"); color: Theme.primaryContrastColor; font: Theme.captionFont; Accessible.ignored: true }
                            MouseArea {
                                id: cpApplyMa
                                anchors.fill: parent
                                onClicked: {
                                    var c = cpEditorInner.color.toString()
                                    if (colorPickerPopup.mode === "text") {
                                        formatter.setColorOnRange(c, formatter.savedSelectionStart, formatter.savedSelectionEnd)
                                    } else {
                                        popup.textBackgroundColor = c
                                    }
                                    colorPickerPopup.close()
                                }
                            }
                        }

                        Rectangle {
                            Layout.preferredWidth: Theme.scaled(80)
                            height: Theme.scaled(32)
                            radius: Theme.scaled(6)
                            color: cpCloseMa.pressed ? Qt.darker(Theme.backgroundColor, 1.2) : Theme.backgroundColor
                            border.color: Theme.borderColor; border.width: 1
                            Accessible.role: Accessible.Button
                            Accessible.name: TranslationManager.translate("common.button.close", "Close")
                            Accessible.focusable: true
                            Accessible.onPressAction: cpCloseMa.clicked(null)
                            Text { anchors.centerIn: parent; text: TranslationManager.translate("common.button.close", "Close"); color: Theme.textColor; font: Theme.captionFont; Accessible.ignored: true }
                            MouseArea { id: cpCloseMa; anchors.fill: parent; onClicked: colorPickerPopup.close() }
                        }
                    }
                }

                // Theme color swatches — quick pick
                Flow {
                    Layout.fillWidth: true
                    spacing: Theme.scaled(4)

                    Repeater {
                        model: [
                            { color: "#ffffff", label: TranslationManager.translate("customEditor.colorWhite", "White") },
                            { color: Theme.temperatureColor, label: TranslationManager.translate("customEditor.colorTemperature", "Temperature") },
                            { color: Theme.errorColor, label: TranslationManager.translate("customEditor.colorError", "Error") },
                            { color: Theme.warningColor, label: TranslationManager.translate("customEditor.colorWarning", "Warning") },
                            { color: Theme.accentColor, label: TranslationManager.translate("customEditor.colorAccent", "Accent") },
                            { color: Theme.successColor, label: TranslationManager.translate("customEditor.colorSuccess", "Success") },
                            { color: Theme.pressureColor, label: TranslationManager.translate("customEditor.colorPressure", "Pressure") },
                            { color: Theme.primaryColor, label: TranslationManager.translate("customEditor.colorPrimary", "Primary") },
                            { color: Theme.flowColor, label: TranslationManager.translate("customEditor.colorFlow", "Flow") },
                            { color: Theme.weightColor, label: TranslationManager.translate("customEditor.colorWeight", "Weight") },
                            { color: Theme.textSecondaryColor, label: TranslationManager.translate("customEditor.colorSecondary", "Secondary") }
                        ]
                        Rectangle {
                            required property var modelData
                            width: Theme.scaled(22); height: Theme.scaled(22)
                            radius: width / 2
                            color: modelData.color
                            border.color: swatchMa.containsMouse ? Theme.primaryContrastColor : Theme.borderColor
                            border.width: swatchMa.containsMouse ? 2 : 1
                            Accessible.role: Accessible.Button
                            Accessible.name: modelData.label + " " + TranslationManager.translate("customeditor.accessible.colorSwatch", "color swatch")
                            Accessible.focusable: true
                            Accessible.onPressAction: swatchMa.clicked(null)

                            MouseArea {
                                id: swatchMa
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    var c = parent.modelData.color.toString()
                                    if (colorPickerPopup.mode === "text") {
                                        formatter.setColorOnRange(c, formatter.savedSelectionStart, formatter.savedSelectionEnd)
                                    } else {
                                        popup.textBackgroundColor = c
                                    }
                                    colorPickerPopup.close()
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Variable items for insertion into text content
    readonly property var _variableItems: [
        { token: "%TEMP%", label: TranslationManager.translate("customeditor.var.temp", "Temp (\u00B0C)") },
        { token: "%STEAM_TEMP%", label: TranslationManager.translate("customeditor.var.steamTemp", "Steam (\u00B0C)") },
        { token: "%PRESSURE%", label: TranslationManager.translate("customeditor.var.pressure", "Pressure (bar)") },
        { token: "%FLOW%", label: TranslationManager.translate("customeditor.var.flow", "Flow (ml/s)") },
        { token: "%WATER%", label: TranslationManager.translate("customeditor.var.water", "Water (%)") },
        { token: "%WATER_ML%", label: TranslationManager.translate("customeditor.var.waterMl", "Water (ml)") },
        { token: "%WEIGHT%", label: TranslationManager.translate("customeditor.var.weight", "Weight (g)") },
        { token: "%SHOT_TIME%", label: TranslationManager.translate("customeditor.var.shotTime", "Shot Time (s)") },
        { token: "%TARGET_WEIGHT%", label: TranslationManager.translate("customeditor.var.targetWeight", "Target Wt (g)") },
        { token: "%VOLUME%", label: TranslationManager.translate("customeditor.var.volume", "Volume (ml)") },
        { token: "%PROFILE%", label: TranslationManager.translate("customeditor.var.profile", "Profile Name") },
        { token: "%STATE%", label: TranslationManager.translate("customeditor.var.state", "Machine State") },
        { token: "%TARGET_TEMP%", label: TranslationManager.translate("customeditor.var.targetTemp", "Target Temp") },
        { token: "%SCALE%", label: TranslationManager.translate("customeditor.var.scale", "Scale Name") },
        { token: "%TIME%", label: TranslationManager.translate("customeditor.var.time", "Time (HH:MM)") },
        { token: "%DATE%", label: TranslationManager.translate("customeditor.var.date", "Date") },
        { token: "%RATIO%", label: TranslationManager.translate("customeditor.var.ratio", "Brew Ratio") },
        { token: "%DOSE%", label: TranslationManager.translate("customeditor.var.dose", "Dose (g)") },
        { token: "%GRIND%", label: TranslationManager.translate("customeditor.var.grind", "Grind Setting") },
        { token: "%GRINDER%", label: TranslationManager.translate("customeditor.var.grinder", "Grinder Model") },
        { token: "%MACHINE_READY%", label: TranslationManager.translate("customeditor.var.machineReady", "Ready/Not ready") },
        { token: "%MACHINE_READY_COLOR%", label: TranslationManager.translate("customeditor.var.readyColor", "Ready Color") },
        { token: "%CONNECTED%", label: TranslationManager.translate("customeditor.var.connected", "Online/Offline") },
        { token: "%CONNECTED_COLOR%", label: TranslationManager.translate("customeditor.var.statusColor", "Status Color") },
        { token: "%DEVICES%", label: TranslationManager.translate("customeditor.var.devices", "Devices") },
        { token: "%MACHINE_CONNECTED%", label: TranslationManager.translate("customeditor.var.machineConnected", "Machine On/Off") },
        { token: "%SCALE_CONNECTED%", label: TranslationManager.translate("customeditor.var.scaleConnected", "Scale On/Off") }
    ]

    // === Variable picker dialog ===
    SelectionDialog {
        id: variablePickerDialog
        title: TranslationManager.translate("customeditor.label.variables", "Variables")
        options: popup._variableItems.map(function(item) { return item.label })
        onSelected: function(index, value) {
            popup.insertVariable(popup._variableItems[index].token)
        }
    }

    // Action matching helper: "command:loadProfile" matches any "command:loadProfile:<name>"
    function actionMatches(current, target) {
        if (current === target) return true
        if (target === "command:loadProfile") return current.indexOf("command:loadProfile:") === 0
        return false
    }

    // Build action items list with "None" prepended
    function buildActionItems() {
        var items = [{ id: "", label: TranslationManager.translate("customeditor.action.none", "None") }]
        var filtered = popup.getFilteredActions()
        for (var i = 0; i < filtered.length; i++)
            items.push(filtered[i])
        return items
    }

    // Find the currently selected action index for a given gesture
    function findCurrentActionIndex(gesture, items) {
        var currentAction = ""
        switch (gesture) {
            case "click": currentAction = popup.textAction; break
            case "longpress": currentAction = popup.textLongPressAction; break
            case "doubleclick": currentAction = popup.textDoubleclickAction; break
        }
        for (var i = 0; i < items.length; i++) {
            if (actionMatches(currentAction, items[i].id))
                return i
        }
        return -1
    }

    // === Action picker dialog ===
    SelectionDialog {
        id: actionPickerPopup
        property string gesture: "click"
        property var _actionItems: []

        title: {
            switch (gesture) {
                case "click": return TranslationManager.translate("customeditor.dialog.clickAction", "Click Action")
                case "longpress": return TranslationManager.translate("customeditor.dialog.longPressAction", "Long Press Action")
                case "doubleclick": return TranslationManager.translate("customeditor.dialog.doubleClickAction", "Double Click Action")
                default: return TranslationManager.translate("customeditor.dialog.action", "Action")
            }
        }
        options: _actionItems.map(function(item) { return item.label })
        currentIndex: popup.findCurrentActionIndex(gesture, _actionItems)

        onAboutToShow: _actionItems = popup.buildActionItems()

        onSelected: function(index, value) {
            var actionId = _actionItems[index].id
            if (actionId === "command:loadProfile") {
                profilePickerPopup.gesture = gesture
                profilePickerPopup.open()
                return
            }
            switch (gesture) {
                case "click": popup.textAction = actionId; break
                case "longpress": popup.textLongPressAction = actionId; break
                case "doubleclick": popup.textDoubleclickAction = actionId; break
            }
        }
    }

    // Profile picker sub-dialog (opened when "Load Profile..." is selected)
    SelectionDialog {
        id: profilePickerPopup
        property string gesture: "click"
        property var _profiles: []

        title: TranslationManager.translate("customaction.command.loadProfile", "Load Profile")
        options: _profiles.map(function(p) { return p.title })
        currentIndex: {
            var currentAction = ""
            switch (gesture) {
                case "click": currentAction = popup.textAction; break
                case "longpress": currentAction = popup.textLongPressAction; break
                case "doubleclick": currentAction = popup.textDoubleclickAction; break
            }
            for (var i = 0; i < _profiles.length; i++) {
                if (currentAction === "command:loadProfile:" + _profiles[i].fileName)
                    return i
            }
            return -1
        }

        onAboutToShow: {
            var profiles = ProfileManager.availableProfiles
            var items = []
            for (var i = 0; i < profiles.length; i++)
                items.push({ title: profiles[i].title, fileName: profiles[i].name })
            _profiles = items
        }

        onSelected: function(index, value) {
            var actionId = "command:loadProfile:" + _profiles[index].fileName
            switch (gesture) {
                case "click": popup.textAction = actionId; break
                case "longpress": popup.textLongPressAction = actionId; break
                case "doubleclick": popup.textDoubleclickAction = actionId; break
            }
        }
    }

    // Action registry with page context filtering
    function getFilteredActions() {
        var allActions = [
            { id: "navigate:settings",      label: TranslationManager.translate("customaction.navigate.settings", "Go to Settings"),             contexts: ["idle", "all"] },
            { id: "navigate:history",        label: TranslationManager.translate("customaction.navigate.history", "Go to History"),               contexts: ["idle", "all"] },
            { id: "navigate:profiles",       label: TranslationManager.translate("customaction.navigate.profiles", "Go to Profiles"),             contexts: ["idle", "all"] },
            { id: "navigate:profileEditor",  label: TranslationManager.translate("customaction.navigate.profileEditor", "Go to Profile Editor"),  contexts: ["idle", "all"] },
            { id: "navigate:recipes",        label: TranslationManager.translate("customaction.navigate.recipes", "Go to Recipes"),               contexts: ["idle", "all"] },
            { id: "navigate:descaling",      label: TranslationManager.translate("customaction.navigate.descaling", "Go to Descaling"),           contexts: ["idle", "all"] },
            { id: "navigate:ai",             label: TranslationManager.translate("customaction.navigate.ai", "Go to AI Settings"),                contexts: ["idle", "all"] },
            { id: "navigate:visualizer",     label: TranslationManager.translate("customaction.navigate.visualizer", "Go to Visualizer"),         contexts: ["idle", "all"] },
            { id: "navigate:autofavorites",  label: TranslationManager.translate("customaction.navigate.autofavorites", "Go to Favorites"),       contexts: ["idle", "all"] },
            { id: "navigate:steam",          label: TranslationManager.translate("customaction.navigate.steam", "Go to Steam"),                   contexts: ["idle"] },
            { id: "navigate:hotwater",       label: TranslationManager.translate("customaction.navigate.hotwater", "Go to Hot Water"),             contexts: ["idle"] },
            { id: "navigate:flush",          label: TranslationManager.translate("customaction.navigate.flush", "Go to Flush"),                   contexts: ["idle"] },
            { id: "navigate:beaninfo",       label: TranslationManager.translate("customaction.navigate.beaninfo", "Go to Bean Info"),             contexts: ["idle", "all"] },
            { id: "navigate:espresso",       label: TranslationManager.translate("customaction.navigate.espresso", "Go to Espresso"),             contexts: ["idle"] },
            { id: "navigate:community",      label: TranslationManager.translate("customaction.navigate.community", "Go to Community"),           contexts: ["idle", "all"] },
            { id: "navigate:flowCalibration", label: TranslationManager.translate("customaction.navigate.flowCalibration", "Go to Flow Calibration"), contexts: ["idle", "all"] },
            { id: "navigate:profileImport",  label: TranslationManager.translate("customaction.navigate.profileImport", "Go to Profile Import"),  contexts: ["idle", "all"] },
            { id: "navigate:shotReview",     label: TranslationManager.translate("customaction.navigate.shotReview", "Go to Shot Review"),        contexts: ["idle"] },
            { id: "command:sleep",           label: TranslationManager.translate("customaction.command.sleep", "Sleep"),                           contexts: ["idle"] },
            { id: "command:startEspresso",   label: TranslationManager.translate("customaction.command.startEspresso", "Start Espresso"),         contexts: ["idle"] },
            { id: "command:startSteam",      label: TranslationManager.translate("customaction.command.startSteam", "Start Steam"),               contexts: ["idle"] },
            { id: "command:startHotWater",   label: TranslationManager.translate("customaction.command.startHotWater", "Start Hot Water"),        contexts: ["idle"] },
            { id: "command:startFlush",      label: TranslationManager.translate("customaction.command.startFlush", "Start Flush"),               contexts: ["idle"] },
            { id: "command:idle",            label: TranslationManager.translate("customaction.command.idle", "Stop (Idle)"),                     contexts: ["idle", "espresso", "steam", "hotwater", "flush"] },
            { id: "command:tare",            label: TranslationManager.translate("customaction.command.tare", "Tare Scale"),                      contexts: ["idle", "espresso", "all"] },
            { id: "command:scanScale",       label: TranslationManager.translate("customaction.command.scanScale", "Scan for Devices"),           contexts: ["idle", "all"] },
            { id: "command:brewSettings",    label: TranslationManager.translate("customaction.command.brewSettings", "Open Brew Settings"),      contexts: ["idle"] },
            { id: "command:tempToggleSteam", label: TranslationManager.translate("customaction.command.tempToggleSteam", "Toggle Steam (temporary)"), contexts: ["idle"] },
            { id: "command:toggleCharging",  label: TranslationManager.translate("customaction.command.toggleCharging", "Toggle Charging Mode"),  contexts: ["idle", "all"] },
            { id: "command:uploadVisualizer", label: TranslationManager.translate("customaction.command.uploadVisualizer", "Upload to Visualizer"), contexts: ["idle"] },
            { id: "command:disconnectDE1",   label: TranslationManager.translate("customaction.command.disconnectDE1", "Disconnect DE1"),         contexts: ["idle"] },
            { id: "command:loadProfile",     label: TranslationManager.translate("customaction.command.loadProfile", "Load Profile") + "...",     contexts: ["idle"] },
            { id: "command:previousProfile", label: TranslationManager.translate("customaction.command.previousProfile", "Previous Profile"),   contexts: ["idle"] },
            { id: "command:quit",            label: TranslationManager.translate("customaction.command.quit", "Quit App"),                        contexts: ["idle"] }
        ]
        var ctx = popup.pageContext
        return allActions.filter(function(a) {
            return a.contexts.indexOf(ctx) >= 0 || a.contexts.indexOf("all") >= 0
        })
    }
}
