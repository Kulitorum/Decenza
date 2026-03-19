import QtQuick
import QtQuick.Controls
import Decenza

// Themed modal selection dialog with checkmark on current item.
// Usage:
//   SelectionDialog {
//       id: myDialog
//       title: "Pick one"
//       options: ["A", "B", "C"]
//       currentIndex: Settings.myChoice
//       onSelected: function(index) { Settings.myChoice = index }
//   }
Dialog {
    id: root

    property var options: []        // Array of display strings
    property int currentIndex: -1   // Which option is currently selected

    signal selected(int index)

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    padding: 0
    topPadding: 0
    bottomPadding: 0
    closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside
    header: null  // Suppress Dialog's built-in header — we render title in contentItem
    width: Math.min(parent ? parent.width - Theme.scaled(40) : Theme.scaled(300), Theme.scaled(400))
    height: Math.min(dialogContent.implicitHeight + Theme.scaled(16),
                     parent ? parent.height - Theme.scaled(80) : Theme.scaled(500))

    // Snapshot refreshed each time the dialog opens
    property var _snapshot: []

    onAboutToShow: _snapshot = root.options.slice()
    onOpened: {
        if (root.currentIndex >= 0 && dialogList.count > 0)
            dialogList.positionViewAtIndex(root.currentIndex, ListView.Center)
        AccessibilityManager.announce(root.title)
    }

    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.scaled(12)
        border.color: Theme.borderColor
        border.width: 1
    }

    contentItem: Column {
        id: dialogContent
        spacing: 0
        width: parent ? parent.width : root.width

        // Header
        Item {
            width: parent.width
            height: Theme.scaled(48)

            Text {
                anchors.centerIn: parent
                text: root.title
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

        // Scrollable option list
        Item {
            width: parent.width
            implicitHeight: dialogList.implicitHeight
            height: implicitHeight

            ListView {
                id: dialogList
                anchors.fill: parent
                implicitHeight: Math.min(count * Theme.scaled(48), Theme.scaled(300))
                clip: true
                model: root._snapshot

                ScrollBar.vertical: ScrollBar {
                    policy: dialogList.contentHeight > dialogList.height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
                }

                delegate: Rectangle {
                    id: optionDelegate
                    width: dialogList.width
                    height: Theme.scaled(48)

                    property string _text: modelData || ""
                    property bool _isCurrent: index === root.currentIndex

                    color: _isCurrent
                        ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.15)
                        : (optionArea.pressed ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.1) : "transparent")

                    Accessible.role: Accessible.Button
                    Accessible.name: _text + (_isCurrent
                        ? ". " + TranslationManager.translate("combobox.selected", "Selected")
                        : "")
                    Accessible.focusable: true
                    Accessible.onPressAction: optionArea.clicked(null)

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.scaled(16)
                        anchors.rightMargin: Theme.scaled(16)
                        spacing: Theme.scaled(8)
                        Accessible.ignored: true

                        ColoredIcon {
                            source: "qrc:/icons/tick.svg"
                            iconWidth: Theme.scaled(16)
                            iconHeight: Theme.scaled(16)
                            iconColor: Theme.primaryColor
                            anchors.verticalCenter: parent.verticalCenter
                            width: Theme.scaled(24)
                            height: Theme.scaled(24)
                            visible: optionDelegate._isCurrent
                        }

                        Text {
                            text: optionDelegate._text
                            font.pixelSize: Theme.scaled(16)
                            font.family: Theme.bodyFont.family
                            color: Theme.textColor
                            verticalAlignment: Text.AlignVCenter
                            anchors.verticalCenter: parent.verticalCenter
                            elide: Text.ElideRight
                            width: dialogList.width - Theme.scaled(56)
                            Accessible.ignored: true
                        }
                    }

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
                            root.selected(index)
                            root.close()
                        }
                    }
                }
            }

            // Top fade
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

            // Bottom fade
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
                onClicked: root.close()
            }
        }
    }
}
