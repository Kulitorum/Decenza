import QtQuick
import QtQuick.Layouts
import Decenza
import "../.."

Item {
    id: root
    property bool isCompact: false
    property string itemId: ""

    readonly property int level: BatteryManager.batteryPercent

    readonly property color levelColor: level > 50 ? Theme.successColor :
                                        level > 20 ? Theme.warningColor : Theme.errorColor

    readonly property string iconSource: {
        if (level <= 10)      return "qrc:/icons/battery-0.svg"
        if (level <= 37)      return "qrc:/icons/battery-25.svg"
        if (level <= 62)      return "qrc:/icons/battery-50.svg"
        if (level <= 87)      return "qrc:/icons/battery-75.svg"
        return "qrc:/icons/battery-100.svg"
    }

    implicitWidth: isCompact ? compactContent.implicitWidth : fullContent.implicitWidth
    implicitHeight: isCompact ? compactContent.implicitHeight : fullContent.implicitHeight

    Accessible.role: Accessible.StaticText
    Accessible.name: {
        var warning = root.level <= 20 ? ". Warning: battery level is low" : ""
        return "Battery level: " + root.level + " percent" + warning
    }
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
            }

            Text {
                text: root.level + "%"
                color: root.levelColor
                font: Theme.bodyFont
                Accessible.ignored: true
            }
        }

        MouseArea {
            anchors.fill: parent
            anchors.margins: -Theme.spacingSmall
            onClicked: {
                if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
                    var warning = root.level <= 20 ? ". Warning: battery level is low" : ""
                    AccessibilityManager.announceLabel("Battery level: " + root.level + " percent" + warning)
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
                    text: root.level + "%"
                    color: root.levelColor
                    font: Theme.valueFont
                    Accessible.ignored: true
                }
            }

            Tr {
                Layout.alignment: Qt.AlignHCenter
                key: "idle.label.battery"
                fallback: "Battery"
                color: Theme.textSecondaryColor
                font: Theme.labelFont
                Accessible.ignored: true
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: {
                if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled) {
                    var warning = root.level <= 20 ? ". Warning: battery level is low" : ""
                    AccessibilityManager.announceLabel("Battery level: " + root.level + " percent" + warning)
                }
            }
        }
    }
}
