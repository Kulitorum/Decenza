pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as C
import QtQuick.Effects

SharedDecenzaDialog {
    id: root

    required property SharedButtonInteraction buttonInteraction
    property var options: []
    property int currentIndex: -1
    property string currentValue: ""
    property string emptyItemText: ""
    property string emptyStateText: ""
    property string selectedLabel: "Selected"
    property string noneLabel: "None"
    property string cancelLabel: "Cancel"

    signal selected(int index, string value)

    parent: C.Overlay.overlay
    anchors.centerIn: parent
    modal: true
    padding: 0
    topPadding: 0
    bottomPadding: 0
    closePolicy: C.Dialog.CloseOnEscape | C.Dialog.CloseOnPressOutside
    header: null
    width: Math.min(parent ? parent.width - dialogStyle.scaled(40) : dialogStyle.scaled(300),
                    dialogStyle.scaled(400))
    height: Math.min(dialogContent.implicitHeight + dialogStyle.scaled(16),
                     parent ? parent.height - dialogStyle.scaled(80) : dialogStyle.scaled(500))

    property var _snapshot: []
    onAboutToShow: _snapshot = options.slice()
    onOptionsChanged: if (visible) _snapshot = options.slice()
    onOpened: {
        if (dialogList.count > 0) {
            if (currentValue.length > 0) {
                for (let i = 0; i < _snapshot.length; ++i) {
                    if (_snapshot[i] === currentValue) {
                        dialogList.positionViewAtIndex(i, ListView.Center)
                        break
                    }
                }
            } else if (currentIndex >= 0) {
                dialogList.positionViewAtIndex(currentIndex, ListView.Center)
            }
        }
        buttonInteraction.announce(title)
    }

    background: Rectangle {
        color: root.dialogStyle.surfaceColor
        radius: root.dialogStyle.scaled(12)
        border.color: root.dialogStyle.borderColor
        border.width: 1
    }

    contentItem: Column {
        id: dialogContent
        spacing: 0
        width: parent ? parent.width : root.width

        Item {
            width: parent.width
            height: root.dialogStyle.scaled(48)
            Text {
                anchors.centerIn: parent
                text: root.title
                font.pixelSize: root.dialogStyle.scaled(18)
                font.family: root.dialogStyle.bodyFont.family
                font.bold: true
                color: root.dialogStyle.textColor
                Accessible.ignored: true
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: root.dialogStyle.borderColor
            }
        }

        Item {
            width: parent.width
            implicitHeight: dialogList.implicitHeight
            height: implicitHeight
            ListView {
                id: dialogList
                anchors.fill: parent
                implicitHeight: Math.min(count * root.dialogStyle.scaled(48), root.dialogStyle.scaled(300))
                clip: true
                model: root._snapshot
                C.ScrollBar.vertical: C.ScrollBar {
                    policy: dialogList.contentHeight > dialogList.height ? C.ScrollBar.AlwaysOn : C.ScrollBar.AsNeeded
                }
                delegate: Rectangle {
                    id: optionDelegate
                    required property var modelData
                    required property int index
                    width: dialogList.width
                    height: root.dialogStyle.scaled(48)
                    property string rawText: modelData || ""
                    property string displayText: rawText.length > 0 ? rawText : root.emptyItemText
                    property bool isCurrent: root.currentValue.length > 0
                        ? rawText === root.currentValue : index === root.currentIndex
                    color: isCurrent
                        ? Qt.rgba(root.dialogStyle.primaryColor.r, root.dialogStyle.primaryColor.g,
                                  root.dialogStyle.primaryColor.b, 0.15)
                        : (optionArea.pressed
                           ? Qt.rgba(root.dialogStyle.primaryColor.r, root.dialogStyle.primaryColor.g,
                                     root.dialogStyle.primaryColor.b, 0.1) : "transparent")
                    Accessible.ignored: true
                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: root.dialogStyle.scaled(16)
                        anchors.rightMargin: root.dialogStyle.scaled(16)
                        spacing: root.dialogStyle.scaled(8)
                        Accessible.ignored: true
                        Item {
                            anchors.verticalCenter: parent.verticalCenter
                            width: root.dialogStyle.scaled(24)
                            height: root.dialogStyle.scaled(24)
                            visible: optionDelegate.isCurrent
                            Image {
                                anchors.centerIn: parent
                                source: "qrc:/icons/tick.svg"
                                sourceSize.width: root.dialogStyle.scaled(16)
                                sourceSize.height: root.dialogStyle.scaled(16)
                                width: root.dialogStyle.scaled(16)
                                height: root.dialogStyle.scaled(16)
                                layer.enabled: true
                                layer.effect: MultiEffect {
                                    colorization: 1.0
                                    colorizationColor: root.dialogStyle.primaryColor
                                }
                            }
                        }
                        Text {
                            text: optionDelegate.displayText
                            font.pixelSize: root.dialogStyle.scaled(16)
                            font.family: root.dialogStyle.bodyFont.family
                            font.italic: optionDelegate.rawText.length === 0 && optionDelegate.displayText.length > 0
                            color: optionDelegate.rawText.length === 0 && root.emptyItemText.length > 0
                                ? root.dialogStyle.textSecondaryColor : root.dialogStyle.textColor
                            verticalAlignment: Text.AlignVCenter
                            anchors.verticalCenter: parent.verticalCenter
                            elide: Text.ElideRight
                            width: dialogList.width - root.dialogStyle.scaled(56)
                            Accessible.ignored: true
                        }
                    }
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 1
                        color: root.dialogStyle.borderColor
                        opacity: 0.3
                    }
                    SharedAccessibleTapArea {
                        id: optionArea
                        objectName: "sharedSelectionOption-" + optionDelegate.index
                        anchors.fill: parent
                        interaction: root.buttonInteraction
                        accessibleName: (optionDelegate.displayText.length > 0
                            ? optionDelegate.displayText : root.noneLabel)
                            + (optionDelegate.isCurrent ? ". " + root.selectedLabel : "")
                        accessibleItem: optionDelegate
                        onAccessibleClicked: {
                            root.selected(optionDelegate.index, optionDelegate.rawText)
                            root.close()
                        }
                    }
                }
                Text {
                    anchors.centerIn: parent
                    text: root.emptyStateText
                    color: root.dialogStyle.textSecondaryColor
                    font.pixelSize: root.dialogStyle.scaled(14)
                    visible: dialogList.count === 0 && root.emptyStateText.length > 0
                }
            }
            Rectangle {
                anchors.top: dialogList.top
                width: dialogList.width
                height: root.dialogStyle.scaled(24)
                visible: dialogList.contentY > 0
                gradient: Gradient {
                    GradientStop { position: 0; color: root.dialogStyle.surfaceColor }
                    GradientStop { position: 1; color: "transparent" }
                }
            }
            Rectangle {
                anchors.bottom: dialogList.bottom
                width: dialogList.width
                height: root.dialogStyle.scaled(24)
                visible: dialogList.contentHeight > dialogList.height
                         && dialogList.contentY < dialogList.contentHeight - dialogList.height - 1
                gradient: Gradient {
                    GradientStop { position: 0; color: "transparent" }
                    GradientStop { position: 1; color: root.dialogStyle.surfaceColor }
                }
            }
        }
        Rectangle { width: parent.width; height: 1; color: root.dialogStyle.borderColor }
        Rectangle {
            width: parent.width
            height: root.dialogStyle.scaled(48)
            color: cancelArea.pressed
                ? Qt.rgba(root.dialogStyle.primaryColor.r, root.dialogStyle.primaryColor.g,
                          root.dialogStyle.primaryColor.b, 0.1) : "transparent"
            Accessible.ignored: true
            Text {
                anchors.centerIn: parent
                text: root.cancelLabel
                font.pixelSize: root.dialogStyle.scaled(16)
                font.family: root.dialogStyle.bodyFont.family
                color: root.dialogStyle.textSecondaryColor
                Accessible.ignored: true
            }
            SharedAccessibleTapArea {
                id: cancelArea
                objectName: "sharedSelectionCancel"
                anchors.fill: parent
                interaction: root.buttonInteraction
                accessibleName: root.cancelLabel
                accessibleItem: parent
                onAccessibleClicked: root.close()
            }
        }
    }
}
