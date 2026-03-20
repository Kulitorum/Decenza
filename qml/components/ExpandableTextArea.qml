import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import Decenza

Rectangle {
    id: root

    // Public properties
    property string text: ""
    property string placeholderText: ""
    required property string accessibleName
    property int inlineHeight: Theme.scaled(80)
    property font textFont: Theme.labelFont
    property font dialogFont: Theme.bodyFont
    property bool readOnly: false
    property bool fitContent: false   // When true, shrink to fit text (inlineHeight is max)
    property alias textField: inlineTextArea

    // Signal emitted when editing finishes (inline blur or dialog Save)
    signal editingFinished()

    color: Theme.surfaceColor
    radius: Theme.cardRadius
    clip: true

    Layout.fillWidth: true
    Layout.preferredHeight: fitContent && !inlineScrollView.visible
        ? Math.min(inlineHeight, Math.max(Theme.scaled(36), displayText.contentHeight + Theme.scaled(24)))
        : inlineHeight

    // On mobile, all editing goes through the fullscreen overlay dialog —
    // no inline editing (the small inline field + on-screen keyboard is a poor experience).
    property bool isMobile: Qt.platform.os === "android" || Qt.platform.os === "ios"

    function openEditorDialog() {
        dialogTextArea.text = root.text
        expandDialog.open()
        dialogTextArea.forceActiveFocus()
        if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
            AccessibilityManager.announce(TranslationManager.translate("expandableText.accessible.expandedEditor", "%1 expanded editor").arg(root.accessibleName))
        }
    }

    // URL detection: convert plain text to StyledText with clickable links
    function escapeHtml(text) {
        return text.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
    }

    function formatTextWithLinks(plainText) {
        if (!plainText) return ""
        // Run regex on original text before HTML escaping so URLs with & are not truncated
        var urlRegex = /https?:\/\/[^\s<>"']+/g
        var lastIndex = 0
        var result = ""
        var match
        while ((match = urlRegex.exec(plainText)) !== null) {
            result += escapeHtml(plainText.substring(lastIndex, match.index))
            var url = match[0].replace(/[.,;:!?\])}]+$/, '')  // trim trailing punctuation
            result += '<a href="' + url + '" style="color:' + Theme.primaryColor + '">' + escapeHtml(url) + '</a>'
            lastIndex = match.index + url.length
            urlRegex.lastIndex = lastIndex
        }
        result += escapeHtml(plainText.substring(lastIndex))
        return result.replace(/\n/g, "<br>")
    }

    // Display mode: read-only Text with clickable URLs (visible when not focused and has text)
    Text {
        id: displayText
        anchors.fill: parent
        anchors.margins: Theme.scaled(6)
        leftPadding: Theme.scaled(8)
        rightPadding: root.isMobile ? Theme.scaled(8) : Theme.scaled(24) // room for expand button on desktop
        topPadding: Theme.scaled(4)
        bottomPadding: Theme.scaled(4)
        text: Theme.replaceEmojiWithImg(formatTextWithLinks(root.text), root.textFont.pixelSize)
        textFormat: Text.RichText
        font: root.textFont
        color: Theme.textColor
        wrapMode: Text.Wrap
        elide: Text.ElideRight
        clip: true
        visible: !inlineScrollView.visible && root.text.length > 0

        onLinkActivated: function(link) {
            Qt.openUrlExternally(link)
        }

        // Pointer cursor on links (desktop)
        HoverHandler {
            cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor
        }

        // Tap anywhere on text (not a link) to start editing
        TapHandler {
            onTapped: {
                if (!root.readOnly) {
                    if (root.isMobile)
                        root.openEditorDialog()
                    else
                        inlineTextArea.forceActiveFocus()
                }
            }
        }
    }

    // Placeholder when empty and not focused
    Text {
        anchors.fill: parent
        anchors.margins: Theme.scaled(6)
        leftPadding: Theme.scaled(8)
        topPadding: Theme.scaled(4)
        text: root.placeholderText
        font: root.textFont
        color: Theme.textSecondaryColor
        visible: root.text.length === 0 && !inlineScrollView.visible

        TapHandler {
            onTapped: {
                if (!root.readOnly) {
                    if (root.isMobile)
                        root.openEditorDialog()
                    else
                        inlineTextArea.forceActiveFocus()
                }
            }
        }
    }

    // Edit mode: scrollable TextArea (visible when focused)
    ScrollView {
        id: inlineScrollView
        anchors.fill: parent
        anchors.margins: Theme.scaled(6)
        contentWidth: availableWidth
        ScrollBar.vertical.policy: ScrollBar.AsNeeded
        visible: inlineTextArea.activeFocus

        TextArea {
            id: inlineTextArea
            text: root.text
            font: root.textFont
            color: Theme.textColor
            wrapMode: TextArea.Wrap
            readOnly: root.readOnly
            leftPadding: Theme.scaled(8)
            rightPadding: root.isMobile ? Theme.scaled(8) : Theme.scaled(24) // room for expand button on desktop
            topPadding: Theme.scaled(4)
            bottomPadding: Theme.scaled(4)
            background: Rectangle { color: "transparent" }

            Accessible.role: Accessible.EditableText
            Accessible.name: root.accessibleName
            Accessible.description: text
            Accessible.focusable: true

            onTextChanged: {
                if (root.text !== text) {
                    root.text = text
                }
            }

            property bool _redirectingToDialog: false
            onActiveFocusChanged: {
                if (activeFocus && root.isMobile) {
                    // On mobile, redirect to fullscreen dialog
                    _redirectingToDialog = true
                    focus = false
                    root.openEditorDialog()
                    return
                }
                if (!activeFocus && !_redirectingToDialog) {
                    root.editingFinished()
                }
                _redirectingToDialog = false
            }
        }
    }

    // Sync external text changes into the TextArea
    onTextChanged: {
        if (inlineTextArea.text !== text) {
            inlineTextArea.text = text
        }
    }

    // Expand button (top-right corner)
    Rectangle {
        id: expandButton
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: Theme.scaled(4)
        anchors.rightMargin: Theme.scaled(4)
        width: Theme.scaled(28)
        height: Theme.scaled(28)
        radius: Theme.scaled(4)
        color: expandArea.pressed ? Qt.darker(Theme.surfaceColor, 1.3) : Qt.lighter(Theme.surfaceColor, 1.3)
        visible: !root.isMobile && (root.text.length > 0 || !root.readOnly)
        z: 10

        Accessible.role: Accessible.Button
        Accessible.name: root.readOnly
            ? TranslationManager.translate("expandableText.accessible.viewFullText", "View full text")
            : TranslationManager.translate("expandableText.accessible.expandEditor", "Expand editor")
        Accessible.focusable: true
        Accessible.onPressAction: expandArea.clicked(null)

        Image {
            anchors.centerIn: parent
            width: Theme.scaled(16)
            height: Theme.scaled(16)
            source: "qrc:/icons/edit.svg"
            sourceSize: Qt.size(width, height)
            opacity: 0.8
            Accessible.ignored: true

            layer.enabled: true
            layer.smooth: true
            layer.effect: MultiEffect {
                colorization: 1.0
                colorizationColor: Theme.textSecondaryColor
            }
        }

        MouseArea {
            id: expandArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.openEditorDialog()
        }
    }

    // Expanded editor dialog
    Dialog {
        id: expandDialog
        parent: Overlay.overlay
        modal: true
        padding: 0
        closePolicy: Dialog.CloseOnEscape

        // True when the text area has focus (proxy for keyboard visibility)
        property bool keyboardActive: dialogTextArea.activeFocus
        property real keyboardHeight: {
            if (!keyboardActive || !root.isMobile) return 0
            var kbh = Qt.inputMethod.keyboardRectangle.height
            if (kbh > 0) return kbh
            return parent.height * 0.45
        }

        // On mobile: fill entire screen above keyboard (no margins, no centering)
        // On desktop: centered dialog with max width/height
        width: root.isMobile ? parent.width : Math.min(parent.width * 0.85, Theme.scaled(600))
        height: {
            if (root.isMobile) {
                // Android: adjustPan shifts the window to keep the cursor visible,
                // so use full height — don't subtract keyboard height (double-shift).
                if (Qt.platform.os === "android")
                    return parent.height
                // iOS: no adjustPan — shrink dialog to fit above keyboard
                if (keyboardHeight > 0)
                    return parent.height - keyboardHeight
                return parent.height
            }
            // Desktop: centered dialog
            var maxH = Math.min(parent.height * 0.75, Theme.scaled(500))
            if (keyboardActive && keyboardHeight > 0) {
                var available = parent.height - keyboardHeight - Theme.scaled(20)
                return Math.min(maxH, Math.max(Theme.scaled(200), available))
            }
            return maxH
        }
        x: root.isMobile ? 0 : (parent.width - width) / 2
        y: root.isMobile ? 0 : (parent.height - height) / 2

        background: Rectangle {
            color: Theme.surfaceColor
            radius: root.isMobile ? 0 : Theme.cardRadius
            border.width: root.isMobile ? 0 : 1
            border.color: "white"
        }

        contentItem: ColumnLayout {
            spacing: 0

            // Header — on mobile, includes Cancel/Save buttons; on desktop, just title
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(44)

                // Cancel button (mobile header, left side)
                AccessibleButton {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.scaled(8)
                    anchors.verticalCenter: parent.verticalCenter
                    text: TranslationManager.translate("common.cancel", "Cancel")
                    accessibleName: TranslationManager.translate("common.cancel", "Cancel")
                    visible: root.isMobile && !root.readOnly
                    onClicked: expandDialog.close()
                }

                Text {
                    anchors.horizontalCenter: root.isMobile ? parent.horizontalCenter : undefined
                    anchors.left: root.isMobile ? undefined : parent.left
                    anchors.leftMargin: root.isMobile ? 0 : Theme.scaled(16)
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.accessibleName
                    font: Theme.subtitleFont
                    color: Theme.textColor
                    Accessible.ignored: true
                }

                // Done/Close button (mobile header, right side)
                AccessibleButton {
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.scaled(8)
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.readOnly
                        ? TranslationManager.translate("common.close", "Close")
                        : TranslationManager.translate("common.button.done", "Done")
                    accessibleName: root.readOnly
                        ? TranslationManager.translate("common.close", "Close")
                        : TranslationManager.translate("common.button.done", "Done")
                    primary: !root.readOnly
                    visible: root.isMobile
                    onClicked: {
                        if (!root.readOnly) {
                            root.text = dialogTextArea.text
                            root.editingFinished()
                        }
                        expandDialog.close()
                    }
                }

                // Hide keyboard button (desktop only — mobile has header buttons)
                HideKeyboardButton {
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.scaled(8)
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !root.isMobile
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: Theme.borderColor
                }
            }

            // Large text editing area
            ScrollView {
                id: dialogScrollView
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: Theme.scaled(12)
                contentWidth: availableWidth
                ScrollBar.vertical.policy: ScrollBar.AsNeeded

                TextArea {
                    id: dialogTextArea
                    font: root.dialogFont
                    color: Theme.textColor
                    wrapMode: TextArea.Wrap
                    readOnly: root.readOnly
                    leftPadding: Theme.scaled(8)
                    rightPadding: Theme.scaled(8)
                    topPadding: Theme.scaled(8)
                    bottomPadding: Theme.scaled(8)
                    background: Rectangle {
                        color: Theme.backgroundColor
                        radius: Theme.scaled(4)
                    }

                    Accessible.role: Accessible.EditableText
                    Accessible.name: root.accessibleName
                    Accessible.description: text
                    Accessible.focusable: true

                    // Scroll to keep cursor visible when typing or tapping
                    onCursorRectangleChanged: {
                        if (!activeFocus) return
                        var flickable = dialogScrollView.contentItem
                        if (!flickable) return
                        var cursorY = cursorRectangle.y
                        var cursorBottom = cursorY + cursorRectangle.height
                        var margin = Theme.scaled(20)
                        var maxContentY = Math.max(0, flickable.contentHeight - flickable.height)
                        if (cursorY < flickable.contentY + margin) {
                            flickable.contentY = Math.max(0, cursorY - margin)
                        } else if (cursorBottom + margin > flickable.contentY + flickable.height) {
                            flickable.contentY = Math.min(cursorBottom + margin - flickable.height, maxContentY)
                        }
                    }
                }
            }

            // Footer separator + buttons (desktop only — mobile uses header buttons)
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Theme.borderColor
                visible: !root.isMobile
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(52)
                Layout.leftMargin: Theme.scaled(12)
                Layout.rightMargin: Theme.scaled(12)
                spacing: Theme.scaled(8)
                visible: !root.isMobile

                Item { Layout.fillWidth: true }

                AccessibleButton {
                    text: TranslationManager.translate("common.cancel", "Cancel")
                    accessibleName: TranslationManager.translate("common.cancel", "Cancel")
                    visible: !root.readOnly
                    onClicked: expandDialog.close()
                }

                AccessibleButton {
                    text: root.readOnly ? TranslationManager.translate("common.close", "Close") : TranslationManager.translate("common.save", "Save")
                    accessibleName: root.readOnly ? TranslationManager.translate("common.close", "Close") : TranslationManager.translate("common.save", "Save")
                    primary: !root.readOnly
                    onClicked: {
                        if (!root.readOnly) {
                            root.text = dialogTextArea.text
                            root.editingFinished()
                        }
                        expandDialog.close()
                    }
                }
            }
        }
    }
}
