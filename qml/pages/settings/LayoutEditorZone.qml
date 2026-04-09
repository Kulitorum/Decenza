import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Decenza
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
    property real zoneScale: 1.0

    signal itemTapped(string itemId)
    signal zoneTapped()
    signal itemRemoved(string itemId)
    signal moveLeft(string itemId)
    signal moveRight(string itemId)
    signal addItemRequested(string type)
    signal moveUp()
    signal moveDown()
    signal scaleUp()
    signal scaleDown()
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
                Accessible.name: TranslationManager.translate("layoutEditor.zoneHeading", "%1 zone").arg(root.zoneLabel)
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
                Accessible.name: TranslationManager.translate("layoutEditor.moveZoneUp", "Move %1 up").arg(root.zoneLabel)
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
                Accessible.name: TranslationManager.translate("layoutEditor.moveZoneDown", "Move %1 down").arg(root.zoneLabel)
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

            // Scale separator
            Rectangle {
                visible: root.showPositionControls
                width: 1
                height: Theme.scaled(20)
                color: Theme.borderColor
            }

            // Scale display
            Text {
                visible: root.showPositionControls && root.zoneScale !== 1.0
                text: "\u00D7" + root.zoneScale.toFixed(2)
                color: Theme.textSecondaryColor
                font: Theme.captionFont
            }

            // Scale DOWN (smaller)
            Rectangle {
                visible: root.showPositionControls
                width: Theme.scaled(32)
                height: Theme.scaled(32)
                radius: Theme.scaled(6)
                color: scaleDownMa.pressed ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.2) : "transparent"
                border.color: Theme.borderColor
                border.width: 1

                Accessible.role: Accessible.Button
                Accessible.name: TranslationManager.translate("layoutEditor.makeZoneSmaller", "Make %1 smaller").arg(root.zoneLabel)
                Accessible.focusable: true
                Accessible.onPressAction: root.scaleDown()

                Text {
                    anchors.centerIn: parent
                    text: "\u2212"
                    color: Theme.primaryColor
                    font.pixelSize: Theme.scaled(18)
                    font.bold: true
                }

                MouseArea {
                    id: scaleDownMa
                    anchors.fill: parent
                    onClicked: root.scaleDown()
                }
            }

            // Scale UP (bigger)
            Rectangle {
                visible: root.showPositionControls
                width: Theme.scaled(32)
                height: Theme.scaled(32)
                radius: Theme.scaled(6)
                color: scaleUpMa.pressed ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.2) : "transparent"
                border.color: Theme.borderColor
                border.width: 1

                Accessible.role: Accessible.Button
                Accessible.name: TranslationManager.translate("layoutEditor.makeZoneBigger", "Make %1 bigger").arg(root.zoneLabel)
                Accessible.focusable: true
                Accessible.onPressAction: root.scaleUp()

                Text {
                    anchors.centerIn: parent
                    text: "+"
                    color: Theme.primaryColor
                    font.pixelSize: Theme.scaled(18)
                    font.bold: true
                }

                MouseArea {
                    id: scaleUpMa
                    anchors.fill: parent
                    onClicked: root.scaleUp()
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
                        var suffix = isSelected ? ", " + TranslationManager.translate("layoutEditor.selected", "selected") : ""
                        return TranslationManager.translate("layoutEditor.widgetItem", "%1 widget").arg(name) + suffix
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
                            color: Theme.primaryContrastColor
                            font.pixelSize: Theme.scaled(24)

                            Accessible.role: Accessible.Button
                            Accessible.name: TranslationManager.translate("layoutEditor.moveLeft", "Move left")
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
                                ? Theme.primaryContrastColor
                                : ((modelData.type === "spacer" || modelData.type === "separator" || modelData.type === "weather") ? "orange"
                                : ((modelData.type.startsWith("screensaver") || modelData.type === "lastShot") ? "#64B5F6" : Theme.textColor))
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
                                color: modelData.id === root.selectedItemId ? Theme.primaryContrastColor : "orange"
                                font: Theme.captionFont
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }

                        // Move right arrow (only visible when selected)
                        Text {
                            visible: itemChip.isSelected && index < root.items.length - 1
                            text: "\u25B6"
                            color: Theme.primaryContrastColor
                            font.pixelSize: Theme.scaled(24)

                            Accessible.role: Accessible.Button
                            Accessible.name: TranslationManager.translate("layoutEditor.moveRight", "Move right")
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
                            Accessible.name: TranslationManager.translate("layoutEditor.removeWidget", "Remove widget")
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
                            if (modelData.type === "custom" || modelData.type.startsWith("screensaver") || modelData.type === "lastShot" || modelData.type === "shotPlan")
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
                Accessible.name: TranslationManager.translate("layoutEditor.addWidgetTo", "Add widget to %1").arg(root.zoneLabel)
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

                Dialog {
                    id: addPopup
                    modal: true
                    parent: Overlay.overlay
                    anchors.centerIn: parent
                    padding: Theme.scaled(4)
                    closePolicy: Dialog.CloseOnPressOutside | Dialog.CloseOnEscape

                    background: Rectangle {
                        color: Theme.surfaceColor
                        radius: Theme.cardRadius
                        border.color: Theme.borderColor
                        border.width: 1
                    }

                    contentItem: ListView {
                        id: addListView
                        implicitWidth: Theme.scaled(200)
                        implicitHeight: Math.min(contentHeight, Theme.scaled(400))
                        boundsBehavior: Flickable.StopAtBounds
                        clip: true

                        // Grouped by color (white, orange, blue), sorted by name within each group
                        model: [
                            // Actions & readouts (white)
                            { type: "beans", label: TranslationManager.translate("layoutEditor.widgetBeans", "Beans") },
                            { type: "connectionStatus", label: TranslationManager.translate("layoutEditor.widgetConnection", "Connection") },
                            { type: "espresso", label: TranslationManager.translate("layoutEditor.widgetEspresso", "Espresso") },
                            { type: "autofavorites", label: TranslationManager.translate("layoutEditor.widgetFavorites", "Favorites") },
                            { type: "flush", label: TranslationManager.translate("layoutEditor.widgetFlush", "Flush") },
                            { type: "history", label: TranslationManager.translate("layoutEditor.widgetHistory", "History") },
                            { type: "hotwater", label: TranslationManager.translate("layoutEditor.widgetHotWater", "Hot Water") },
                            { type: "machineStatus", label: TranslationManager.translate("layoutEditor.widgetMachineStatus", "Machine Status") },
                            { type: "scaleWeight", label: TranslationManager.translate("layoutEditor.widgetScaleWeight", "Scale Weight") },
                            { type: "settings", label: TranslationManager.translate("layoutEditor.widgetSettings", "Settings") },
                            { type: "shotPlan", label: TranslationManager.translate("layoutEditor.widgetShotPlan", "Shot Plan") },
                            { type: "sleep", label: TranslationManager.translate("layoutEditor.widgetSleep", "Sleep") },
                            { type: "steam", label: TranslationManager.translate("layoutEditor.widgetSteam", "Steam") },
                            { type: "steamTemperature", label: TranslationManager.translate("layoutEditor.widgetSteamTemp", "Steam Temp") },
                            { type: "temperature", label: TranslationManager.translate("layoutEditor.widgetTemperature", "Temperature") },
                            { type: "batteryLevel", label: TranslationManager.translate("layoutEditor.widgetBatteryLevel", "Battery Level") },
                            { type: "scaleBattery", label: TranslationManager.translate("layoutEditor.widgetScaleBattery", "Scale Battery") },
                            { type: "ghcSimulator", label: TranslationManager.translate("layoutEditor.widgetGHCSimulator", "Mini GHC") },
                            { type: "discuss", label: TranslationManager.translate("layoutEditor.widgetDiscuss", "Discuss") },
                            { type: "waterLevel", label: TranslationManager.translate("layoutEditor.widgetWaterLevel", "Water Level") },
                            // Utility (orange)
                            { type: "custom", label: TranslationManager.translate("layoutEditor.widgetCustom", "Custom") },
                            { type: "pageTitle", label: TranslationManager.translate("layoutEditor.widgetPageTitle", "Page Title") },
                            { type: "quit", label: TranslationManager.translate("layoutEditor.widgetQuit", "Quit") },
                            { type: "separator", label: TranslationManager.translate("layoutEditor.widgetSeparator", "Separator") },
                            { type: "spacer", label: TranslationManager.translate("layoutEditor.widgetSpacer", "Spacer") },
                            { type: "weather", label: TranslationManager.translate("layoutEditor.widgetWeather", "Weather") },
                            // Screensavers & widgets (blue)
                            { type: "screensaverPipes", label: TranslationManager.translate("layoutEditor.widget3DPipes", "3D Pipes") },
                            { type: "screensaverAttractor", label: TranslationManager.translate("layoutEditor.widgetAttractors", "Attractors") },
                            { type: "screensaverFlipClock", label: TranslationManager.translate("layoutEditor.widgetFlipClock", "Flip Clock") },
                            { type: "lastShot", label: TranslationManager.translate("layoutEditor.widgetLastShot", "Last Shot") },
                            { type: "screensaverShotMap", label: TranslationManager.translate("layoutEditor.widgetShotMap", "Shot Map") }
                        ]

                        delegate: Rectangle {
                            width: addListView.width
                            height: Theme.scaled(36)
                            color: delegateMa.containsMouse ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.12) : "transparent"
                            radius: Theme.scaled(4)

                            Accessible.role: Accessible.MenuItem
                            Accessible.name: TranslationManager.translate("layoutEditor.addWidget", "Add %1").arg(modelData.label)
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
                                color: (modelData.type.startsWith("screensaver") || modelData.type === "lastShot") ? "#64B5F6"
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
            "espresso": TranslationManager.translate("layoutEditor.chipEspresso", "Espresso"),
            "steam": TranslationManager.translate("layoutEditor.chipSteam", "Steam"),
            "hotwater": TranslationManager.translate("layoutEditor.chipHotWater", "Hot Water"),
            "flush": TranslationManager.translate("layoutEditor.chipFlush", "Flush"),
            "beans": TranslationManager.translate("layoutEditor.chipBeans", "Beans"),
            "history": TranslationManager.translate("layoutEditor.chipHistory", "History"),
            "autofavorites": TranslationManager.translate("layoutEditor.chipFavorites", "Favorites"),
            "sleep": TranslationManager.translate("layoutEditor.chipSleep", "Sleep"),
            "settings": TranslationManager.translate("layoutEditor.chipSettings", "Settings"),
            "temperature": TranslationManager.translate("layoutEditor.chipTemp", "Temp"),
            "steamTemperature": TranslationManager.translate("layoutEditor.chipSteamTemp", "Steam Temp"),
            "batteryLevel": TranslationManager.translate("layoutEditor.chipBattery", "Battery"),
            "waterLevel": TranslationManager.translate("layoutEditor.chipWater", "Water"),
            "connectionStatus": TranslationManager.translate("layoutEditor.chipConnection", "Connection"),
            "machineStatus": TranslationManager.translate("layoutEditor.chipStatus", "Status"),
            "scaleWeight": TranslationManager.translate("layoutEditor.chipScale", "Scale"),
            "scaleBattery": TranslationManager.translate("layoutEditor.chipScaleBat", "Scale Bat"),
            "ghcSimulator": TranslationManager.translate("layoutEditor.chipGHCSim", "Mini GHC"),
            "shotPlan": TranslationManager.translate("layoutEditor.chipShotPlan", "Shot Plan"),
            "pageTitle": TranslationManager.translate("layoutEditor.chipPageTitle", "Page Title"),
            "spacer": TranslationManager.translate("layoutEditor.chipSpacer", "Spacer"),
            "separator": TranslationManager.translate("layoutEditor.chipSep", "Sep"),
            "custom": TranslationManager.translate("layoutEditor.chipCustom", "Custom"),
            "weather": TranslationManager.translate("layoutEditor.chipWeather", "Weather"),
            "lastShot": TranslationManager.translate("layoutEditor.chipLastShot", "Last Shot"),
            "screensaverFlipClock": TranslationManager.translate("layoutEditor.chipClock", "Clock"),
            "screensaverPipes": TranslationManager.translate("layoutEditor.chipPipes", "Pipes"),
            "screensaverAttractor": TranslationManager.translate("layoutEditor.chipAttractor", "Attractor"),
            "screensaverShotMap": TranslationManager.translate("layoutEditor.chipMap", "Map"),
            "quit": TranslationManager.translate("layoutEditor.chipQuit", "Quit"),
            "discuss": TranslationManager.translate("layoutEditor.chipDiscuss", "Discuss")
        }
        return names[type] || type
    }

}
