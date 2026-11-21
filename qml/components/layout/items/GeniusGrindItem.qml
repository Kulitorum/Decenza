import QtQuick
import QtQuick.Layouts
import Decenza

// Layout widget: the grind suggestion for the loaded profile and the active bag on
// the active equipment (add-genius-grind-widget; designed by Mark Palmos). Tapping
// opens GeniusGrindDialog to try another bean or profile and apply the result.
LayoutWidgetItem {
    id: root

    property var suggestion: ({})

    readonly property string labelText: TranslationManager.translate("geniusGrind.label", "Genius Grind")
    readonly property string profileTitle: String(ProfileService.currentProfileSummary.title || "")
    readonly property string valueText: {
        const setting = String(root.suggestion.setting || "")
        return setting.length > 0 ? setting : "—"
    }
    readonly property var _activeBag: {
        const bags = BagService.bags
        const activeId = Number(BagService.activeBagId)
        for (let i = 0; i < bags.length; ++i) {
            if (Number(bags[i].id) === activeId) return bags[i]
        }
        return null
    }
    // A shot saved, rated, edited or deleted moves historyRevision; comparing the key
    // by value ignores the replica's other notifications. The handler must not call
    // BagService.requestInventory(): on the in-process replica that flips loading
    // synchronously, re-firing this binding inside its own change handler.
    readonly property string requestKey: {
        const bag = root._activeBag
        return EquipmentService.ready + "|" + EquipmentService.activePackageId + "|"
            + (bag ? bag.id + "|" + bag.roasterName + "|" + bag.coffeeName + "|" + bag.roastDate : "")
            + "|" + root.profileTitle + "|" + HistoryService.historyRevision
    }

    onRequestKeyChanged: root.requestSuggestion()
    Component.onCompleted: {
        BagService.requestInventory()
        root.requestSuggestion()
    }

    function bagMatches(bag: var, s: var): bool {
        return !!bag && s.packageId === EquipmentService.activePackageId
            && s.beanBrand === String(bag.roasterName || "")
            && s.beanType === String(bag.coffeeName || "")
            && s.roastDate === String(bag.roastDate || "")
    }

    function requestSuggestion(): void {
        root.suggestion = ({})
        const bag = root._activeBag
        const packageId = EquipmentService.activePackageId
        if (!bag || root.profileTitle.length === 0 || packageId <= 0 || !EquipmentService.ready)
            return
        EquipmentService.requestGrindSuggestion(packageId, String(bag.roasterName || ""),
                                                String(bag.coffeeName || ""),
                                                String(bag.roastDate || ""), root.profileTitle)
    }

    Connections {
        target: BagService
        function onBagsChanged() {
            BagService.requestInventory()
        }
    }

    Connections {
        target: EquipmentService
        function onGrindSuggestionReady(suggestion) {
            if (!root.bagMatches(root._activeBag, suggestion) || suggestion.profileTitle !== root.profileTitle)
                return
            root.suggestion = suggestion
        }
        function onGrindCalibrationChanged(packageId) {
            root.requestSuggestion()
        }
    }

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
        }
        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            text: root.valueText
            color: root.zoneTextColor
            font.family: Theme.bodyFont.family
            font.pixelSize: Theme.scaled(21)
            font.bold: true
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
                font.family: Theme.bodyFont.family
                font.pixelSize: Theme.scaled(40)
                font.bold: true
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
