import QtQuick
import QtQuick.Layouts
import Decenza

// Layout widget: the grind suggestion for the loaded profile and the active bag on
// the active equipment (add-genius-grind-widget; designed by Mark Palmos). Tapping
// opens GeniusGrindDialog to try another bean or profile and apply the result.
LayoutWidgetItem {
    id: root

    readonly property string labelText: TranslationManager.translate("geniusGrind.label", "Genius Grind")
    readonly property string profileTitle: String(ProfileService.currentProfileSummary.title || "")
    readonly property var _activeBag: {
        const bags = BagService.bags
        const activeId = Number(BagService.activeBagId)
        for (let i = 0; i < bags.length; ++i) {
            if (Number(bags[i].id) === activeId) return bags[i]
        }
        return null
    }
    readonly property GrindSuggestionRequest _request: GrindSuggestionRequest {
        active: root._activeBag !== null
        packageId: EquipmentService.activePackageId
        roaster: String(root._activeBag ? root._activeBag.roasterName || "" : "")
        coffee: String(root._activeBag ? root._activeBag.coffeeName || "" : "")
        roastDate: String(root._activeBag ? root._activeBag.roastDate || "" : "")
        profileTitle: root.profileTitle
    }
    readonly property string valueText: {
        const setting = String(root._request.suggestion.setting || "")
        return setting.length > 0 ? setting : "—"
    }

    // The host refreshes the bag list after every change but loads it only when asked.
    // Only here: on the in-process replica it flips loading synchronously, so calling
    // it from a change handler re-fires that handler's own bindings.
    Component.onCompleted: BagService.requestInventory()

    implicitWidth: root.isCompact ? col.implicitWidth : tile.implicitWidth
    implicitHeight: root.isCompact ? col.implicitHeight : tile.implicitHeight

    function openDialog(): void {
        geniusDialog.openFor(Number(BagService.activeBagId), root.profileTitle)
    }

    // --- Bars: label over value ---
    ColumnLayout {
        id: col
        visible: root.isCompact
        anchors.centerIn: parent
        width: parent.width
        spacing: 0
        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            text: root.labelText
            color: root.zoneTextColor
            font: Theme.labelFont
            Accessible.ignored: true
        }
        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            text: root.valueText
            color: root.zoneTextColor
            font.pixelSize: Theme.scaled(21)
            font.bold: root.zoneValueBold
            Accessible.ignored: true
        }
    }

    AccessibleMouseArea {
        visible: root.isCompact
        anchors.fill: parent
        accessibleName: root.labelText + ": " + root.valueText
        onAccessibleClicked: root.openDialog()
    }

    // --- Centre: an action tile matching the compiled tiles, with the number where
    // their icon sits ---
    Item {
        id: tile
        visible: !root.isCompact
        anchors.fill: parent
        implicitWidth: Math.max(Theme.scaled(150), tileLabel.implicitWidth + Theme.scaled(24))
        implicitHeight: Theme.scaled(120)

        readonly property color baseFill: root.zoneFillOverride.a > 0 ? root.zoneFillOverride
                                                                      : Theme.actionTileColor
        readonly property color fill: Theme.glassChrome ? Theme.chromeFill(tile.baseFill) : tile.baseFill
        readonly property color contentColor: Theme.contentColorOn(tile.fill, Theme.primaryContrastColor)

        Rectangle {
            anchors.fill: parent
            color: tileTap.isPressed ? Qt.darker(tile.fill, 1.2) : tile.fill
            radius: Theme.cardRadius
            border.width: Theme.hasBackgroundPreset ? 1 : 0
            border.color: Theme.borderColor
        }

        Column {
            anchors.centerIn: parent
            spacing: Theme.spacingSmall

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                height: Theme.scaled(48)
                verticalAlignment: Text.AlignVCenter
                text: root.valueText
                color: tile.contentColor
                font: Theme.valueFont
                Accessible.ignored: true
            }
            Text {
                id: tileLabel
                anchors.horizontalCenter: parent.horizontalCenter
                text: root.labelText
                color: tile.contentColor
                font: Theme.bodyFont
                Accessible.ignored: true
            }
        }

        AccessibleTapHandler {
            id: tileTap
            anchors.fill: parent
            accessibleName: root.labelText + ": " + root.valueText
            onAccessibleClicked: root.openDialog()
        }
    }

    GeniusGrindDialog {
        id: geniusDialog
    }
}
