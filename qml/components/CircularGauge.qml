import QtQuick
import QtQuick.Shapes
import Decenza

Item {
    id: root

    property real value: 0
    property real minValue: 0
    property real maxValue: 10
    property string unit: ""
    property string label: ""
    property color color: Theme.primaryColor

    readonly property real normalizedValue: Math.max(0, Math.min(1, (value - minValue) / (maxValue - minValue)))
    readonly property real startAngle: 135
    readonly property real sweepAngle: 270

    implicitWidth: Theme.gaugeSize
    implicitHeight: Theme.gaugeSize + Theme.scaled(20)

    // Gauge sizing calculations
    readonly property int gaugeWidth: Theme.gaugeSize
    readonly property real gaugeCenter: gaugeWidth / 2
    readonly property real gaugeStroke: Theme.scaled(8)
    readonly property real gaugeRadius: gaugeCenter - gaugeStroke / 2 - Theme.scaled(8)

    Shape {
        id: gauge
        anchors.horizontalCenter: parent.horizontalCenter
        width: root.gaugeWidth
        height: root.gaugeWidth

        // Background arc
        ShapePath {
            strokeColor: Qt.rgba(root.color.r, root.color.g, root.color.b, 0.2)
            strokeWidth: root.gaugeStroke
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap

            PathAngleArc {
                centerX: root.gaugeCenter
                centerY: root.gaugeCenter
                radiusX: root.gaugeRadius
                radiusY: root.gaugeRadius
                startAngle: root.startAngle
                sweepAngle: root.sweepAngle
            }
        }

        // Value arc
        ShapePath {
            strokeColor: root.color
            strokeWidth: root.gaugeStroke
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap

            PathAngleArc {
                centerX: root.gaugeCenter
                centerY: root.gaugeCenter
                radiusX: root.gaugeRadius
                radiusY: root.gaugeRadius
                startAngle: root.startAngle
                sweepAngle: root.sweepAngle * root.normalizedValue
            }
        }
    }

    // Value text
    Column {
        anchors.centerIn: gauge
        anchors.verticalCenterOffset: Theme.scaled(-5)

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.value.toFixed(1)
            color: Theme.textColor
            font.pixelSize: Theme.scaled(20)
            font.bold: true
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.unit
            color: Theme.textSecondaryColor
            font: Theme.labelFont
        }
    }

    // Label
    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: gauge.bottom
        anchors.topMargin: Theme.scaled(5)
        text: root.label
        color: Theme.textSecondaryColor
        font: Theme.labelFont
    }
}
