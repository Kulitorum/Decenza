import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import DecenzaDE1
import "../layout" as LayoutComponents
import "../layout/items" as LayoutItems

Rectangle {
    id: card

    property var entryData: ({})
    property int displayMode: 0  // 0=full preview, 1=compact list
    property bool isSelected: false
    property bool showBadge: true
    property bool livePreview: false  // When true, always render live (for thumbnail capture)

    width: parent ? parent.width : 100
    visible: !(displayMode === 1 && entryType !== "item" && entryType !== "theme")

    height: {
        if (displayMode === 1) {
            return Theme.bottomBarHeight
        }
        // Full preview mode
        if (entryType === "item") {
            return Theme.scaled(120)
        }
        if (entryType === "zone") {
            if (isBarZone) {
                // Bar zones use natural implicit width, just need bar height
                return Theme.bottomBarHeight + Theme.scaled(8)
            }
            // Center zones: scale by width, derive height from aspect ratio
            var refW = Theme.scaled(800)
            var availW = (width > 0 ? width : 100) - Theme.scaled(8)
            var s = Math.min(1.0, availW / refW)
            return Theme.scaled(120) * s + Theme.scaled(8)
        }
        if (entryType === "layout") {
            return Theme.scaled(160)
        }
        if (entryType === "theme") {
            // 3:2 aspect ratio matching thumbnail (300x200)
            var tw = (width > 0 ? width : 100) - Theme.scaled(8)
            return tw * 2 / 3 + Theme.scaled(8)
        }
        return Theme.scaled(44)
    }
    radius: Theme.cardRadius
    color: isSelected ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.15)
                      : Theme.backgroundColor
    border.color: isSelected ? Theme.primaryColor : Theme.borderColor
    border.width: isSelected ? 2 : 1

    // Server thumbnail URLs (community entries)
    readonly property string thumbnailFullUrl: entryData.thumbnailFullUrl || ""
    readonly property string thumbnailCompactUrl: entryData.thumbnailCompactUrl || ""
    readonly property string serverThumbnailUrl: displayMode === 0 ? thumbnailFullUrl : thumbnailCompactUrl

    // Local thumbnail (captured on save, avoids live rendering)
    property int _thumbVersion: 0
    Connections {
        target: WidgetLibrary
        function onThumbnailSaved(savedId) {
            if (savedId === (entryData.id || "")) _thumbVersion++
        }
    }
    readonly property string localThumbnailSource: {
        void(_thumbVersion)
        if (livePreview) return ""
        var id = entryData.id || ""
        if (id === "") return ""
        if (displayMode === 1 && WidgetLibrary.hasThumbnailCompact(id))
            return "file:///" + WidgetLibrary.thumbnailCompactPath(id) + "?v=" + _thumbVersion
        if (WidgetLibrary.hasThumbnail(id))
            return "file:///" + WidgetLibrary.thumbnailPath(id) + "?v=" + _thumbVersion
        return ""
    }

    // Combined: server > local > none
    readonly property string thumbnailUrl: serverThumbnailUrl !== "" ? serverThumbnailUrl : localThumbnailSource
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

    // Zone name for zone entries
    readonly property string entryZoneName: {
        var d = entryData.data || {}
        return d.zoneName || ""
    }
    // Theme name for theme entries
    readonly property string entryThemeName: {
        var d = entryData.data || {}
        var t = d.theme || {}
        return t.name || ""
    }

    readonly property bool isBarZone: entryZoneName.startsWith("top") ||
                                       entryZoneName.startsWith("bottom") ||
                                       entryZoneName === "statusBar"

    // Layout zone items for layout entries
    readonly property var layoutZones: {
        var d = entryData.data || {}
        var layout = d.layout || {}
        return layout.zones || {}
    }
    readonly property var layoutStatusBarItems: layoutZones.statusBar || []
    readonly property var layoutTopLeftItems: layoutZones.topLeft || []
    readonly property var layoutTopRightItems: layoutZones.topRight || []
    readonly property var layoutCenterStatusItems: layoutZones.centerStatus || []
    readonly property var layoutCenterTopItems: layoutZones.centerTop || []
    readonly property var layoutCenterMiddleItems: layoutZones.centerMiddle || []
    readonly property var layoutBottomLeftItems: layoutZones.bottomLeft || []
    readonly property var layoutBottomRightItems: layoutZones.bottomRight || []

    // Compile action button type to CustomItem modelData (mirrors LayoutItemDelegate)
    function compileActionType(type) {
        switch (type) {
            case "espresso": return {
                emoji: "qrc:/icons/espresso.svg",
                content: "Espresso",
                action: "togglePreset:espresso",
                backgroundColor: String(Theme.primaryColor)
            }
            case "steam": return {
                emoji: "qrc:/icons/steam.svg",
                content: "Steam",
                action: "togglePreset:steam",
                backgroundColor: String(Theme.primaryColor)
            }
            case "hotwater": return {
                emoji: "qrc:/icons/water.svg",
                content: "Hot Water",
                action: "togglePreset:hotwater",
                backgroundColor: String(Theme.primaryColor)
            }
            case "flush": return {
                emoji: "qrc:/icons/flush.svg",
                content: "Flush",
                action: "togglePreset:flush",
                backgroundColor: String(Theme.primaryColor)
            }
            case "beans": return {
                emoji: "qrc:/icons/coffeebeans.svg",
                content: "Beans",
                action: "togglePreset:beans",
                backgroundColor: String(Theme.primaryColor)
            }
            case "history": return {
                emoji: "qrc:/icons/history.svg",
                content: "History",
                action: "navigate:history",
                backgroundColor: String(Theme.primaryColor)
            }
            case "settings": return {
                emoji: "qrc:/icons/settings.svg",
                content: "Settings",
                action: "navigate:settings",
                backgroundColor: String(Theme.primaryColor)
            }
            case "autofavorites": return {
                emoji: "qrc:/icons/star.svg",
                content: "Favorites",
                action: "navigate:autofavorites",
                backgroundColor: String(Theme.primaryColor)
            }
            case "sleep": return {
                emoji: "qrc:/icons/sleep.svg",
                content: "Sleep",
                action: "command:sleep",
                backgroundColor: "#555555"
            }
            case "quit": return {
                emoji: "qrc:/icons/quit.svg",
                content: "Quit",
                action: "command:quit",
                backgroundColor: "#555555"
            }
            default: return null
        }
    }

    // Preview model data - compile action types for CustomItem rendering
    readonly property var previewModelData: {
        var src = entryItemData
        var d = {}
        for (var key in src) d[key] = src[key]
        d.id = d.id || "preview"
        var compiled = compileActionType(d.type || "")
        if (compiled) {
            for (var key2 in compiled) d[key2] = compiled[key2]
        }
        return d
    }

    // Resolve variables with sample/live values
    function resolveContent(text) {
        if (!text) return ""
        var result = text
        var vars = {
            "%TEMP%": "93.2", "%STEAM_TEMP%": "155\u00B0",
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
            case "theme": return Theme.warningColor
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
            visible: card.showBadge
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

        // Thumbnail image (server or local)
        Image {
            visible: hasThumbnail
            anchors.fill: parent
            source: thumbnailUrl
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            sourceSize.width: width * Screen.devicePixelRatio
            sourceSize.height: height * Screen.devicePixelRatio
        }

        // Theme name overlay (bottom of thumbnail)
        Rectangle {
            visible: entryType === "theme" && entryThemeName !== ""
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: themeNameText.implicitHeight + Theme.scaled(6)
            color: Qt.rgba(0, 0, 0, 0.6)
            z: 1

            Text {
                id: themeNameText
                anchors.centerIn: parent
                width: parent.width - Theme.scaled(8)
                text: entryThemeName
                color: "white"
                font.family: Theme.captionFont.family
                font.pixelSize: Theme.scaled(10)
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }
        }

        // Item preview — only instantiated when needed (no thumbnail, or livePreview for capture)
        Loader {
            active: entryType === "item" && (livePreview || !hasThumbnail)
            anchors.fill: parent
            sourceComponent: Component {
                LayoutItems.CustomItem {
                    isCompact: false
                    modelData: card.previewModelData
                }
            }
        }

        // Zone preview — only instantiated when needed
        Loader {
            active: entryType === "zone" && (livePreview || !hasThumbnail)
            anchors.fill: parent
            sourceComponent: Component {
                Item {
                    clip: true
                    LayoutComponents.LayoutCenterZone {
                        visible: !card.isBarZone
                        width: Theme.scaled(800)
                        anchors.centerIn: parent
                        zoneName: card.entryZoneName || "centerTop"
                        items: card.entryZoneItems
                        scale: Math.min(parent.width / Math.max(1, width), 1.0)
                        transformOrigin: Item.Center
                    }
                    LayoutComponents.LayoutBarZone {
                        visible: card.isBarZone
                        anchors.centerIn: parent
                        zoneName: card.entryZoneName
                        items: card.entryZoneItems
                        scale: Math.min(parent.width / Math.max(1, implicitWidth), 1.0)
                        transformOrigin: Item.Center
                    }
                }
            }
        }

        // Layout preview — only instantiated when needed (heaviest component)
        Loader {
            active: entryType === "layout" && (livePreview || !hasThumbnail)
            anchors.fill: parent
            sourceComponent: Component {
                Item {
                    clip: true
                    Item {
                        width: Theme.scaled(800)
                        height: Theme.scaled(480)
                        anchors.centerIn: parent
                        scale: Math.min(parent.width / Math.max(1, width),
                                       parent.height / Math.max(1, height))
                        transformOrigin: Item.Center

                        Rectangle {
                            anchors.fill: parent
                            color: Theme.backgroundColor
                        }

                        Rectangle {
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.right: parent.right
                            height: Theme.statusBarHeight
                            color: Theme.surfaceColor
                            visible: card.layoutStatusBarItems.length > 0

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: Theme.scaled(4)
                                anchors.rightMargin: Theme.scaled(4)
                                spacing: Theme.spacingMedium

                                Repeater {
                                    model: card.layoutStatusBarItems
                                    delegate: LayoutComponents.LayoutItemDelegate {
                                        zoneName: "statusBar"
                                        Layout.fillWidth: modelData.type === "spacer"
                                    }
                                }
                            }
                        }

                        RowLayout {
                            anchors.top: parent.top
                            anchors.topMargin: (card.layoutStatusBarItems.length > 0
                                ? Theme.statusBarHeight : 0) + Theme.scaled(8)
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: Theme.scaled(8)
                            anchors.rightMargin: Theme.scaled(8)
                            spacing: Theme.scaled(20)

                            LayoutComponents.LayoutBarZone {
                                zoneName: "topLeft"
                                items: card.layoutTopLeftItems
                            }
                            Item { Layout.fillWidth: true }
                            LayoutComponents.LayoutBarZone {
                                zoneName: "topRight"
                                items: card.layoutTopRightItems
                            }
                        }

                        ColumnLayout {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.verticalCenterOffset: Theme.scaled(20)
                            anchors.leftMargin: Theme.scaled(8)
                            anchors.rightMargin: Theme.scaled(8)
                            spacing: Theme.scaled(10)

                            LayoutComponents.LayoutCenterZone {
                                Layout.fillWidth: true
                                zoneName: "centerStatus"
                                items: card.layoutCenterStatusItems
                                visible: card.layoutCenterStatusItems.length > 0
                            }
                            LayoutComponents.LayoutCenterZone {
                                Layout.fillWidth: true
                                zoneName: "centerTop"
                                items: card.layoutCenterTopItems
                            }
                            LayoutComponents.LayoutCenterZone {
                                Layout.fillWidth: true
                                zoneName: "centerMiddle"
                                items: card.layoutCenterMiddleItems
                                visible: card.layoutCenterMiddleItems.length > 0
                            }
                        }

                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: Theme.bottomBarHeight
                            color: Theme.surfaceColor

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: Theme.spacingMedium
                                anchors.rightMargin: Theme.spacingMedium
                                spacing: Theme.spacingMedium

                                LayoutComponents.LayoutBarZone {
                                    zoneName: "bottomLeft"
                                    items: card.layoutBottomLeftItems
                                    Layout.fillHeight: true
                                }
                                Item { Layout.fillWidth: true }
                                LayoutComponents.LayoutBarZone {
                                    zoneName: "bottomRight"
                                    items: card.layoutBottomRightItems
                                    Layout.fillHeight: true
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // --- COMPACT LIST MODE (items) - matches CustomItem compact rendering ---
    Item {
        visible: displayMode === 1 && entryType === "item"
        anchors.fill: parent

        // Type badge overlay
        Rectangle {
            visible: card.showBadge
            z: 1
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.leftMargin: Theme.scaled(4)
            anchors.topMargin: Theme.scaled(4)
            width: compactItemBadgeText.implicitWidth + Theme.scaled(6)
            height: Theme.scaled(14)
            radius: Theme.scaled(3)
            color: typeBadgeColor(entryType)
            opacity: 0.8

            Text {
                id: compactItemBadgeText
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
            anchors.margins: Theme.spacingSmall
            source: thumbnailUrl
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            sourceSize.width: width * Screen.devicePixelRatio
            sourceSize.height: height * Screen.devicePixelRatio
        }

        // Item preview — only instantiated when no thumbnail
        Loader {
            active: livePreview || !hasThumbnail
            anchors.fill: parent
            sourceComponent: Component {
                LayoutItems.CustomItem {
                    isCompact: true
                    modelData: card.previewModelData
                }
            }
        }
    }

    // --- COMPACT LIST MODE (zones, layouts, thumbnails) ---
    RowLayout {
        visible: displayMode === 1 && entryType !== "item"
        anchors.fill: parent
        anchors.leftMargin: Theme.scaled(6)
        anchors.rightMargin: Theme.scaled(6)
        spacing: Theme.scaled(4)

        // Type badge
        Rectangle {
            visible: card.showBadge
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

        // Compact thumbnail for themes
        Image {
            visible: entryType === "theme" && hasThumbnail
            Layout.preferredWidth: Theme.scaled(40)
            Layout.preferredHeight: Theme.scaled(27)
            source: hasThumbnail ? thumbnailUrl : ""
            fillMode: Image.PreserveAspectFit
            asynchronous: true
        }

        // Content preview
        Text {
            Layout.fillWidth: true
            text: {
                if (entryType === "theme") return entryThemeName || "Theme"
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
