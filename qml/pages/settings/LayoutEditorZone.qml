import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import DecenzaDE1
import "../../components"

Rectangle {
    id: root

    property string zoneName: ""
    property string zoneLabel: ""
    property var items: []
    property string selectedItemId: ""
    property bool showPositionControls: false
    property int yOffset: 0

    signal itemTapped(string itemId)
    signal zoneTapped()
    signal itemRemoved(string itemId)
    signal moveLeft(string itemId)
    signal moveRight(string itemId)
    signal addItemRequested(string type)
    signal moveUp()
    signal moveDown()
    signal editTextRequested(string itemId, string zoneName)

    Layout.fillWidth: true
    implicitHeight: zoneContent.implicitHeight + Theme.scaled(20)
    color: Theme.surfaceColor
    radius: Theme.cardRadius
    border.color: Theme.borderColor
    border.width: 1

    ColumnLayout {
        id: zoneContent
        anchors.fill: parent
        anchors.margins: Theme.scaled(10)
        spacing: Theme.spacingSmall

        // Zone label with optional position controls
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.scaled(8)

            Text {
                text: root.zoneLabel
                color: Theme.textSecondaryColor
                font: Theme.labelFont
            }

            Item { Layout.fillWidth: true }

            // Position offset display
            Text {
                visible: root.showPositionControls && root.yOffset !== 0
                text: (root.yOffset > 0 ? "+" : "") + root.yOffset
                color: Theme.textSecondaryColor
                font: Theme.captionFont
            }

            // UP arrow
            Rectangle {
                visible: root.showPositionControls
                width: Theme.scaled(32)
                height: Theme.scaled(32)
                radius: Theme.scaled(6)
                color: upMa.pressed ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.2) : "transparent"
                border.color: Theme.borderColor
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: "\u25B2"
                    color: Theme.primaryColor
                    font.pixelSize: Theme.scaled(16)
                }

                MouseArea {
                    id: upMa
                    anchors.fill: parent
                    onClicked: root.moveUp()
                }
            }

            // DOWN arrow
            Rectangle {
                visible: root.showPositionControls
                width: Theme.scaled(32)
                height: Theme.scaled(32)
                radius: Theme.scaled(6)
                color: downMa.pressed ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.2) : "transparent"
                border.color: Theme.borderColor
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: "\u25BC"
                    color: Theme.primaryColor
                    font.pixelSize: Theme.scaled(16)
                }

                MouseArea {
                    id: downMa
                    anchors.fill: parent
                    onClicked: root.moveDown()
                }
            }
        }

        // Items in zone
        Flow {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall

            Repeater {
                model: root.items
                delegate: Rectangle {
                    id: itemChip
                    width: chipRow.implicitWidth + Theme.scaled(16)
                    height: Theme.scaled(36)
                    radius: Theme.scaled(8)
                    color: modelData.id === root.selectedItemId ? Theme.primaryColor : Theme.backgroundColor
                    border.color: Theme.borderColor
                    border.width: 1

                    property bool isSelected: modelData.id === root.selectedItemId

                    RowLayout {
                        id: chipRow
                        anchors.centerIn: parent
                        spacing: Theme.scaled(4)

                        // Move left arrow (only visible when selected)
                        Text {
                            visible: itemChip.isSelected && index > 0
                            text: "\u25C0"
                            color: "white"
                            font.pixelSize: Theme.scaled(24)
                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -Theme.scaled(4)
                                onClicked: root.moveLeft(modelData.id)
                            }
                        }

                        Text {
                            text: getItemDisplayName(modelData.type)
                            color: modelData.id === root.selectedItemId
                                ? "white"
                                : ((modelData.type === "spacer" || modelData.type === "separator" || modelData.type === "text" || modelData.type === "weather") ? "orange" : Theme.textColor)
                            font: Theme.bodyFont
                        }

                        // Move right arrow (only visible when selected)
                        Text {
                            visible: itemChip.isSelected && index < root.items.length - 1
                            text: "\u25B6"
                            color: "white"
                            font.pixelSize: Theme.scaled(24)
                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -Theme.scaled(4)
                                onClicked: root.moveRight(modelData.id)
                            }
                        }

                        // Remove button (only visible when selected)
                        Text {
                            visible: itemChip.isSelected
                            text: "\u00D7"
                            color: Theme.errorColor
                            font.pixelSize: Theme.scaled(20)
                            font.bold: true
                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -Theme.scaled(4)
                                onClicked: root.itemRemoved(modelData.id)
                            }
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        z: -1
                        onClicked: root.itemTapped(modelData.id)
                        onPressAndHold: {
                            if (modelData.type === "text")
                                root.editTextRequested(modelData.id, root.zoneName)
                        }
                    }
                }
            }

            // Add widget button
            Rectangle {
                id: addButton
                width: Theme.scaled(36)
                height: Theme.scaled(36)
                radius: Theme.scaled(8)
                color: "transparent"
                border.color: Theme.primaryColor
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: "+"
                    color: Theme.primaryColor
                    font.family: Theme.bodyFont.family
                    font.pixelSize: Theme.bodyFont.pixelSize
                    font.bold: true
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: addPopup.open()
                }

                Popup {
                    id: addPopup
                    padding: Theme.scaled(4)
                    closePolicy: Popup.CloseOnPressOutside | Popup.CloseOnEscape

                    // Computed once on open, not live bindings
                    property bool opensAbove: false
                    property real maxListHeight: Theme.scaled(300)

                    onAboutToShow: {
                        var win = addButton.Window.window
                        if (!win) return
                        var globalY = addButton.mapToItem(null, 0, 0).y
                        var spaceBelow = win.height - globalY - addButton.height - Theme.spacingSmall
                        var spaceAbove = globalY - Theme.spacingSmall
                        opensAbove = spaceAbove > spaceBelow
                        var space = opensAbove ? spaceAbove : spaceBelow
                        maxListHeight = Math.max(Theme.scaled(100), space - 2 * padding)
                    }

                    y: opensAbove
                        ? -addListView.height - 2 * padding - Theme.spacingSmall
                        : addButton.height + Theme.spacingSmall

                    x: {
                        var win = addButton.Window.window
                        var popupW = addListView.implicitWidth + 2 * padding
                        if (win) {
                            var globalX = addButton.mapToItem(null, 0, 0).x
                            var candidateX = 0
                            if (globalX + popupW > win.width)
                                candidateX = addButton.width - popupW
                            if (globalX + candidateX < 0)
                                candidateX = -globalX
                            return candidateX
                        }
                        return 0
                    }

                    background: Rectangle {
                        color: Theme.surfaceColor
                        radius: Theme.cardRadius
                        border.color: Theme.borderColor
                        border.width: 1
                    }

                    contentItem: ListView {
                        id: addListView
                        implicitWidth: Theme.scaled(160)
                        implicitHeight: Math.min(contentHeight, addPopup.maxListHeight)
                        boundsBehavior: Flickable.StopAtBounds
                        clip: true

                        model: [
                            { type: "espresso", label: "Espresso" },
                            { type: "steam", label: "Steam" },
                            { type: "hotwater", label: "Hot Water" },
                            { type: "flush", label: "Flush" },
                            { type: "beans", label: "Beans" },
                            { type: "history", label: "History" },
                            { type: "autofavorites", label: "Favorites" },
                            { type: "sleep", label: "Sleep" },
                            { type: "settings", label: "Settings" },
                            { type: "temperature", label: "Temperature" },
                            { type: "steamTemperature", label: "Steam Temp" },
                            { type: "waterLevel", label: "Water Level" },
                            { type: "connectionStatus", label: "Connection" },
                            { type: "scaleWeight", label: "Scale Weight" },
                            { type: "shotPlan", label: "Shot Plan" },
                            { type: "pageTitle", label: "Page Title" },
                            { type: "spacer", label: "Spacer" },
                            { type: "separator", label: "Separator" },
                            { type: "text", label: "Text" },
                            { type: "weather", label: "Weather" }
                        ]

                        delegate: Rectangle {
                            width: addListView.width
                            height: Theme.scaled(36)
                            color: delegateMa.containsMouse ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.12) : "transparent"
                            radius: Theme.scaled(4)

                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: Theme.scaled(12)
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.label
                                color: (modelData.type === "spacer" || modelData.type === "separator" || modelData.type === "text" || modelData.type === "weather") ? "orange" : Theme.textColor
                                font: Theme.bodyFont
                            }

                            MouseArea {
                                id: delegateMa
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    root.addItemRequested(modelData.type)
                                    addPopup.close()
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Tap on zone background to select as target or move item here
    MouseArea {
        anchors.fill: parent
        z: -1
        onClicked: root.zoneTapped()
    }

    function getItemDisplayName(type) {
        var names = {
            "espresso": "Espresso", "steam": "Steam", "hotwater": "Hot Water",
            "flush": "Flush", "beans": "Beans", "history": "History",
            "autofavorites": "Favorites", "sleep": "Sleep", "settings": "Settings",
            "temperature": "Temp", "steamTemperature": "Steam Temp",
            "waterLevel": "Water",
            "connectionStatus": "Connection", "scaleWeight": "Scale",
            "shotPlan": "Shot Plan", "pageTitle": "Page Title",
            "spacer": "Spacer", "separator": "Sep", "text": "Text",
            "weather": "Weather"
        }
        return names[type] || type
    }
}
