import QtQuick
import QtQuick.Shapes
import DE1App

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

    implicitWidth: 120
    implicitHeight: 140

    Shape {
        id: gauge
        anchors.horizontalCenter: parent.horizontalCenter
        width: 100
        height: 100

        // Background arc
        ShapePath {
            strokeColor: Qt.rgba(root.color.r, root.color.g, root.color.b, 0.2)
            strokeWidth: 8
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap

            PathAngleArc {
                centerX: 50
                centerY: 50
                radiusX: 42
                radiusY: 42
                startAngle: root.startAngle
                sweepAngle: root.sweepAngle
            }
        }

        // Value arc
        ShapePath {
            strokeColor: root.color
            strokeWidth: 8
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap

            PathAngleArc {
                centerX: 50
                centerY: 50
                radiusX: 42
                radiusY: 42
                startAngle: root.startAngle
                sweepAngle: root.sweepAngle * root.normalizedValue
            }
        }
    }

    // Value text
    Column {
        anchors.centerIn: gauge
        anchors.verticalCenterOffset: -5

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.value.toFixed(1)
            color: Theme.textColor
            font.pixelSize: 20
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
        anchors.topMargin: 5
        text: root.label
        color: Theme.textSecondaryColor
        font: Theme.labelFont
    }
}
