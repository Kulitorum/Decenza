import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1

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
    property alias textField: inlineTextArea

    // Signal emitted when editing finishes (inline blur or dialog Save)
    signal editingFinished()

    color: Theme.surfaceColor
    radius: Theme.cardRadius
    clip: true

    Layout.fillWidth: true
    Layout.preferredHeight: inlineHeight

    // URL detection: convert plain text to StyledText with clickable links
    function formatTextWithLinks(plainText) {
        if (!plainText) return ""
        var escaped = plainText.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
        escaped = escaped.replace(/\n/g, "<br>")
        var urlRegex = /https?:\/\/[^\s<>&"')\]]+/g
        return escaped.replace(urlRegex, function(url) {
            return '<a href="' + url + '" style="color:' + Theme.primaryColor + '">' + url + '</a>'
        })
    }

    // Display mode: read-only Text with clickable URLs (visible when not focused and has text)
    Text {
        id: displayText
        anchors.fill: parent
        anchors.margins: Theme.scaled(6)
        leftPadding: Theme.scaled(8)
        rightPadding: Theme.scaled(24) // room for expand button
        topPadding: Theme.scaled(4)
        bottomPadding: Theme.scaled(4)
        text: formatTextWithLinks(root.text)
        textFormat: Text.StyledText
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
            rightPadding: Theme.scaled(24) // room for expand button
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

            onEditingFinished: {
                root.editingFinished()
            }

            onActiveFocusChanged: {
                if (!activeFocus) {
                    root.editingFinished()
                }
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
        visible: root.text.length > 0 || !root.readOnly
        z: 10

        Accessible.role: Accessible.Button
        Accessible.name: "Expand editor"
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
        }

        MouseArea {
            id: expandArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                dialogTextArea.text = root.text
                expandDialog.open()
                dialogTextArea.forceActiveFocus()
                if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
                    AccessibilityManager.announce(root.accessibleName + " expanded editor")
                }
            }
        }
    }

    // Expanded editor dialog
    Dialog {
        id: expandDialog
        anchors.centerIn: Overlay.overlay
        width: Math.min(Theme.windowWidth * 0.85, Theme.scaled(600))
        height: Math.min(Theme.windowHeight * 0.75, Theme.scaled(500))
        modal: true
        padding: 0
        closePolicy: Dialog.CloseOnEscape

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.width: 1
            border.color: "white"
        }

        contentItem: ColumnLayout {
            spacing: 0

            // Header
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(44)

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.scaled(16)
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.accessibleName
                    font: Theme.subtitleFont
                    color: Theme.textColor
                    Accessible.ignored: true
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
                }
            }

            // Separator
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Theme.borderColor
            }

            // Footer buttons
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.scaled(52)
                Layout.leftMargin: Theme.scaled(12)
                Layout.rightMargin: Theme.scaled(12)
                spacing: Theme.scaled(8)

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
