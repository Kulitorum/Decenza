import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import DecenzaDE1
import "../.."

Item {
    id: root
    property bool isCompact: false
    property string itemId: ""

    // Access currentPageTitle from the ApplicationWindow (main.qml)
    property string pageTitle: {
        var win = Window.window
        return win ? (win.currentPageTitle || "") : ""
    }

    implicitWidth: isCompact ? compactContent.implicitWidth : fullContent.implicitWidth
    implicitHeight: isCompact ? compactContent.implicitHeight : fullContent.implicitHeight

    Accessible.role: Accessible.StaticText
    Accessible.name: root.pageTitle || "Decenza"
    Accessible.focusable: true

    // --- COMPACT MODE (status bar rendering) ---
    Item {
        id: compactContent
        visible: root.isCompact
        anchors.fill: parent
        implicitWidth: compactRow.implicitWidth
        implicitHeight: compactRow.implicitHeight

        Row {
            id: compactRow
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingSmall

            Text {
                text: root.pageTitle
                color: DE1Device.simulationMode ? "#E65100" : Theme.textColor
                font.pixelSize: Theme.scaled(20)
                font.bold: true
                elide: Text.ElideRight
            }

            Text {
                text: "- " + DE1Device.subStateString
                color: Theme.textSecondaryColor
                font: Theme.bodyFont
                visible: MachineState.isFlowing
            }
        }
    }

    // --- FULL MODE (center rendering) ---
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

            Text {
                Layout.alignment: Qt.AlignHCenter
                text: root.pageTitle
                color: DE1Device.simulationMode ? "#E65100" : Theme.textColor
                font: Theme.valueFont
            }

            Text {
                Layout.alignment: Qt.AlignHCenter
                text: "Page Title"
                color: Theme.textSecondaryColor
                font: Theme.labelFont
            }
        }
    }
}
