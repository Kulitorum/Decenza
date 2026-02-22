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

    Accessible.role: Accessible.StaticText
    Accessible.name: {
        if (!WeatherManager.valid) return "Weather: not available"
        var forecast = WeatherManager.hourlyForecast
        if (forecast.length > 0) {
            var rawTemp = forecast[0].temperature || 0
            var temp = WeatherManager.useImperialUnits
                ? Math.round(rawTemp * 9 / 5 + 32) : Math.round(rawTemp)
            var loc = WeatherManager.locationName || ""
            return "Weather: " + temp + " degrees" + (loc ? ", " + loc : "")
        }
        return "Weather"
    }
    Accessible.focusable: true

    // Moon phase emoji based on date (synodic month = 29.53059 days)
    function moonEmoji(timeStr) {
        // Reference new moon: Jan 6, 2000 18:14 UTC
        var refNew = Date.UTC(2000, 0, 6, 18, 14, 0) / 86400000
        var now = new Date(timeStr).getTime() / 86400000
        var phase = ((now - refNew) / 29.53059) % 1
        if (phase < 0) phase += 1
        // 8 phases: 🌑🌒🌓🌔🌕🌖🌗🌘
        var phases = [
            "\uD83C\uDF11", "\uD83C\uDF12", "\uD83C\uDF13", "\uD83C\uDF14",
            "\uD83C\uDF15", "\uD83C\uDF16", "\uD83C\uDF17", "\uD83C\uDF18"
        ]
        return phases[Math.floor(phase * 8) % 8]
    }

    // Map WMO weather icon names to unicode symbols
    function weatherEmoji(iconName, isDaytime, timeStr) {
        if (typeof isDaytime === "undefined") isDaytime = true
        switch (iconName) {
            case "clear":         return isDaytime ? "\u2600" : moonEmoji(timeStr)     // ☀ / moon phase
            case "partly-cloudy": return isDaytime ? "\u26C5" : moonEmoji(timeStr)     // ⛅ / moon phase
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

    // Format temperature with degree sign (F for US, C otherwise)
    function formatTemp(temp) {
        if (WeatherManager.useImperialUnits)
            return Math.round(temp * 9 / 5 + 32) + "\u00B0"
        return Math.round(temp) + "\u00B0"
    }

    // Format hour string: 12h for locales that use it, 24h otherwise
    // Input is "HH:mm" from the model
    function formatHour(hourStr) {
        if (!WeatherManager.use12HourTime)
            return hourStr
        var parts = (hourStr || "").split(":")
        if (parts.length < 2) return hourStr
        var h = parseInt(parts[0])
        var suffix = h >= 12 ? "pm" : "am"
        if (h === 0) h = 12
        else if (h > 12) h -= 12
        return h + suffix
    }

    // Wind direction arrow from degrees
    function windArrow(degrees) {
        var arrows = ["\u2193", "\u2199", "\u2190", "\u2196", "\u2191", "\u2197", "\u2192", "\u2198"]
        var index = Math.round(degrees / 45) % 8
        return arrows[index]
    }

    // --- COMPACT MODE (bar zone - flickable hourly forecast) ---
    Item {
        id: compactContent
        visible: root.isCompact
        anchors.fill: parent
        implicitWidth: Theme.scaled(160)
        implicitHeight: Theme.bottomBarHeight

        ListView {
            id: compactList
            anchors.fill: parent
            anchors.topMargin: Theme.scaled(2)
            anchors.bottomMargin: Theme.scaled(2)
            orientation: ListView.Horizontal
            spacing: Theme.scaled(8)
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            visible: WeatherManager.valid

            model: WeatherManager.hourlyForecast

            delegate: Item {
                width: Theme.scaled(32)
                height: compactList.height

                // Day-alternating background
                Rectangle {
                    anchors.fill: parent
                    color: parseInt((modelData.time || "").substring(8, 10)) % 2 === 0
                        ? Qt.rgba(1, 1, 1, 0.06) : "transparent"
                    radius: Theme.scaled(3)
                }
                // Daytime yellow overlay
                Rectangle {
                    anchors.fill: parent
                    color: Qt.rgba(1, 0.9, 0.3, 0.1)
                    radius: Theme.scaled(3)
                    visible: modelData.isDaytime || false
                }

                Column {
                    anchors.centerIn: parent
                    spacing: Theme.scaled(1)

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: formatHour(modelData.hour || "")
                        color: index === 0 ? Theme.primaryColor : Theme.textSecondaryColor
                        font: Theme.captionFont
                    }
                    Image {
                        anchors.horizontalCenter: parent.horizontalCenter
                        source: Theme.emojiToImage(weatherEmoji(modelData.weatherIcon || "", modelData.isDaytime, modelData.time || ""))
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
        }

        // Loading / no data fallback
        Text {
            visible: !WeatherManager.valid
            anchors.centerIn: parent
            text: WeatherManager.loading ? "..." : "--"
            color: Theme.textSecondaryColor
            font: Theme.bodyFont
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
                            return Theme.emojiToImage(weatherEmoji(forecast[0].weatherIcon || "", forecast[0].isDaytime, forecast[0].time || ""))
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
                                if (f.windSpeed > 0) {
                                    if (WeatherManager.useImperialUnits)
                                        parts.push(windArrow(f.windDirection) + Math.round(f.windSpeed * 0.621371) + "mph")
                                    else
                                        parts.push(windArrow(f.windDirection) + Math.round(f.windSpeed) + "km/h")
                                }
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

                delegate: Item {
                    width: Theme.scaled(38)
                    height: hourlyList.height

                    // Day-alternating background
                    Rectangle {
                        anchors.fill: parent
                        color: parseInt((modelData.time || "").substring(8, 10)) % 2 === 0
                            ? Qt.rgba(1, 1, 1, 0.06) : "transparent"
                        radius: Theme.scaled(3)
                    }
                    // Daytime yellow overlay
                    Rectangle {
                        anchors.fill: parent
                        color: Qt.rgba(1, 0.9, 0.3, 0.1)
                        radius: Theme.scaled(3)
                        visible: modelData.isDaytime || false
                    }

                    Column {
                        anchors.centerIn: parent
                        spacing: Theme.scaled(1)

                        // Hour
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: formatHour(modelData.hour || "")
                            color: index === 0 ? Theme.primaryColor : Theme.textSecondaryColor
                            font: Theme.captionFont
                        }

                        // Weather icon
                        Image {
                            anchors.horizontalCenter: parent.horizontalCenter
                            source: Theme.emojiToImage(weatherEmoji(modelData.weatherIcon || "", modelData.isDaytime, modelData.time || ""))
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

            // Loading / no-data state
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                visible: !WeatherManager.valid
                spacing: Theme.scaled(4)

                Image {
                    visible: !WeatherManager.loading
                    source: "qrc:/emoji/26c5.svg"
                    sourceSize.width: Theme.scaled(18)
                    sourceSize.height: Theme.scaled(18)
                }

                Text {
                    text: WeatherManager.loading ? "Loading weather..." : "Set city in Settings \u2192 Options"
                    color: Theme.textSecondaryColor
                    font: Theme.labelFont
                }

                Image {
                    visible: !WeatherManager.loading
                    source: "qrc:/emoji/1f327.svg"
                    sourceSize.width: Theme.scaled(18)
                    sourceSize.height: Theme.scaled(18)
                }
            }
        }

        // Accessibility only - don't block flick gestures on the hourly list
        MouseArea {
            anchors.fill: parent
            enabled: typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled
            onClicked: {
                var forecast = WeatherManager.hourlyForecast
                if (forecast.length > 0) {
                    var f = forecast[0]
                    var imperial = WeatherManager.useImperialUnits
                    var tempVal = imperial ? Math.round(f.temperature * 9 / 5 + 32) : Math.round(f.temperature)
                    var windVal = imperial ? Math.round(f.windSpeed * 0.621371) : Math.round(f.windSpeed)
                    var windUnit = imperial ? "miles per hour" : "kilometers per hour"
                    var msg = "Weather: " + (f.weatherDescription || "unknown")
                        + ", " + tempVal + " degrees"
                        + ", humidity " + f.relativeHumidity + " percent"
                        + ", wind " + windVal + " " + windUnit
                    AccessibilityManager.announceLabel(msg)
                }
            }
        }
    }
}
