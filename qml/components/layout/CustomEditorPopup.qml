import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1
import ".."

Popup {
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
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
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
        result = result.replace(/%TIME%/g, Qt.formatTime(now, WeatherManager.use12HourTime ? "h:mmap" : "hh:mm"))
        result = result.replace(/%DATE%/g, Qt.formatDate(now, "yyyy-MM-dd"))
        return result
    }

    // Helper to get action label
    function getActionLabel(actionId) {
        if (!actionId) return "None"
        var actions = getFilteredActions()
        for (var i = 0; i < actions.length; i++) {
            if (actions[i].id === actionId) return actions[i].label
        }
        return actionId
    }

    contentItem: Item {
        Flickable {
            id: editorFlickable
            anchors.fill: parent
            anchors.bottomMargin: buttonRow.height + Theme.scaled(4)
            contentWidth: width
            contentHeight: mainColumn.implicitHeight
            clip: true
            flickableDirection: Flickable.VerticalFlick
            boundsBehavior: Flickable.StopAtBounds

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
                    text: "Icon"
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
                    Accessible.name: popup.showEmojiPicker ? "Hide Picker" : "Pick Icon"
                    Accessible.focusable: true
                    Accessible.onPressAction: pickMa.clicked(null)

                    Text {
                        id: pickText
                        anchors.centerIn: parent
                        text: popup.showEmojiPicker ? "Hide Picker" : "Pick Icon"
                        color: pickMa.pressed ? "white" : Theme.primaryColor
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
                    Accessible.name: "Clear icon"
                    Accessible.focusable: true
                    Accessible.onPressAction: clearEmojiMa.clicked(null)

                    Text {
                        id: clearEmojiText
                        anchors.centerIn: parent
                        text: "Clear"
                        color: clearEmojiMa.pressed ? "white" : Theme.errorColor
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
                        text: "Full"
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
                        text: "Bar"
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
                        text: "Format"
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
                            Accessible.name: "Bold" + (formatter.bold ? ", on" : ", off")
                            Accessible.focusable: true
                            Accessible.onPressAction: boldMa.clicked(null)
                            Text {
                                anchors.centerIn: parent; text: "B"
                                color: formatter.bold ? "white" : Theme.textColor; font.bold: true
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
                            Accessible.name: "Italic" + (formatter.italic ? ", on" : ", off")
                            Accessible.focusable: true
                            Accessible.onPressAction: italicMa.clicked(null)
                            Text {
                                anchors.centerIn: parent; text: "I"
                                color: formatter.italic ? "white" : Theme.textColor; font.italic: true
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
                                Accessible.name: "Font size " + modelData.label + (isActive ? ", selected" : "")
                                Accessible.focusable: true
                                Accessible.onPressAction: sizeMa.clicked(null)
                                Text {
                                    anchors.centerIn: parent; text: modelData.label
                                    color: parent.isActive ? "white" : Theme.textColor; font: Theme.captionFont
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
                                Accessible.name: "Align " + modelData.align + (popup.textAlign === modelData.align ? ", selected" : "")
                                Accessible.focusable: true
                                Accessible.onPressAction: alignMa.clicked(null)
                                Text {
                                    anchors.centerIn: parent; text: modelData.label
                                    color: popup.textAlign === modelData.align ? "white" : Theme.textColor
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
                            text: "Color"
                            color: Theme.textSecondaryColor
                            font: Theme.captionFont
                        }

                        Rectangle {
                            width: Theme.scaled(26); height: Theme.scaled(26)
                            radius: Theme.scaled(13)
                            color: formatter.currentColor || "#ffffff"
                            border.color: Theme.borderColor; border.width: 1
                            Accessible.role: Accessible.Button
                            Accessible.name: "Text color"
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
                            Accessible.name: "Clear text color"
                            Accessible.focusable: true
                            Accessible.onPressAction: clearFgMa.clicked(null)
                            Text {
                                anchors.centerIn: parent
                                text: "\u00D7"
                                color: clearFgMa.pressed ? "white" : Theme.errorColor
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
                            text: "Bg"
                            color: Theme.textSecondaryColor
                            font: Theme.captionFont
                        }

                        Rectangle {
                            width: Theme.scaled(26); height: Theme.scaled(26)
                            radius: Theme.scaled(13)
                            color: popup.textBackgroundColor || Theme.backgroundColor
                            border.color: popup.textBackgroundColor ? "white" : Theme.borderColor
                            border.width: popup.textBackgroundColor ? 2 : 1
                            Accessible.role: Accessible.Button
                            Accessible.name: "Background color"
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
                            Accessible.name: "Clear background color"
                            Accessible.focusable: true
                            Accessible.onPressAction: clearBgMa.clicked(null)
                            Text {
                                anchors.centerIn: parent
                                text: "\u00D7"
                                color: clearBgMa.pressed ? "white" : Theme.errorColor
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
                            Accessible.name: "Hide background" + (popup.textHideBackground ? ", on" : ", off")
                            Accessible.focusable: true
                            Accessible.onPressAction: noBgMa.clicked(null)
                            Text {
                                id: noBgText
                                anchors.centerIn: parent
                                text: "No Bg"
                                color: popup.textHideBackground ? "white" : Theme.textSecondaryColor
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

                // --- CENTER COLUMN: Variables (scrollable) ---
                ColumnLayout {
                    Layout.preferredWidth: Theme.scaled(85)
                    Layout.maximumWidth: Theme.scaled(100)
                    Layout.fillHeight: true
                    spacing: Theme.scaled(2)

                    Text {
                        text: "Variables"
                        color: Theme.textSecondaryColor
                        font: Theme.captionFont
                    }

                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds

                        model: [
                            { token: "%TEMP%", label: "Temp (°C)" },
                            { token: "%STEAM_TEMP%", label: "Steam (°C)" },
                            { token: "%PRESSURE%", label: "Pressure (bar)" },
                            { token: "%FLOW%", label: "Flow (ml/s)" },
                            { token: "%WATER%", label: "Water (%)" },
                            { token: "%WATER_ML%", label: "Water (ml)" },
                            { token: "%WEIGHT%", label: "Weight (g)" },
                            { token: "%SHOT_TIME%", label: "Shot Time (s)" },
                            { token: "%TARGET_WEIGHT%", label: "Target Wt (g)" },
                            { token: "%VOLUME%", label: "Volume (ml)" },
                            { token: "%PROFILE%", label: "Profile Name" },
                            { token: "%STATE%", label: "Machine State" },
                            { token: "%TARGET_TEMP%", label: "Target Temp" },
                            { token: "%SCALE%", label: "Scale Name" },
                            { token: "%TIME%", label: "Time (HH:MM)" },
                            { token: "%DATE%", label: "Date" },
                            { token: "%RATIO%", label: "Brew Ratio" },
                            { token: "%DOSE%", label: "Dose (g)" },
                            { token: "%GRIND%", label: "Grind Setting" },
                            { token: "%GRINDER%", label: "Grinder Model" },
                            { token: "%MACHINE_READY%", label: "Ready/Not ready" },
                            { token: "%MACHINE_READY_COLOR%", label: "Ready Color" },
                            { token: "%CONNECTED%", label: "Online/Offline" },
                            { token: "%CONNECTED_COLOR%", label: "Status Color" },
                            { token: "%DEVICES%", label: "Devices" },
                            { token: "%MACHINE_CONNECTED%", label: "Machine ✓/✗" },
                            { token: "%SCALE_CONNECTED%", label: "Scale ✓/✗" }
                        ]

                        delegate: Rectangle {
                            width: ListView.view.width
                            height: Theme.scaled(26)
                            radius: Theme.scaled(3)
                            color: varDelegateMa.pressed ? Qt.darker(Theme.backgroundColor, 1.3) : "transparent"

                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: Theme.scaled(4)
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.label
                                color: Theme.primaryColor
                                font: Theme.captionFont
                            }

                            MouseArea {
                                id: varDelegateMa
                                anchors.fill: parent
                                onClicked: popup.insertVariable(modelData.token)
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

                // --- RIGHT COLUMN: Actions (3 gesture selectors) ---
                ColumnLayout {
                    Layout.preferredWidth: Theme.scaled(110)
                    Layout.maximumWidth: Theme.scaled(130)
                    Layout.alignment: Qt.AlignTop
                    spacing: Theme.scaled(4)

                    Text {
                        text: "Actions"
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
                        Accessible.name: "Click action, " + popup.getActionLabel(popup.textAction)
                        Accessible.focusable: true
                        Accessible.onPressAction: clickActionMa.clicked(null)

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.scaled(4)
                            anchors.rightMargin: Theme.scaled(4)

                            Text {
                                text: "Click:"
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
                        Accessible.name: "Long press action, " + popup.getActionLabel(popup.textLongPressAction)
                        Accessible.focusable: true
                        Accessible.onPressAction: longPressActionMa.clicked(null)

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.scaled(4)
                            anchors.rightMargin: Theme.scaled(4)

                            Text {
                                text: "Long:"
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
                        Accessible.name: "Double click action, " + popup.getActionLabel(popup.textDoubleclickAction)
                        Accessible.focusable: true
                        Accessible.onPressAction: dblClickActionMa.clicked(null)

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.scaled(4)
                            anchors.rightMargin: Theme.scaled(4)

                            Text {
                                text: "DblClk:"
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
                Accessible.name: "Cancel"
                Accessible.focusable: true
                Accessible.onPressAction: cancelMa.clicked(null)

                Text {
                    anchors.centerIn: parent
                    text: "Cancel"
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
                Accessible.name: "Save"
                Accessible.focusable: true
                Accessible.onPressAction: saveMa.clicked(null)

                Text {
                    anchors.centerIn: parent
                    text: "Save"
                    color: "white"
                    font: Theme.captionFont
                    Accessible.ignored: true
                }
                MouseArea { id: saveMa; anchors.fill: parent; onClicked: popup.doSave() }
            }
        }
    }

    // === Color picker popup (deferred via Loader) ===
    Popup {
        id: colorPickerPopup
        property string mode: "text"  // "text" or "bg"
        property color initialColor: "#ffffff"
        // Selection is tracked by formatter.savedSelectionStart/End (survives focus loss)

        parent: Overlay.overlay
        modal: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
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
                        showBrightnessSlider: false
                    }

                    ColumnLayout {
                        Layout.alignment: Qt.AlignVCenter
                        spacing: Theme.scaled(8)

                        Rectangle {
                            width: Theme.scaled(48); height: Theme.scaled(48)
                            radius: Theme.scaled(8)
                            color: cpEditorInner.color
                            border.color: "white"; border.width: 2
                            Layout.alignment: Qt.AlignHCenter
                        }

                        Text {
                            text: colorPickerPopup.mode === "text" ? "Text Color" : "Background"
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
                            Accessible.name: "Apply"
                            Accessible.focusable: true
                            Accessible.onPressAction: cpApplyMa.clicked(null)
                            Text { anchors.centerIn: parent; text: "Apply"; color: "white"; font: Theme.captionFont; Accessible.ignored: true }
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
                            Accessible.name: "Close"
                            Accessible.focusable: true
                            Accessible.onPressAction: cpCloseMa.clicked(null)
                            Text { anchors.centerIn: parent; text: "Close"; color: Theme.textColor; font: Theme.captionFont; Accessible.ignored: true }
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
                            { color: "#ffffff", label: "White" },
                            { color: Theme.temperatureColor, label: "Temperature" },
                            { color: Theme.errorColor, label: "Error" },
                            { color: Theme.warningColor, label: "Warning" },
                            { color: Theme.accentColor, label: "Accent" },
                            { color: Theme.successColor, label: "Success" },
                            { color: Theme.pressureColor, label: "Pressure" },
                            { color: Theme.primaryColor, label: "Primary" },
                            { color: Theme.flowColor, label: "Flow" },
                            { color: Theme.weightColor, label: "Weight" },
                            { color: Theme.textSecondaryColor, label: "Secondary" }
                        ]
                        Rectangle {
                            required property var modelData
                            width: Theme.scaled(22); height: Theme.scaled(22)
                            radius: width / 2
                            color: modelData.color
                            border.color: swatchMa.containsMouse ? "white" : Theme.borderColor
                            border.width: swatchMa.containsMouse ? 2 : 1
                            Accessible.role: Accessible.Button
                            Accessible.name: modelData.label + " color swatch"
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

    // === Action picker dialog (modal Dialog for TalkBack focus trapping) ===
    Dialog {
        id: actionPickerPopup
        property string gesture: "click"

        // Snapshot of actions, refreshed each time the dialog opens
        property var actionItems: []

        onAboutToShow: {
            var items = [{ id: "", label: "None" }]
            var filtered = popup.getFilteredActions()
            for (var i = 0; i < filtered.length; i++)
                items.push(filtered[i])
            actionItems = items
        }

        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(parent ? parent.width - Theme.scaled(40) : Theme.scaled(200), Theme.scaled(300))
        height: Math.min(apDialogContent.implicitHeight + Theme.scaled(16), parent ? parent.height - Theme.scaled(80) : Theme.scaled(400))
        padding: 0
        topPadding: 0
        bottomPadding: 0

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.scaled(12)
            border.color: Theme.borderColor
            border.width: 1
        }

        contentItem: Column {
            id: apDialogContent
            spacing: 0
            width: parent ? parent.width : actionPickerPopup.width

            // Header
            Item {
                width: parent.width
                height: Theme.scaled(48)

                Text {
                    anchors.centerIn: parent
                    text: {
                        switch (actionPickerPopup.gesture) {
                            case "click": return "Click Action"
                            case "longpress": return "Long Press Action"
                            case "doubleclick": return "Double Click Action"
                            default: return "Action"
                        }
                    }
                    font.pixelSize: Theme.scaled(18)
                    font.family: Theme.bodyFont.family
                    font.bold: true
                    color: Theme.textColor
                    Accessible.ignored: true
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: Theme.borderColor
                }
            }

            // Scrollable list of actions
            Item {
                width: parent.width
                implicitHeight: apDialogList.implicitHeight
                height: implicitHeight

                ListView {
                    id: apDialogList
                    anchors.fill: parent
                    implicitHeight: Math.min(count * Theme.scaled(48), Theme.scaled(300))
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    model: actionPickerPopup.actionItems

                    delegate: Rectangle {
                        id: apOptionDelegate
                        width: apDialogList.width
                        height: Theme.scaled(48)

                        property bool isSelected: {
                            switch (actionPickerPopup.gesture) {
                                case "click": return popup.textAction === modelData.id
                                case "longpress": return popup.textLongPressAction === modelData.id
                                case "doubleclick": return popup.textDoubleclickAction === modelData.id
                                default: return false
                            }
                        }

                        color: isSelected
                            ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.15)
                            : (apDelegateMa.pressed ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.1) : "transparent")

                        Accessible.role: Accessible.Button
                        Accessible.name: modelData.label + (isSelected ? ". " + TranslationManager.translate("combobox.selected", "Selected") : "")
                        Accessible.focusable: true
                        Accessible.onPressAction: apDelegateMa.clicked(null)

                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.scaled(16)
                            anchors.rightMargin: Theme.scaled(16)
                            spacing: Theme.scaled(8)
                            Accessible.ignored: true

                            Text {
                                text: apOptionDelegate.isSelected ? "\u2713" : ""
                                font.pixelSize: Theme.scaled(16)
                                font.family: Theme.bodyFont.family
                                color: Theme.primaryColor
                                anchors.verticalCenter: parent.verticalCenter
                                width: Theme.scaled(24)
                                horizontalAlignment: Text.AlignHCenter
                                Accessible.ignored: true
                            }

                            Text {
                                text: modelData.label
                                font.pixelSize: Theme.scaled(16)
                                font.family: Theme.bodyFont.family
                                color: Theme.textColor
                                verticalAlignment: Text.AlignVCenter
                                anchors.verticalCenter: parent.verticalCenter
                                elide: Text.ElideRight
                                width: apDialogList.width - Theme.scaled(56)
                                Accessible.ignored: true
                            }
                        }

                        // Bottom separator
                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: 1
                            color: Theme.borderColor
                            opacity: 0.3
                        }

                        MouseArea {
                            id: apDelegateMa
                            anchors.fill: parent
                            onClicked: {
                                switch (actionPickerPopup.gesture) {
                                    case "click": popup.textAction = modelData.id; break
                                    case "longpress": popup.textLongPressAction = modelData.id; break
                                    case "doubleclick": popup.textDoubleclickAction = modelData.id; break
                                }
                                actionPickerPopup.close()
                            }
                        }
                    }
                }
            }

            // Separator
            Rectangle {
                width: parent.width
                height: 1
                color: Theme.borderColor
            }

            // Cancel button
            Rectangle {
                width: parent.width
                height: Theme.scaled(48)
                color: apCancelArea.pressed ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.1) : "transparent"

                Accessible.role: Accessible.Button
                Accessible.name: TranslationManager.translate("combobox.cancel", "Cancel")
                Accessible.focusable: true
                Accessible.onPressAction: apCancelArea.clicked(null)

                Text {
                    anchors.centerIn: parent
                    text: TranslationManager.translate("combobox.cancel", "Cancel")
                    font.pixelSize: Theme.scaled(16)
                    font.family: Theme.bodyFont.family
                    color: Theme.textSecondaryColor
                    Accessible.ignored: true
                }

                MouseArea {
                    id: apCancelArea
                    anchors.fill: parent
                    onClicked: actionPickerPopup.close()
                }
            }
        }
    }

    // Action registry with page context filtering
    function getFilteredActions() {
        var allActions = [
            { id: "navigate:settings",      label: "Go to Settings",       contexts: ["idle", "all"] },
            { id: "navigate:history",        label: "Go to History",        contexts: ["idle", "all"] },
            { id: "navigate:profiles",       label: "Go to Profiles",       contexts: ["idle", "all"] },
            { id: "navigate:profileEditor",  label: "Go to Profile Editor", contexts: ["idle", "all"] },
            { id: "navigate:recipes",        label: "Go to Recipes",        contexts: ["idle", "all"] },
            { id: "navigate:descaling",      label: "Go to Descaling",      contexts: ["idle", "all"] },
            { id: "navigate:ai",             label: "Go to AI Settings",    contexts: ["idle", "all"] },
            { id: "navigate:visualizer",     label: "Go to Visualizer",     contexts: ["idle", "all"] },
            { id: "navigate:autofavorites",  label: "Go to Favorites",      contexts: ["idle", "all"] },
            { id: "command:sleep",           label: "Sleep",                contexts: ["idle"] },
            { id: "command:startEspresso",   label: "Start Espresso",       contexts: ["idle"] },
            { id: "command:startSteam",      label: "Start Steam",          contexts: ["idle"] },
            { id: "command:startHotWater",   label: "Start Hot Water",      contexts: ["idle"] },
            { id: "command:startFlush",      label: "Start Flush",          contexts: ["idle"] },
            { id: "command:idle",            label: "Stop (Idle)",          contexts: ["idle", "espresso", "steam", "hotwater", "flush"] },
            { id: "command:tare",            label: "Tare Scale",           contexts: ["idle", "espresso", "all"] },
            { id: "command:scanDE1",         label: "Scan for DE1",         contexts: ["idle", "all"] },
            { id: "command:scanScale",       label: "Scan for Scale",       contexts: ["idle", "all"] },
            { id: "command:quit",            label: "Quit App",             contexts: ["idle"] }
        ]
        var ctx = popup.pageContext
        return allActions.filter(function(a) {
            return a.contexts.indexOf(ctx) >= 0 || a.contexts.indexOf("all") >= 0
        })
    }
}
