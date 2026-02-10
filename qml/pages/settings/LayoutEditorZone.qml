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
    property bool zoneSelected: false
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
    signal editCustomRequested(string itemId, string zoneName)

    Layout.fillWidth: true
    implicitHeight: zoneContent.implicitHeight + Theme.scaled(20)
    color: Theme.surfaceColor
    radius: Theme.cardRadius
    border.color: zoneSelected ? Theme.primaryColor : Theme.borderColor
    border.width: zoneSelected ? 2 : 1

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
                Accessible.role: Accessible.Heading
                Accessible.name: root.zoneLabel + " zone"
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

                Accessible.role: Accessible.Button
                Accessible.name: "Move " + root.zoneLabel + " up"
                Accessible.focusable: true
                Accessible.onPressAction: root.moveUp()

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

                Accessible.role: Accessible.Button
                Accessible.name: "Move " + root.zoneLabel + " down"
                Accessible.focusable: true
                Accessible.onPressAction: root.moveDown()

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
                    border.color: modelData.type === "custom" && modelData.id !== root.selectedItemId ? "orange" : Theme.borderColor
                    border.width: modelData.type === "custom" && modelData.id !== root.selectedItemId ? 2 : 1

                    property bool isSelected: modelData.id === root.selectedItemId

                    Accessible.role: Accessible.Button
                    Accessible.name: {
                        var name = modelData.type === "custom"
                            ? root.getTextChipLabel(modelData)
                            : getItemDisplayName(modelData.type)
                        return name + " widget" + (isSelected ? ", selected" : "")
                    }
                    Accessible.focusable: true
                    Accessible.onPressAction: root.itemTapped(modelData.id)

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

                            Accessible.role: Accessible.Button
                            Accessible.name: "Move left"
                            Accessible.focusable: true
                            Accessible.onPressAction: root.moveLeft(modelData.id)

                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -Theme.scaled(4)
                                onClicked: root.moveLeft(modelData.id)
                            }
                        }

                        // Standard label for non-text items
                        Text {
                            visible: modelData.type !== "custom"
                            text: getItemDisplayName(modelData.type)
                            color: modelData.id === root.selectedItemId
                                ? "white"
                                : ((modelData.type === "spacer" || modelData.type === "separator" || modelData.type === "weather") ? "orange"
                                : (modelData.type.startsWith("screensaver") ? "#64B5F6" : Theme.textColor))
                            font: Theme.bodyFont
                        }

                        // Mini preview for text items
                        Row {
                            visible: modelData.type === "custom"
                            spacing: Theme.scaled(3)
                            Layout.alignment: Qt.AlignVCenter

                            Image {
                                visible: (modelData.emoji || "") !== ""
                                source: visible ? Theme.emojiToImage(modelData.emoji || "") : ""
                                sourceSize.width: Theme.scaled(18)
                                sourceSize.height: Theme.scaled(18)
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Text {
                                text: root.getTextChipLabel(modelData)
                                color: modelData.id === root.selectedItemId ? "white" : "orange"
                                font: Theme.captionFont
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }

                        // Move right arrow (only visible when selected)
                        Text {
                            visible: itemChip.isSelected && index < root.items.length - 1
                            text: "\u25B6"
                            color: "white"
                            font.pixelSize: Theme.scaled(24)

                            Accessible.role: Accessible.Button
                            Accessible.name: "Move right"
                            Accessible.focusable: true
                            Accessible.onPressAction: root.moveRight(modelData.id)

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

                            Accessible.role: Accessible.Button
                            Accessible.name: "Remove widget"
                            Accessible.focusable: true
                            Accessible.onPressAction: root.itemRemoved(modelData.id)

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
                            if (modelData.type === "custom" || modelData.type.startsWith("screensaver"))
                                root.editCustomRequested(modelData.id, root.zoneName)
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

                Accessible.role: Accessible.Button
                Accessible.name: "Add widget to " + root.zoneLabel
                Accessible.focusable: true
                Accessible.onPressAction: addPopup.open()

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
                            { type: "custom", label: "Custom" },
                            { type: "weather", label: "Weather" },
                            { type: "screensaverFlipClock", label: "Flip Clock" },
                            { type: "screensaverPipes", label: "3D Pipes" },
                            { type: "screensaverAttractor", label: "Attractors" },
                            { type: "screensaverShotMap", label: "Shot Map" },
                            { type: "quit", label: "Quit" }
                        ]

                        delegate: Rectangle {
                            width: addListView.width
                            height: Theme.scaled(36)
                            color: delegateMa.containsMouse ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.12) : "transparent"
                            radius: Theme.scaled(4)

                            Accessible.role: Accessible.MenuItem
                            Accessible.name: "Add " + modelData.label
                            Accessible.focusable: true
                            Accessible.onPressAction: {
                                root.addItemRequested(modelData.type)
                                addPopup.close()
                            }

                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: Theme.scaled(12)
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.label
                                color: modelData.type.startsWith("screensaver") ? "#64B5F6"
                                    : (modelData.type === "spacer" || modelData.type === "separator" || modelData.type === "custom" || modelData.type === "weather") ? "orange" : Theme.textColor
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

    // Build a short label for a text item chip from its content/action
    function getTextChipLabel(item) {
        var content = item.content || ""

        // Strip HTML tags to get plain text
        var plain = content.replace(/<[^>]*>/g, "").trim()

        // Replace variables with short readable labels
        var varLabels = {
            "%TEMP%": "92\u00B0", "%STEAM_TEMP%": "155\u00B0",
            "%PRESSURE%": "9bar", "%FLOW%": "2.1ml",
            "%WATER%": "78%", "%WATER_ML%": "850ml",
            "%WEIGHT%": "36g", "%SHOT_TIME%": "28s",
            "%TARGET_WEIGHT%": "36g", "%VOLUME%": "42ml",
            "%PROFILE%": "Profile", "%STATE%": "Idle",
            "%TARGET_TEMP%": "93\u00B0", "%SCALE%": "Scale",
            "%TIME%": "14:30", "%DATE%": "2025-01",
            "%RATIO%": "2.0", "%DOSE%": "18g",
            "%CONNECTED%": "Online", "%CONNECTED_COLOR%": "",
            "%DEVICES%": "Devices"
        }
        for (var token in varLabels) {
            if (plain.indexOf(token) >= 0)
                plain = plain.replace(new RegExp(token.replace(/%/g, "\\%"), "g"), varLabels[token])
        }

        if (plain && plain !== "Text" && plain !== "Custom") {
            return plain.length > 14 ? plain.substring(0, 12) + ".." : plain
        }

        // Fall back to action target if content is just "Text"
        var action = item.action || ""
        if (action) {
            var actionLabels = {
                "navigate:settings": "Settings", "navigate:history": "History",
                "navigate:profiles": "Profiles", "navigate:autofavorites": "Favorites",
                "navigate:visualizer": "Visualizer", "navigate:recipes": "Recipes",
                "command:sleep": "Sleep", "command:quit": "Quit",
                "command:startEspresso": "Espresso", "command:startSteam": "Steam",
                "command:startHotWater": "Hot Water", "command:startFlush": "Flush",
                "command:tare": "Tare", "command:idle": "Stop"
            }
            if (actionLabels[action]) return actionLabels[action]
        }

        return "Custom"
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
            "spacer": "Spacer", "separator": "Sep", "custom": "Custom",
            "weather": "Weather",
            "screensaverFlipClock": "Clock",
            "screensaverPipes": "Pipes",
            "screensaverAttractor": "Attractor",
            "screensaverShotMap": "Map",
            "quit": "Quit"
        }
        return names[type] || type
    }

}
