import QtQuick
import QtQuick.Layouts
import DecenzaDE1
import "../.."

Item {
    id: root
    property bool isCompact: false
    property string itemId: ""

    implicitWidth: isCompact ? compactContent.implicitWidth : fullContent.implicitWidth
    implicitHeight: isCompact ? compactContent.implicitHeight : fullContent.implicitHeight

    // Map WMO weather icon names to unicode symbols
    function weatherEmoji(iconName) {
        switch (iconName) {
            case "clear":         return "\u2600"   // ☀
            case "partly-cloudy": return "\u26C5"   // ⛅
            case "overcast":      return "\u2601"   // ☁
            case "fog":           return "\uD83C\uDF2B"  // 🌫
            case "drizzle":       return "\uD83C\uDF26"  // 🌦
            case "rain":          return "\uD83C\uDF27"  // 🌧
            case "freezing-rain": return "\u2744"   // ❄ (with rain)
            case "snow":          return "\u2744"   // ❄
            case "showers":       return "\uD83C\uDF26"  // 🌦
            case "snow-showers":  return "\uD83C\uDF28"  // 🌨
            case "thunderstorm":  return "\u26A1"   // ⚡
            default:              return "\u2601"   // ☁
        }
    }

    // Format temperature with degree sign
    function formatTemp(temp) {
        return Math.round(temp) + "\u00B0"
    }

    // Wind direction arrow from degrees
    function windArrow(degrees) {
        var arrows = ["\u2193", "\u2199", "\u2190", "\u2196", "\u2191", "\u2197", "\u2192", "\u2198"]
        var index = Math.round(degrees / 45) % 8
        return arrows[index]
    }

    // --- COMPACT MODE (bar zone - next 4 hours) ---
    Item {
        id: compactContent
        visible: root.isCompact
        anchors.fill: parent
        implicitWidth: compactRow.implicitWidth
        implicitHeight: Theme.bottomBarHeight

        Row {
            id: compactRow
            anchors.centerIn: parent
            spacing: Theme.scaled(8)

            Repeater {
                model: {
                    if (!WeatherManager.valid) return []
                    var forecast = WeatherManager.hourlyForecast
                    return forecast.slice(0, Math.min(4, forecast.length))
                }

                delegate: Column {
                    spacing: Theme.scaled(1)

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: modelData.hour || ""
                        color: Theme.textSecondaryColor
                        font: Theme.captionFont
                    }
                    Image {
                        anchors.horizontalCenter: parent.horizontalCenter
                        source: Theme.emojiToImage(weatherEmoji(modelData.weatherIcon || ""))
                        sourceSize.width: Theme.scaled(16)
                        sourceSize.height: Theme.scaled(16)
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: formatTemp(modelData.temperature || 0)
                        color: Theme.textColor
                        font: Theme.captionFont
                    }
                }
            }

            // Loading / no data fallback
            Text {
                visible: !WeatherManager.valid
                anchors.verticalCenter: parent.verticalCenter
                text: WeatherManager.loading ? "..." : "--"
                color: Theme.textSecondaryColor
                font: Theme.bodyFont
            }
        }
    }

    // --- FULL MODE (center zone - next 24 hours) ---
    Item {
        id: fullContent
        visible: !root.isCompact
        anchors.fill: parent
        implicitWidth: fullColumn.implicitWidth
        implicitHeight: fullColumn.implicitHeight

        ColumnLayout {
            id: fullColumn
            anchors.fill: parent
            spacing: Theme.scaled(4)

            // Header: current conditions
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: Theme.scaled(8)
                visible: WeatherManager.valid

                Image {
                    source: {
                        var forecast = WeatherManager.hourlyForecast
                        if (forecast.length > 0)
                            return Theme.emojiToImage(weatherEmoji(forecast[0].weatherIcon || ""))
                        return ""
                    }
                    sourceSize.width: Theme.scaled(28)
                    sourceSize.height: Theme.scaled(28)
                }

                Text {
                    text: {
                        var forecast = WeatherManager.hourlyForecast
                        if (forecast.length > 0)
                            return formatTemp(forecast[0].temperature || 0)
                        return "--"
                    }
                    color: Theme.textColor
                    font.family: Theme.valueFont.family
                    font.pixelSize: Theme.scaled(28)
                    font.bold: true
                }

                ColumnLayout {
                    spacing: 0

                    Text {
                        text: {
                            var forecast = WeatherManager.hourlyForecast
                            if (forecast.length > 0)
                                return forecast[0].weatherDescription || ""
                            return ""
                        }
                        color: Theme.textSecondaryColor
                        font: Theme.captionFont
                    }

                    Text {
                        text: {
                            var forecast = WeatherManager.hourlyForecast
                            if (forecast.length > 0) {
                                var f = forecast[0]
                                var parts = []
                                if (f.relativeHumidity > 0) parts.push(f.relativeHumidity + "%")
                                if (f.windSpeed > 0) parts.push(windArrow(f.windDirection) + Math.round(f.windSpeed) + "km/h")
                                return parts.join("  ")
                            }
                            return ""
                        }
                        color: Theme.textSecondaryColor
                        font: Theme.captionFont
                    }
                }
            }

            // Hourly forecast scroll
            ListView {
                id: hourlyList
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: Theme.scaled(60)
                orientation: ListView.Horizontal
                spacing: Theme.scaled(2)
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                visible: WeatherManager.valid

                model: WeatherManager.hourlyForecast

                delegate: Column {
                    width: Theme.scaled(38)
                    spacing: Theme.scaled(1)

                    // Hour
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: modelData.hour || ""
                        color: index === 0 ? Theme.primaryColor : Theme.textSecondaryColor
                        font: Theme.captionFont
                    }

                    // Weather icon
                    Image {
                        anchors.horizontalCenter: parent.horizontalCenter
                        source: Theme.emojiToImage(weatherEmoji(modelData.weatherIcon || ""))
                        sourceSize.width: Theme.scaled(16)
                        sourceSize.height: Theme.scaled(16)
                    }

                    // Temperature
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: formatTemp(modelData.temperature || 0)
                        color: Theme.textColor
                        font.family: Theme.captionFont.family
                        font.pixelSize: Theme.captionFont.pixelSize
                        font.bold: index === 0
                    }

                    // Precipitation probability (only if > 0)
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: (modelData.precipitationProbability || 0) > 0
                              ? (modelData.precipitationProbability + "%")
                              : ""
                        color: Theme.primaryColor
                        font.family: Theme.captionFont.family
                        font.pixelSize: Theme.scaled(10)
                        visible: (modelData.precipitationProbability || 0) > 0
                    }
                }
            }

            // Location + provider label
            Text {
                Layout.alignment: Qt.AlignHCenter
                visible: WeatherManager.valid
                text: {
                    var parts = []
                    if (WeatherManager.locationName) parts.push(WeatherManager.locationName)
                    parts.push(WeatherManager.provider)
                    return parts.join(" \u00B7 ")
                }
                color: Theme.textSecondaryColor
                font.family: Theme.captionFont.family
                font.pixelSize: Theme.scaled(10)
            }

            // Loading state
            Text {
                Layout.alignment: Qt.AlignHCenter
                visible: !WeatherManager.valid
                text: WeatherManager.loading ? "Loading weather..." : "No weather data"
                color: Theme.textSecondaryColor
                font: Theme.labelFont
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: {
                if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
                    var forecast = WeatherManager.hourlyForecast
                    if (forecast.length > 0) {
                        var f = forecast[0]
                        var msg = "Weather: " + (f.weatherDescription || "unknown")
                            + ", " + Math.round(f.temperature) + " degrees"
                            + ", humidity " + f.relativeHumidity + " percent"
                            + ", wind " + Math.round(f.windSpeed) + " kilometers per hour"
                        AccessibilityManager.announceLabel(msg)
                    }
                }
            }
        }
    }
}
