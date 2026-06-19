import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import Decenza
import "../.."

// Idle-page Equipment button (add-equipment-packages). Tapping it opens the
// Equipment window, parallel to the Beans button. Simpler than BeansItem — no
// quick-select pill popup; equipment is switched per-bag from Brew Settings.
// In center zones LayoutItemDelegate compiles "equipment" to a CustomItem, so
// this file renders only in the compact (top/bottom/statusBar) zones.
Item {
    id: root
    property bool isCompact: false
    property string itemId: ""

    function goToEquipment() {
        if (typeof pageStack !== "undefined")
            pageStack.push(Qt.resolvedUrl("../../../pages/EquipmentPage.qml"))
    }

    implicitWidth: isCompact ? compactContent.implicitWidth : fullContent.implicitWidth
    implicitHeight: isCompact ? compactContent.implicitHeight : fullContent.implicitHeight

    // --- COMPACT MODE ---
    Item {
        id: compactContent
        visible: root.isCompact
        anchors.fill: parent
        implicitWidth: compactRow.implicitWidth + Theme.scaled(16)
        implicitHeight: Theme.bottomBarHeight

        RowLayout {
            id: compactRow
            anchors.centerIn: parent
            spacing: Theme.spacingSmall

            Image {
                source: "qrc:/icons/grind.svg"
                sourceSize.height: Theme.scaled(20)
                fillMode: Image.PreserveAspectFit
                Accessible.ignored: true
                layer.enabled: true
                layer.smooth: true
                layer.effect: MultiEffect {
                    colorization: 1.0
                    colorizationColor: Theme.textColor
                }
            }
            Tr {
                key: "idle.button.equipment"
                fallback: "Equipment"
                font: Theme.bodyFont
                color: Theme.textColor
                Accessible.ignored: true
            }
        }

        AccessibleTapHandler {
            anchors.fill: parent
            accessibleName: TranslationManager.translate("idle.button.equipment", "Equipment")
            accessibleDescription: TranslationManager.translate("idle.accessible.equipment.hint", "Tap to open the equipment inventory.")
            onAccessibleClicked: root.goToEquipment()
        }
    }

    // --- FULL MODE ---
    Item {
        id: fullContent
        visible: !root.isCompact
        anchors.fill: parent
        implicitWidth: Theme.scaled(150)
        implicitHeight: Theme.scaled(120)

        ActionButton {
            anchors.fill: parent
            translationKey: "idle.button.equipment"
            translationFallback: "Equipment"
            iconSource: "qrc:/icons/grind.svg"
            iconSize: Theme.scaled(43)
            backgroundColor: Theme.primaryColor
            onClicked: root.goToEquipment()

            Accessible.description: TranslationManager.translate("idle.accessible.equipment.description", "Open the equipment inventory to manage and switch grinders.")
        }
    }
}
