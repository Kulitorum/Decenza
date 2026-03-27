import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import Decenza
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
    Accessible.name: root.pageTitle
    Accessible.focusable: root.pageTitle.length > 0

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
                color: DE1Device.simulationMode ? Theme.simulationIndicatorColor : Theme.textColor
                font.pixelSize: Theme.scaled(20)
                font.bold: true
                elide: Text.ElideRight
                Accessible.ignored: true
            }

            Text {
                text: "- " + DE1Device.subStateString
                color: Theme.textSecondaryColor
                font: Theme.bodyFont
                visible: MachineState.isFlowing
                Accessible.ignored: true
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
                Layout.maximumWidth: root.width
                text: root.pageTitle
                color: DE1Device.simulationMode ? Theme.simulationIndicatorColor : Theme.textColor
                font: Theme.valueFont
                elide: Text.ElideRight
                Accessible.ignored: true
            }

            Text {
                Layout.alignment: Qt.AlignHCenter
                text: TranslationManager.translate("pageTitle.pageTitle", "Page Title")
                color: Theme.textSecondaryColor
                font: Theme.labelFont
                Accessible.ignored: true
            }
        }
    }
}
