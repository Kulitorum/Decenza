import QtQuick

Loader {
    id: root

    required property var modelData
    required property string zoneName

    readonly property string itemType: modelData.type || ""
    readonly property string itemId: modelData.id || ""

    // Is this a bar zone (compact rendering)?
    readonly property bool isCompact: zoneName.startsWith("top") || zoneName.startsWith("bottom") || zoneName === "statusBar"

    source: {
        var src = ""
        switch (itemType) {
            case "espresso":         src = "items/EspressoItem.qml"; break
            case "steam":            src = "items/SteamItem.qml"; break
            case "hotwater":         src = "items/HotWaterItem.qml"; break
            case "flush":            src = "items/FlushItem.qml"; break
            case "beans":            src = "items/BeansItem.qml"; break
            case "history":          src = "items/HistoryItem.qml"; break
            case "autofavorites":    src = "items/AutoFavoritesItem.qml"; break
            case "sleep":            src = "items/SleepItem.qml"; break
            case "settings":         src = "items/SettingsItem.qml"; break
            case "temperature":      src = "items/TemperatureItem.qml"; break
            case "waterLevel":       src = "items/WaterLevelItem.qml"; break
            case "connectionStatus": src = "items/ConnectionStatusItem.qml"; break
            case "scaleWeight":      src = "items/ScaleWeightItem.qml"; break
            case "shotPlan":         src = "items/ShotPlanItem.qml"; break
            case "spacer":           src = "items/SpacerItem.qml"; break
            case "text":             src = "items/TextItem.qml"; break
            case "weather":          src = "items/WeatherItem.qml"; break
            case "pageTitle":        src = "items/PageTitleItem.qml"; break
            case "steamTemperature": src = "items/SteamTemperatureItem.qml"; break
            case "separator":        src = "items/SeparatorItem.qml"; break
            default:                 src = ""; break
        }
        console.log("[IdlePage] type:", itemType, "zone:", zoneName, "compact:", isCompact, "source:", src)
        return src
    }

    onLoaded: {
        console.log("[IdlePage] LOADED type:", itemType, "id:", itemId, "item:", item, "implicitSize:", item.implicitWidth, "x", item.implicitHeight)
        item.isCompact = Qt.binding(function() { return root.isCompact })
        item.itemId = root.itemId
        if (typeof item.modelData !== "undefined") {
            item.modelData = Qt.binding(function() { return root.modelData })
        }
        // Bind loaded item to fill the Loader so it gets the correct size
        // from the parent Layout (implicit size flows up, actual size flows down)
        item.anchors.fill = root
        console.log("[IdlePage] after setup - loader size:", root.width, "x", root.height, "item size:", item.width, "x", item.height)
    }

    onStatusChanged: {
        if (status === Loader.Error) {
            console.log("[IdlePage] LOAD ERROR for type:", itemType, "source:", source)
        } else if (status === Loader.Null) {
            console.log("[IdlePage] NULL status for type:", itemType)
        } else if (status === Loader.Loading) {
            console.log("[IdlePage] Loading type:", itemType)
        }
    }
}
