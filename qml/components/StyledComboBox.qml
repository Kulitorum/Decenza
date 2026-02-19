import QtQuick
import QtQuick.Controls
import DecenzaDE1

ComboBox {
    id: control

    implicitWidth: Theme.scaled(100)
    implicitHeight: Theme.scaled(36)

    // Set this to the field label so TalkBack announces the label, not the selected value
    property string accessibleLabel: ""

    // Optional function(index) → string for custom dialog item text.
    // When set, the dialog shows this text instead of textAt(i).
    // Useful for object models where you need richer display (e.g. name + IP).
    property var textFunction: null

    // Text shown in the dialog for empty/blank items (e.g. "(None)" for an empty first option)
    property string emptyItemText: ""

    contentItem: Text {
        text: control.displayText
        font: Theme.bodyFont
        color: control.enabled ? Theme.textColor : Theme.textSecondaryColor
        leftPadding: Theme.scaled(10)
        rightPadding: Theme.scaled(30)
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        Accessible.ignored: true
    }

    indicator: Text {
        x: control.width - width - Theme.scaled(10)
        y: control.height / 2 - height / 2
        text: "\u25BC"
        font.pixelSize: Theme.scaled(10)
        color: control.enabled ? Theme.textSecondaryColor : Qt.darker(Theme.textSecondaryColor, 1.5)
        Accessible.ignored: true
    }

    background: Rectangle {
        implicitHeight: Theme.scaled(36)
        color: control.enabled ? Qt.rgba(255, 255, 255, 0.1) : Qt.rgba(128, 128, 128, 0.1)
        radius: Theme.scaled(6)
        border.color: control.activeFocus ? Theme.primaryColor : "transparent"
        border.width: control.activeFocus ? 1 : 0
    }

    // Suppress the native popup entirely — we use a Dialog instead
    popup: Popup {
        width: 0
        height: 0
        visible: false
    }

    // Delegate is unused (Dialog has its own delegates) but required by ComboBox
    delegate: ItemDelegate {
        width: 0
        height: 0
    }

    Accessible.role: Accessible.ComboBox
    Accessible.name: control.accessibleLabel || control.displayText
    Accessible.focusable: true
    Accessible.onPressAction: selectionDialog.open()

    // Close dialog when ComboBox becomes invisible (page popped, tab switched)
    onVisibleChanged: if (!visible) selectionDialog.close()

    // Keyboard support: open dialog with Space/Enter (native popup is suppressed)
    Keys.onSpacePressed: selectionDialog.open()
    Keys.onReturnPressed: selectionDialog.open()
    Keys.onEnterPressed: selectionDialog.open()

    // Intercept all taps to open our Dialog instead of the native Popup
    MouseArea {
        anchors.fill: parent
        z: 100
        enabled: control.enabled
        cursorShape: Qt.PointingHandCursor
        Accessible.ignored: true

        onClicked: {
            selectionDialog.open()
        }
    }

    // Build a plain JS array of display strings from the ComboBox model.
    // This avoids model-type issues (QVariant wrapping, delegateModel, etc.)
    function _buildItemList() {
        var items = []
        for (var i = 0; i < control.count; i++) {
            items.push(control.textFunction ? control.textFunction(i) : control.textAt(i))
        }
        return items
    }

    // Modal Dialog replaces the Popup — works correctly with TalkBack focus trapping
    Dialog {
        id: selectionDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(parent ? parent.width - Theme.scaled(40) : Theme.scaled(300), Theme.scaled(400))
        height: Math.min(dialogContent.implicitHeight + Theme.scaled(16), parent ? parent.height - Theme.scaled(80) : Theme.scaled(500))
        padding: 0
        topPadding: 0
        bottomPadding: 0

        // Snapshot of items, refreshed each time the dialog opens
        property var itemList: []

        onAboutToShow: {
            itemList = control._buildItemList()
        }

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.scaled(12)
            border.color: Theme.borderColor
            border.width: 1
        }

        onOpened: {
            // Scroll to center the current selection
            if (control.currentIndex >= 0 && dialogList.count > 0) {
                dialogList.positionViewAtIndex(control.currentIndex, ListView.Center)
            }
            if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
                var label = control.accessibleLabel || control.displayText
                AccessibilityManager.announce(label)
            }
        }

        contentItem: Column {
            id: dialogContent
            spacing: 0
            width: parent ? parent.width : selectionDialog.width

            // Header
            Item {
                width: parent.width
                height: Theme.scaled(48)

                Text {
                    anchors.centerIn: parent
                    text: control.accessibleLabel || control.displayText
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

            // Scrollable list of options (wrapped for fade gradients)
            Item {
                width: parent.width
                implicitHeight: dialogList.implicitHeight
                height: implicitHeight

                ListView {
                    id: dialogList
                    anchors.fill: parent
                    implicitHeight: Math.min(count * Theme.scaled(48), Theme.scaled(300))
                    clip: true
                    model: selectionDialog.itemList

                    ScrollBar.vertical: ScrollBar {
                        policy: dialogList.contentHeight > dialogList.height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
                    }

                    delegate: Rectangle {
                        id: optionDelegate
                        width: dialogList.width
                        height: Theme.scaled(48)

                        property string _rawText: modelData || ""
                        property string _text: _rawText.length > 0 ? _rawText : control.emptyItemText
                        property bool _isCurrent: index === control.currentIndex

                        color: _isCurrent
                            ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.15)
                            : (optionArea.pressed ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.1) : "transparent")

                        Accessible.role: Accessible.Button
                        Accessible.name: (_text.length > 0 ? _text : TranslationManager.translate("combobox.empty", "None")) +
                            (_isCurrent ? ". " + TranslationManager.translate("combobox.selected", "Selected") : "")
                        Accessible.focusable: true
                        Accessible.onPressAction: optionArea.clicked(null)

                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.scaled(16)
                            anchors.rightMargin: Theme.scaled(16)
                            spacing: Theme.scaled(8)
                            Accessible.ignored: true

                            Text {
                                text: optionDelegate._isCurrent ? "\u2713" : ""
                                font.pixelSize: Theme.scaled(16)
                                font.family: Theme.bodyFont.family
                                color: Theme.primaryColor
                                anchors.verticalCenter: parent.verticalCenter
                                width: Theme.scaled(24)
                                horizontalAlignment: Text.AlignHCenter
                                Accessible.ignored: true
                            }

                            Text {
                                text: optionDelegate._text
                                font.pixelSize: Theme.scaled(16)
                                font.family: Theme.bodyFont.family
                                font.italic: optionDelegate._rawText.length === 0 && optionDelegate._text.length > 0
                                color: optionDelegate._rawText.length === 0 ? Theme.textSecondaryColor : Theme.textColor
                                verticalAlignment: Text.AlignVCenter
                                anchors.verticalCenter: parent.verticalCenter
                                elide: Text.ElideRight
                                width: dialogList.width - Theme.scaled(56)
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
                            id: optionArea
                            anchors.fill: parent
                            onClicked: {
                                control.currentIndex = index
                                control.activated(index)
                                selectionDialog.close()
                            }
                        }
                    }
                }

                // Top fade: visible when scrolled down
                Rectangle {
                    anchors.top: dialogList.top
                    width: dialogList.width
                    height: Theme.scaled(24)
                    visible: dialogList.contentY > 0
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: Theme.surfaceColor }
                        GradientStop { position: 1.0; color: "transparent" }
                    }
                }

                // Bottom fade: visible when more content below
                Rectangle {
                    anchors.bottom: dialogList.bottom
                    width: dialogList.width
                    height: Theme.scaled(24)
                    visible: dialogList.contentHeight > dialogList.height &&
                             dialogList.contentY < dialogList.contentHeight - dialogList.height - 1
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: "transparent" }
                        GradientStop { position: 1.0; color: Theme.surfaceColor }
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
                color: cancelArea.pressed ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.1) : "transparent"

                Accessible.role: Accessible.Button
                Accessible.name: TranslationManager.translate("combobox.cancel", "Cancel")
                Accessible.focusable: true
                Accessible.onPressAction: cancelArea.clicked(null)

                Text {
                    anchors.centerIn: parent
                    text: TranslationManager.translate("combobox.cancel", "Cancel")
                    font.pixelSize: Theme.scaled(16)
                    font.family: Theme.bodyFont.family
                    color: Theme.textSecondaryColor
                    Accessible.ignored: true
                }

                MouseArea {
                    id: cancelArea
                    anchors.fill: parent
                    onClicked: selectionDialog.close()
                }
            }
        }
    }
}
