import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DecenzaDE1

Rectangle {
    id: card

    property var entryData: ({})
    property int displayMode: 0  // 0=full preview, 1=compact list
    property bool isSelected: false

    width: parent ? parent.width : 100
    height: displayMode === 0 ? Theme.scaled(44) : Theme.scaled(32)
    radius: Theme.cardRadius
    color: isSelected ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.15)
                      : Theme.backgroundColor
    border.color: isSelected ? Theme.primaryColor : Theme.borderColor
    border.width: isSelected ? 2 : 1

    // Thumbnail URLs from server (community entries)
    readonly property string thumbnailFullUrl: entryData.thumbnailFullUrl || ""
    readonly property string thumbnailCompactUrl: entryData.thumbnailCompactUrl || ""
    readonly property string thumbnailUrl: displayMode === 0 ? thumbnailFullUrl : thumbnailCompactUrl
    readonly property bool hasThumbnail: thumbnailUrl !== ""

    // Entry data helpers
    readonly property string entryType: entryData.type || ""
    readonly property var entryItemData: {
        var d = entryData.data || {}
        return d.item || {}
    }
    readonly property var entryZoneItems: {
        var d = entryData.data || {}
        return d.items || []
    }
    readonly property string itemContent: entryItemData.content || ""
    readonly property string itemEmoji: entryItemData.emoji || ""
    readonly property string itemBgColor: entryItemData.backgroundColor || ""
    readonly property bool itemHasEmoji: itemEmoji !== ""
    readonly property bool itemEmojiIsSvg: itemHasEmoji && itemEmoji.indexOf("qrc:") === 0
    readonly property string itemAction: entryItemData.action || ""
    readonly property bool itemHasAction: itemAction !== "" ||
        (entryItemData.longPressAction || "") !== "" ||
        (entryItemData.doubleclickAction || "") !== ""

    // Resolve variables with sample/live values
    function resolveContent(text) {
        if (!text) return ""
        var result = text.replace(/<[^>]*>/g, "").trim()  // Strip HTML for preview
        var vars = {
            "%TEMP%": "93.2", "%STEAM_TEMP%": "155.0",
            "%PRESSURE%": "9.0", "%FLOW%": "2.1",
            "%WATER%": "78", "%WATER_ML%": "850",
            "%WEIGHT%": "36.2", "%SHOT_TIME%": "28.5",
            "%TARGET_WEIGHT%": "36.0", "%VOLUME%": "42",
            "%PROFILE%": "Profile", "%STATE%": "Idle",
            "%TARGET_TEMP%": "93.0", "%SCALE%": "Scale",
            "%TIME%": Qt.formatTime(new Date(), "hh:mm"),
            "%DATE%": Qt.formatDate(new Date(), "yyyy-MM-dd"),
            "%RATIO%": "2.0", "%DOSE%": "18.0",
            "%CONNECTED%": "Online", "%CONNECTED_COLOR%": "",
            "%DEVICES%": "Machine"
        }
        for (var token in vars) {
            if (result.indexOf(token) >= 0)
                result = result.replace(new RegExp(token.replace(/%/g, "\\%"), "g"), vars[token])
        }
        return result
    }

    function typeBadgeColor(type) {
        switch (type) {
            case "item": return Theme.primaryColor
            case "zone": return Theme.accentColor
            case "layout": return Theme.successColor
            default: return Theme.textSecondaryColor
        }
    }

    function typeBadgeLabel(type) {
        switch (type) {
            case "item": return "ITEM"
            case "zone": return "ZONE"
            case "layout": return "LAYOUT"
            default: return type.toUpperCase()
        }
    }

    // --- FULL PREVIEW MODE ---
    Item {
        id: fullLayout
        visible: displayMode === 0
        anchors.fill: parent
        anchors.margins: Theme.scaled(4)
        implicitHeight: Theme.scaled(36)

        // Type badge (small, top-left corner overlay)
        Rectangle {
            id: typeBadge
            z: 1
            anchors.top: parent.top
            anchors.left: parent.left
            width: badgeText.implicitWidth + Theme.scaled(6)
            height: Theme.scaled(14)
            radius: Theme.scaled(3)
            color: typeBadgeColor(entryType)
            opacity: 0.8

            Text {
                id: badgeText
                anchors.centerIn: parent
                text: typeBadgeLabel(entryType)
                color: "white"
                font.family: Theme.captionFont.family
                font.pixelSize: Theme.scaled(8)
                font.bold: true
            }
        }

        // Server thumbnail (community entries)
        Image {
            visible: hasThumbnail
            anchors.fill: parent
            source: thumbnailUrl
            fillMode: Image.PreserveAspectFit
        }

        // Item preview - rendered like the real widget (local entries)
        Rectangle {
            visible: entryType === "item" && !hasThumbnail
            anchors.fill: parent
            radius: Theme.scaled(6)
            color: itemBgColor || (itemHasAction ? "#555555" : "transparent")

            RowLayout {
                anchors.centerIn: parent
                spacing: Theme.spacingSmall

                Image {
                    visible: itemEmojiIsSvg
                    source: itemEmojiIsSvg ? itemEmoji : ""
                    sourceSize.width: Theme.scaled(22)
                    sourceSize.height: Theme.scaled(22)
                    Layout.alignment: Qt.AlignVCenter
                }

                Text {
                    visible: itemHasEmoji && !itemEmojiIsSvg
                    text: itemEmoji
                    font.family: Theme.emojiFontFamily
                    font.pixelSize: Theme.scaled(18)
                    Layout.alignment: Qt.AlignVCenter
                }

                Text {
                    text: resolveContent(itemContent)
                    color: (itemHasAction || itemHasEmoji || itemBgColor !== "") ? "white" : Theme.textColor
                    font: Theme.bodyFont
                    elide: Text.ElideRight
                    Layout.maximumWidth: card.width - Theme.scaled(50)
                }
            }
        }

        // Zone preview - row of mini item chips (local entries)
        Flow {
            visible: entryType === "zone" && !hasThumbnail
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.scaled(3)
            clip: true

            Repeater {
                model: entryZoneItems

                Rectangle {
                    width: zoneChipRow.implicitWidth + Theme.scaled(8)
                    height: Theme.scaled(22)
                    radius: Theme.scaled(4)
                    color: {
                        var bg = modelData.backgroundColor || ""
                        if (bg) return bg
                        var hasAct = (modelData.action || "") !== ""
                        return hasAct ? "#555555" : Theme.surfaceColor
                    }

                    Row {
                        id: zoneChipRow
                        anchors.centerIn: parent
                        spacing: Theme.scaled(2)

                        Image {
                            visible: (modelData.emoji || "").indexOf("qrc:") === 0
                            source: visible ? (modelData.emoji || "") : ""
                            sourceSize.width: Theme.scaled(14)
                            sourceSize.height: Theme.scaled(14)
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        Text {
                            visible: (modelData.emoji || "") !== "" && (modelData.emoji || "").indexOf("qrc:") !== 0
                            text: modelData.emoji || ""
                            font.family: Theme.emojiFontFamily
                            font.pixelSize: Theme.scaled(12)
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        Text {
                            text: {
                                var t = modelData.type || "custom"
                                if (t !== "custom") return getItemDisplayName(t)
                                var plain = resolveContent(modelData.content || "")
                                return plain.length > 10 ? plain.substring(0, 8) + ".." : (plain || "Custom")
                            }
                            color: "white"
                            font.family: Theme.captionFont.family
                            font.pixelSize: Theme.scaled(10)
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }
            }
        }

        // Layout preview (local entries)
        Text {
            visible: entryType === "layout" && !hasThumbnail
            anchors.centerIn: parent
            text: "Layout"
            color: Theme.textSecondaryColor
            font: Theme.bodyFont
        }
    }

    // --- COMPACT LIST MODE ---
    RowLayout {
        visible: displayMode === 1
        anchors.fill: parent
        anchors.leftMargin: Theme.scaled(6)
        anchors.rightMargin: Theme.scaled(6)
        spacing: Theme.scaled(4)

        // Type badge
        Rectangle {
            width: compactBadgeText.implicitWidth + Theme.scaled(6)
            height: Theme.scaled(14)
            radius: Theme.scaled(3)
            color: typeBadgeColor(entryType)
            opacity: 0.8

            Text {
                id: compactBadgeText
                anchors.centerIn: parent
                text: typeBadgeLabel(entryType)
                color: "white"
                font.family: Theme.captionFont.family
                font.pixelSize: Theme.scaled(8)
                font.bold: true
            }
        }

        // Emoji preview (local entries only)
        Image {
            visible: entryType === "item" && itemEmojiIsSvg && !hasThumbnail
            source: visible ? itemEmoji : ""
            sourceSize.width: Theme.scaled(18)
            sourceSize.height: Theme.scaled(18)
            Layout.alignment: Qt.AlignVCenter
        }

        Text {
            visible: entryType === "item" && itemHasEmoji && !itemEmojiIsSvg && !hasThumbnail
            text: itemEmoji
            font.family: Theme.emojiFontFamily
            font.pixelSize: Theme.scaled(14)
            Layout.alignment: Qt.AlignVCenter
        }

        // Content preview
        Text {
            Layout.fillWidth: true
            text: {
                if (hasThumbnail) return entryData.name || entryType
                if (entryType === "item") return resolveContent(itemContent)
                if (entryType === "zone") return entryZoneItems.length + " items"
                return "Layout"
            }
            color: Theme.textColor
            font: Theme.captionFont
            elide: Text.ElideRight
        }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: card.clicked()
        onDoubleClicked: card.doubleClicked()
    }

    signal clicked()
    signal doubleClicked()

    function getItemDisplayName(type) {
        var names = {
            "espresso": "Espresso", "steam": "Steam", "hotwater": "Hot Water",
            "flush": "Flush", "beans": "Beans", "history": "History",
            "autofavorites": "Favs", "sleep": "Sleep", "settings": "Settings",
            "temperature": "Temp", "steamTemperature": "Steam",
            "waterLevel": "Water", "connectionStatus": "Conn",
            "scaleWeight": "Scale", "shotPlan": "Plan", "pageTitle": "Title",
            "spacer": "---", "separator": "|", "weather": "Weather", "quit": "Quit"
        }
        return names[type] || type
    }
}
