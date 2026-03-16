import QtQuick
import QtQuick.Effects
import QtQuick.Layouts
import Decenza
import "../.."

Item {
    id: root
    property bool isCompact: false
    property string itemId: ""

    readonly property bool scaleConnected: ScaleDevice && ScaleDevice.connected && !ScaleDevice.isFlowScale
    readonly property int level: scaleConnected ? ScaleDevice.batteryLevel : -1
    readonly property bool hasLevel: level >= 0 && level <= 100

    readonly property color levelColor: {
        if (!hasLevel) return Theme.textSecondaryColor
        if (level > 50) return Theme.successColor
        if (level > 20) return Theme.warningColor
        return Theme.errorColor
    }

    readonly property string iconSource: {
        if (!hasLevel)       return "qrc:/icons/battery-0.svg"
        if (level <= 10)     return "qrc:/icons/battery-0.svg"
        if (level <= 37)     return "qrc:/icons/battery-25.svg"
        if (level <= 62)     return "qrc:/icons/battery-50.svg"
        if (level <= 87)     return "qrc:/icons/battery-75.svg"
        return "qrc:/icons/battery-100.svg"
    }

    readonly property string displayText: {
        if (!scaleConnected) return "--"
        if (!hasLevel) return "N/A"
        return level + "%"
    }

    readonly property string accessibleText: {
        if (!scaleConnected)
            return TranslationManager.translate("scaleBattery.accessible.disconnected", "Scale battery: no scale connected")
        if (!hasLevel)
            return TranslationManager.translate("scaleBattery.accessible.notReported", "Scale battery: not reported by this scale")
        var warning = level <= 20
            ? ". " + TranslationManager.translate("scaleBattery.accessible.warning", "Warning: scale battery is low")
            : ""
        return TranslationManager.translate("scaleBattery.accessible.level", "Scale battery: %1 percent").arg(level) + warning
    }

    implicitWidth: isCompact ? compactContent.implicitWidth : fullContent.implicitWidth
    implicitHeight: isCompact ? compactContent.implicitHeight : fullContent.implicitHeight

    Accessible.role: Accessible.StaticText
    Accessible.name: root.accessibleText
    Accessible.focusable: true

    // --- COMPACT MODE ---
    Item {
        id: compactContent
        visible: root.isCompact
        anchors.fill: parent
        implicitWidth: compactRow.implicitWidth
        implicitHeight: compactRow.implicitHeight

        Row {
            id: compactRow
            anchors.centerIn: parent
            spacing: Theme.spacingSmall

            Image {
                anchors.verticalCenter: parent.verticalCenter
                source: root.iconSource
                sourceSize.width: Theme.bodyFont.pixelSize
                sourceSize.height: Theme.bodyFont.pixelSize
                Accessible.ignored: true
                layer.enabled: !Theme.isDarkMode
                layer.smooth: true
                layer.effect: MultiEffect {
                    colorization: 1.0
                    colorizationColor: Theme.textColor
                }
            }

            Text {
                text: root.displayText
                color: root.levelColor
                font: Theme.bodyFont
                Accessible.ignored: true
            }
        }

        AccessibleMouseArea {
            anchors.fill: parent
            anchors.margins: -Theme.spacingSmall
            accessibleName: root.accessibleText
            accessibleItem: compactContent
            onAccessibleClicked: {
                if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
                    AccessibilityManager.announceLabel(root.accessibleText)
                }
            }
        }
    }

    // --- FULL MODE ---
    Item {
        id: fullContent
        visible: !root.isCompact
        anchors.fill: parent
        implicitWidth: fullColumn.implicitWidth
        implicitHeight: fullColumn.implicitHeight

        ColumnLayout {
            id: fullColumn
            anchors.centerIn: parent
            spacing: Theme.spacingSmall

            Row {
                Layout.alignment: Qt.AlignHCenter
                spacing: Theme.spacingSmall

                Image {
                    anchors.verticalCenter: parent.verticalCenter
                    source: root.iconSource
                    sourceSize.width: Theme.valueFont.pixelSize
                    sourceSize.height: Theme.valueFont.pixelSize
                    Accessible.ignored: true
                }

                Text {
                    text: root.displayText
                    color: root.levelColor
                    font: Theme.valueFont
                    Accessible.ignored: true
                }
            }

            Tr {
                Layout.alignment: Qt.AlignHCenter
                key: "idle.label.scaleBattery"
                fallback: "Scale Battery"
                color: Theme.textSecondaryColor
                font: Theme.labelFont
                Accessible.ignored: true
            }
        }

        AccessibleMouseArea {
            anchors.fill: parent
            accessibleName: root.accessibleText
            accessibleItem: fullContent
            onAccessibleClicked: {
                if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
                    AccessibilityManager.announceLabel(root.accessibleText)
                }
            }
        }
    }
}
