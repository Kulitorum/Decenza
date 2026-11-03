pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

DecenzaDialog {
    id: root
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(Theme.scaled(520), parent ? parent.width * 0.95 : Theme.scaled(520))
    height: Math.min(contentCol.implicitHeight + Theme.spacingLarge * 2, parent ? parent.height * 0.92 : Theme.scaled(640))
    modal: true
    closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside
    padding: 0

    property string titleText: ""
    property string subtitle: ""
    property string editSubtitle: ""
    property var presets: []
    property double compareRatio: 0
    readonly property double currentRatio: compareRatio
    property double minimumRatio: 0.1
    property double maximumRatio: 100
    property bool allowPresetEditing: false
    property bool editMode: false
    property bool pending: false
    property string errorMessage: ""
    property Component helpContent: null
    property bool allowCustom: false
    property double customRatio: 2.5
    property string ratioPrefix: "1:"
    property string ratioSuffix: ""
    property bool closeOnPick: true

    signal ratioPicked(double ratio)
    signal presetRatioEdited(int index, double ratio)

    function ratioText(value) {
        return root.ratioPrefix + Number(value).toFixed(1) + root.ratioSuffix
    }
    function setPresetRatio(index, value) {
        if (!root.pending)
            root.presetRatioEdited(index, Math.max(root.minimumRatio,
                Math.min(root.maximumRatio, Math.round(value * 10) / 10)))
    }
    function applyRatio(value) {
        root.ratioPicked(value)
        if (root.closeOnPick) root.close()
    }

    background: Rectangle {
        color: Theme.dialogBackgroundColor
        radius: Theme.cardRadius
        border.width: 1
        border.color: Theme.borderColor
    }

    contentItem: Flickable {
        contentWidth: width
        contentHeight: contentCol.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        ColumnLayout {
            id: contentCol
            width: parent.width
            spacing: Theme.spacingSmall

            Text {
                visible: root.errorMessage.length > 0
                Layout.fillWidth: true
                Layout.leftMargin: Theme.spacingLarge
                Layout.rightMargin: Theme.spacingLarge
                text: root.errorMessage
                textFormat: Text.PlainText
                color: Theme.errorColor
                wrapMode: Text.WordWrap
                font: Theme.captionFont
            }

            // --- Header ---
            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacingMedium
                Layout.leftMargin: Theme.spacingLarge
                Layout.rightMargin: Theme.spacingLarge
                spacing: Theme.scaled(2)
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSmall
                    Text {
                        text: root.titleText
                        color: Theme.textColor
                        font: Theme.subtitleFont
                    }
                    Item { Layout.fillWidth: true }
                    // Edit toggle: reveals a per-card stepper to tune each preset's ratio.
                    Rectangle {
                        visible: root.allowPresetEditing
                        Layout.preferredHeight: Theme.scaled(34)
                        Layout.preferredWidth: editLabel.implicitWidth + Theme.spacingMedium * 2
                        radius: Theme.buttonRadius
                        color: editMa.pressed ? Qt.darker(Theme.backgroundColor, 1.1)
                             : (root.editMode ? Theme.primaryColor : "transparent")
                        border.width: 1
                        border.color: root.editMode ? Theme.primaryColor : Theme.borderColor
                        Accessible.role: Accessible.Button
                        Accessible.name: root.editMode
                            ? TranslationManager.translate("ratio.edit.done", "Done editing ratios")
                            : TranslationManager.translate("ratio.edit.button.accessible", "Edit ratios")
                        Accessible.focusable: true
                        Accessible.onPressAction: editMa.clicked(null)
                        activeFocusOnTab: true
                        Keys.onReturnPressed: editMa.clicked(null)
                        Keys.onSpacePressed: editMa.clicked(null)
                        Text {
                            id: editLabel
                            anchors.centerIn: parent
                            text: root.editMode
                                ? TranslationManager.translate("common.button.done", "Done")
                                : TranslationManager.translate("ratio.edit.button", "Edit")
                            color: root.editMode ? Theme.primaryContrastColor : Theme.primaryColor
                            font: Theme.labelFont
                        }
                        MouseArea { id: editMa; anchors.fill: parent; enabled: !root.pending; onClicked: root.editMode = !root.editMode }
                    }
                }
                Text {
                    text: root.editMode ? root.editSubtitle : root.subtitle
                    color: Theme.textSecondaryColor
                    font: Theme.labelFont
                }
            }

            // --- Ratio style cards ---
            Repeater {
                model: root.presets
                delegate: Rectangle {
                    id: card
                    required property var modelData
                    readonly property bool isCurrent: Math.abs(root.currentRatio - modelData.ratio) < 0.05
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.spacingLarge
                    Layout.rightMargin: Theme.spacingLarge
                    Layout.preferredHeight: cardCol.implicitHeight + Theme.spacingSmall * 2
                    radius: Theme.buttonRadius
                    color: cardMa.pressed ? Qt.darker(Theme.backgroundColor, 1.1) : Theme.backgroundColor
                    border.width: isCurrent ? 2 : 1
                    border.color: isCurrent ? Theme.primaryColor : Theme.borderColor

                    Accessible.role: root.editMode ? Accessible.Grouping : Accessible.Button
                    activeFocusOnTab: !root.editMode
                    Keys.onReturnPressed: if (!root.editMode) root.applyRatio(card.modelData.ratio)
                    Keys.onSpacePressed: if (!root.editMode) root.applyRatio(card.modelData.ratio)
                    Accessible.name: TranslationManager.translate(modelData.key + ".name", modelData.name)
                                     + " " + root.ratioText(modelData.ratio)
                                     + (isCurrent ? ", " + TranslationManager.translate("ratio.current", "current") : "")
                    // In edit mode the card is not an apply button — the steppers are the actionable
                    // controls. Gate the accessibility activation the same way cardMa.enabled gates
                    // pointer taps (a directly-emitted clicked() ignores enabled), and don't announce
                    // the card as a button so screen-reader focus lands on the steppers instead.
                    Accessible.focusable: !root.editMode
                    Accessible.onPressAction: if (!root.editMode) cardMa.clicked(null)

                    ColumnLayout {
                        id: cardCol
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: Theme.spacingMedium
                        anchors.rightMargin: Theme.spacingMedium
                        spacing: Theme.scaled(2)

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingSmall
                            Text {
                                text: TranslationManager.translate(card.modelData.key + ".name", card.modelData.name)
                                color: Theme.textColor
                                font: Theme.subtitleFont
                            }
                            Text {
                                text: root.ratioText(card.modelData.ratio)
                                color: Theme.primaryColor
                                font: Theme.subtitleFont
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                visible: card.isCurrent
                                text: TranslationManager.translate("ratio.current", "current").toUpperCase()
                                color: Theme.primaryColor
                                font: Theme.captionFont
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: TranslationManager.translate(card.modelData.key + ".desc", card.modelData.desc)
                            color: Theme.textSecondaryColor
                            font: Theme.captionFont
                            wrapMode: Text.WordWrap
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.spacingSmall
                            visible: root.editMode
                            spacing: Theme.spacingMedium

                            Rectangle {
                                Layout.preferredWidth: Theme.scaled(46)
                                Layout.preferredHeight: Theme.scaled(46)
                                radius: Theme.buttonRadius
                                color: minusMa.pressed ? Qt.darker(Theme.primaryColor, 1.15) : Theme.primaryColor
                                Accessible.role: Accessible.Button
                                Accessible.name: TranslationManager.translate("ratio.edit.decrease", "Decrease %1 ratio")
                                                 .arg(TranslationManager.translate(card.modelData.key + ".name", card.modelData.name))
                                Accessible.focusable: true
                                Accessible.onPressAction: minusMa.clicked(null)
                        activeFocusOnTab: true
                        Keys.onReturnPressed: minusMa.clicked(null)
                        Keys.onSpacePressed: minusMa.clicked(null)
                                Text { anchors.centerIn: parent; text: "—"; color: Theme.primaryContrastColor
                                       font: Theme.subtitleFont }
                                MouseArea { id: minusMa; anchors.fill: parent; enabled: !root.pending
                                    onClicked: root.setPresetRatio(card.modelData.idx, card.modelData.ratio - (card.modelData.ratio > 10 ? 1 : 0.1)) }
                            }
                            Text {
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignHCenter
                                text: root.ratioText(card.modelData.ratio)
                                color: Theme.textColor
                                font: Theme.subtitleFont
                            }
                            Rectangle {
                                Layout.preferredWidth: Theme.scaled(46)
                                Layout.preferredHeight: Theme.scaled(46)
                                radius: Theme.buttonRadius
                                color: plusMa.pressed ? Qt.darker(Theme.primaryColor, 1.15) : Theme.primaryColor
                                Accessible.role: Accessible.Button
                                Accessible.name: TranslationManager.translate("ratio.edit.increase", "Increase %1 ratio")
                                                 .arg(TranslationManager.translate(card.modelData.key + ".name", card.modelData.name))
                                Accessible.focusable: true
                                Accessible.onPressAction: plusMa.clicked(null)
                        activeFocusOnTab: true
                        Keys.onReturnPressed: plusMa.clicked(null)
                        Keys.onSpacePressed: plusMa.clicked(null)
                                Text { anchors.centerIn: parent; text: "+"; color: Theme.primaryContrastColor
                                       font: Theme.subtitleFont }
                                MouseArea { id: plusMa; anchors.fill: parent; enabled: !root.pending
                                    onClicked: root.setPresetRatio(card.modelData.idx, card.modelData.ratio + (card.modelData.ratio >= 10 ? 1 : 0.1)) }
                            }
                        }
                    }

                    MouseArea {
                        id: cardMa
                        anchors.fill: parent
                        enabled: !root.editMode
                        onClicked: root.applyRatio(card.modelData.ratio)
                    }
                }
            }

            ColumnLayout {
                visible: root.allowCustom
                Layout.fillWidth: true
                Layout.margins: Theme.spacingLarge
                Label {
                    text: TranslationManager.translate("water.ratio.custom", "Custom ratio")
                    color: Theme.textColor
                    font: Theme.bodyFont
                }
                ValueInput {
                    Layout.fillWidth: true
                    value: root.customRatio
                    from: root.minimumRatio
                    to: root.maximumRatio
                    stepSize: 0.1
                    fineStepSize: 0.1
                    decimals: 1
                    suffix: root.ratioSuffix
                    accessibleName: TranslationManager.translate("water.ratio.custom", "Custom ratio")
                    onValueModified: function(value) { root.customRatio = value }
                }
                AccessibleButton {
                    text: TranslationManager.translate("common.button.use", "Use")
                    accessibleName: TranslationManager.translate("water.ratio.useCustom", "Use custom water ratio")
                    onClicked: root.applyRatio(root.customRatio)
                }
            }
            Loader {
                Layout.fillWidth: true
                sourceComponent: root.helpContent
            }

            // --- Close ---
            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.spacingLarge
                Layout.rightMargin: Theme.spacingLarge
                Layout.bottomMargin: Theme.spacingMedium
                Layout.preferredHeight: Theme.scaled(44)
                radius: Theme.buttonRadius
                color: closeMa.pressed ? Qt.darker(Theme.primaryColor, 1.15) : Theme.primaryColor
                Accessible.role: Accessible.Button
                Accessible.name: TranslationManager.translate("common.button.close", "Close")
                Accessible.focusable: true
                Accessible.onPressAction: closeMa.clicked(null)
                        activeFocusOnTab: true
                        Keys.onReturnPressed: closeMa.clicked(null)
                        Keys.onSpacePressed: closeMa.clicked(null)
                Text {
                    anchors.centerIn: parent
                    text: TranslationManager.translate("common.button.close", "Close")
                    color: Theme.primaryContrastColor
                    font: Theme.bodyFont
                }
                MouseArea { id: closeMa; anchors.fill: parent; onClicked: root.close() }
            }
        }
    }
}
