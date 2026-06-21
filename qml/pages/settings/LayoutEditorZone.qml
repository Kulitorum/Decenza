import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtQml.Models
import Decenza
import "../../components"

Rectangle {
    id: root

    property string zoneName: ""
    property string zoneLabel: ""
    property var items: []
    property string selectedItemId: ""
    property bool zoneSelected: false
    property bool showPositionControls: false
    property int yOffset: 0
    property real zoneScale: 1.0

    // True while a chip is being dragged for reorder (suppresses selection
    // highlight so it doesn't jump during live swaps).
    property bool _dragging: false
    // Whether a screen reader is active — gates the accessible-only reorder
    // fallback buttons (drag has no assistive-tech equivalent).
    readonly property bool _a11yEnabled: typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled

    signal itemTapped(string itemId)
    signal zoneTapped()
    signal itemRemoved(string itemId)
    signal moveLeft(string itemId)
    signal moveRight(string itemId)
    signal reorder(int fromIndex, int toIndex)
    signal addItemRequested(string type)
    signal moveUp()
    signal moveDown()
    signal scaleUp()
    signal scaleDown()
    signal editCustomRequested(string itemId, string zoneName)
    signal zoneOptionsRequested()

    Layout.fillWidth: true
    implicitHeight: zoneContent.implicitHeight + Theme.scaled(20)
    color: Theme.surfaceColor
    radius: Theme.cardRadius
    border.color: zoneSelected ? Theme.primaryColor : Theme.borderColor
    border.width: zoneSelected ? 2 : 1

    ColumnLayout {
        id: zoneContent
        anchors.fill: parent
        anchors.margins: Theme.scaled(10)
        spacing: Theme.spacingSmall

        // Zone label with optional position controls
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.scaled(8)

            Text {
                text: root.zoneLabel
                color: Theme.textSecondaryColor
                font: Theme.labelFont
                Accessible.role: Accessible.Heading
                Accessible.name: TranslationManager.translate("layoutEditor.zoneHeading", "%1 zone").arg(root.zoneLabel)

                // Long-press / double-click the zone label opens zone options.
                MouseArea {
                    anchors.fill: parent
                    onPressAndHold: root.zoneOptionsRequested()
                    onDoubleClicked: root.zoneOptionsRequested()
                }
            }

            // Zone options (distribution / alignment / style / populate)
            StyledIconButton {
                implicitWidth: Theme.scaled(32)
                implicitHeight: Theme.scaled(32)
                icon.source: "qrc:/icons/more-vertical.svg"
                accessibleName: TranslationManager.translate("layoutEditor.zoneOptions", "%1 options").arg(root.zoneLabel)
                onClicked: root.zoneOptionsRequested()
            }

            Item { Layout.fillWidth: true }

            // Position offset display
            Text {
                visible: root.showPositionControls && root.yOffset !== 0
                text: (root.yOffset > 0 ? "+" : "") + root.yOffset
                color: Theme.textSecondaryColor
                font: Theme.captionFont
            }

            // UP arrow (ArrowLeft rotated to point up \u2014 avoids a new asset)
            StyledIconButton {
                visible: root.showPositionControls
                implicitWidth: Theme.scaled(32)
                implicitHeight: Theme.scaled(32)
                icon.source: "qrc:/icons/ArrowLeft.svg"
                icon.width: Theme.scaled(14)
                icon.height: Theme.scaled(14)
                rotation: 90
                inactiveColor: Theme.primaryColor
                accessibleName: TranslationManager.translate("layoutEditor.moveZoneUp", "Move %1 up").arg(root.zoneLabel)
                onClicked: root.moveUp()
            }

            // DOWN arrow (ArrowLeft rotated to point down)
            StyledIconButton {
                visible: root.showPositionControls
                implicitWidth: Theme.scaled(32)
                implicitHeight: Theme.scaled(32)
                icon.source: "qrc:/icons/ArrowLeft.svg"
                icon.width: Theme.scaled(14)
                icon.height: Theme.scaled(14)
                rotation: 270
                inactiveColor: Theme.primaryColor
                accessibleName: TranslationManager.translate("layoutEditor.moveZoneDown", "Move %1 down").arg(root.zoneLabel)
                onClicked: root.moveDown()
            }

            // Scale separator
            Rectangle {
                visible: root.showPositionControls
                width: 1
                height: Theme.scaled(20)
                color: Theme.borderColor
            }

            // Scale display
            Text {
                visible: root.showPositionControls && root.zoneScale !== 1.0
                text: "\u00D7" + root.zoneScale.toFixed(2)
                color: Theme.textSecondaryColor
                font: Theme.captionFont
            }

            // Scale DOWN (smaller)
            StyledIconButton {
                visible: root.showPositionControls
                implicitWidth: Theme.scaled(32)
                implicitHeight: Theme.scaled(32)
                icon.source: "qrc:/icons/minus.svg"
                icon.width: Theme.scaled(14)
                icon.height: Theme.scaled(14)
                inactiveColor: Theme.primaryColor
                accessibleName: TranslationManager.translate("layoutEditor.makeZoneSmaller", "Make %1 smaller").arg(root.zoneLabel)
                onClicked: root.scaleDown()
            }

            // Scale UP (bigger)
            StyledIconButton {
                visible: root.showPositionControls
                implicitWidth: Theme.scaled(32)
                implicitHeight: Theme.scaled(32)
                icon.source: "qrc:/icons/plus.svg"
                icon.width: Theme.scaled(14)
                icon.height: Theme.scaled(14)
                inactiveColor: Theme.primaryColor
                accessibleName: TranslationManager.translate("layoutEditor.makeZoneBigger", "Make %1 bigger").arg(root.zoneLabel)
                onClicked: root.scaleUp()
            }
        }

        // Items in zone
        Flow {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall

            // Animate chips sliding out of the way during a drag reorder.
            move: Transition {
                NumberAnimation { properties: "x,y"; duration: 180; easing.type: Easing.OutQuad }
            }

            // Reorderable widget chips. Drag a chip to a new slot; the
            // DelegateModel live-swaps and the Flow `move` transition slides the
            // displaced chips. On release we persist via reorder(from,to).
            // Pattern adapted from FavoritesListView.qml.
            Repeater {
                model: DelegateModel {
                    id: visualModel
                    model: root.items

                    delegate: Item {
                        id: chipDelegate
                        width: chipBody.width
                        height: chipBody.height

                        readonly property int liveIndex: DelegateModel.itemsIndex
                        readonly property bool isSelected: !root._dragging && (modelData.id === root.selectedItemId)
                        readonly property bool hasOptions: Settings.network.typeHasOptions(modelData.type)

                        Rectangle {
                            id: chipBody
                            width: chipRow.implicitWidth + Theme.scaled(16)
                            height: Theme.scaled(36)
                            radius: Theme.scaled(8)
                            color: chipDelegate.isSelected ? Theme.primaryColor : Theme.backgroundColor
                            // A coloured border marks chips that carry per-instance
                            // options (custom orange retained; others use accent).
                            border.color: dragMa.drag.active ? Theme.primaryColor
                                : (chipDelegate.hasOptions && !chipDelegate.isSelected
                                    ? (modelData.type === "custom" ? "orange" : Theme.primaryColor)
                                    : Theme.borderColor)
                            border.width: (dragMa.drag.active || (chipDelegate.hasOptions && !chipDelegate.isSelected)) ? 2 : 1
                            scale: dragMa.drag.active ? 1.05 : 1.0
                            opacity: dragMa.drag.active ? 0.95 : 1.0
                            z: dragMa.drag.active ? 100 : 1

                            Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutQuad } }

                            Drag.active: dragMa.drag.active
                            Drag.source: chipDelegate
                            Drag.hotSpot.x: width / 2
                            Drag.hotSpot.y: height / 2

                            // Lift out to the overlay while dragging so the chip
                            // renders above siblings and moves freely.
                            states: State {
                                when: dragMa.drag.active
                                ParentChange { target: chipBody; parent: dragLayer }
                            }

                            Accessible.role: Accessible.Button
                            Accessible.name: {
                                var nm = modelData.type === "custom"
                                    ? root.getTextChipLabel(modelData)
                                    : getItemDisplayName(modelData.type)
                                var suffix = chipDelegate.isSelected ? ", " + TranslationManager.translate("layoutEditor.selected", "selected") : ""
                                return TranslationManager.translate("layoutEditor.widgetItem", "%1 widget").arg(nm) + suffix
                            }
                            Accessible.focusable: true
                            Accessible.onPressAction: root.itemTapped(modelData.id)

                            RowLayout {
                                id: chipRow
                                anchors.centerIn: parent
                                spacing: Theme.scaled(4)

                                // Accessible-only reorder fallback (drag has no
                                // screen-reader equivalent). Hidden in normal use.
                                StyledIconButton {
                                    visible: root._a11yEnabled && chipDelegate.isSelected && chipDelegate.liveIndex > 0
                                    implicitWidth: Theme.scaled(28)
                                    implicitHeight: Theme.scaled(28)
                                    icon.source: "qrc:/icons/ArrowLeft.svg"
                                    icon.width: Theme.scaled(14)
                                    icon.height: Theme.scaled(14)
                                    active: chipDelegate.isSelected
                                    activeColor: Theme.primaryContrastColor
                                    accessibleName: TranslationManager.translate("layoutEditor.moveToStart", "Move toward start")
                                    onClicked: root.moveLeft(modelData.id)
                                }

                                // Standard label for non-text items
                                Text {
                                    visible: modelData.type !== "custom"
                                    text: getItemDisplayName(modelData.type)
                                    color: chipDelegate.isSelected
                                        ? Theme.primaryContrastColor
                                        : ((modelData.type === "spacer" || modelData.type === "separator" || modelData.type === "weather") ? "orange"
                                        : ((modelData.type.startsWith("screensaver") || modelData.type === "lastShot") ? "#64B5F6" : Theme.textColor))
                                    font: Theme.bodyFont
                                }

                                // Mini preview for text items
                                Row {
                                    visible: modelData.type === "custom"
                                    spacing: Theme.scaled(3)
                                    Layout.alignment: Qt.AlignVCenter

                                    Image {
                                        visible: (modelData.emoji || "") !== ""
                                        source: visible ? Theme.emojiToImage(modelData.emoji || "") : ""
                                        sourceSize.width: Theme.scaled(18)
                                        sourceSize.height: Theme.scaled(18)
                                        anchors.verticalCenter: parent.verticalCenter
                                    }

                                    Text {
                                        text: root.getTextChipLabel(modelData)
                                        color: chipDelegate.isSelected ? Theme.primaryContrastColor : "orange"
                                        font: Theme.captionFont
                                        anchors.verticalCenter: parent.verticalCenter
                                    }
                                }

                                // Accessible-only reorder fallback (toward end).
                                StyledIconButton {
                                    visible: root._a11yEnabled && chipDelegate.isSelected && chipDelegate.liveIndex < root.items.length - 1
                                    implicitWidth: Theme.scaled(28)
                                    implicitHeight: Theme.scaled(28)
                                    icon.source: "qrc:/icons/ArrowLeft.svg"
                                    icon.width: Theme.scaled(14)
                                    icon.height: Theme.scaled(14)
                                    rotation: 180
                                    active: chipDelegate.isSelected
                                    activeColor: Theme.primaryContrastColor
                                    accessibleName: TranslationManager.translate("layoutEditor.moveToEnd", "Move toward end")
                                    onClicked: root.moveRight(modelData.id)
                                }

                                // Options button \u2014 persistent has-options indicator
                                // AND the explicit open-options affordance.
                                StyledIconButton {
                                    visible: chipDelegate.hasOptions
                                    implicitWidth: Theme.scaled(28)
                                    implicitHeight: Theme.scaled(28)
                                    icon.source: "qrc:/icons/settings.svg"
                                    icon.width: Theme.scaled(15)
                                    icon.height: Theme.scaled(15)
                                    active: chipDelegate.isSelected
                                    activeColor: Theme.primaryContrastColor
                                    accessibleName: TranslationManager.translate("layoutEditor.editOptions", "Edit %1 options").arg(getItemDisplayName(modelData.type))
                                    onClicked: root.editCustomRequested(modelData.id, root.zoneName)
                                }

                                // Remove button. Always occupies its slot so
                                // selecting a chip never resizes it (no jump);
                                // it's faint by default and brightens on hover or
                                // selection — so it's discoverable without a click.
                                StyledIconButton {
                                    implicitWidth: Theme.scaled(28)
                                    implicitHeight: Theme.scaled(28)
                                    icon.source: "qrc:/icons/cross.svg"
                                    icon.width: Theme.scaled(14)
                                    icon.height: Theme.scaled(14)
                                    inactiveColor: Theme.errorColor
                                    activeColor: Theme.errorColor
                                    active: true
                                    opacity: (chipDelegate.isSelected || chipHover.hovered) ? 1.0 : 0.4
                                    accessibleName: TranslationManager.translate("layoutEditor.removeWidget", "Remove widget")
                                    onClicked: root.itemRemoved(modelData.id)
                                    Behavior on opacity { NumberAnimation { duration: 100 } }
                                }
                            }

                            // Brighten the remove control on mouse hover (desktop);
                            // on touch the chip's selection state reveals it.
                            HoverHandler { id: chipHover }

                            // Pointer interaction: drag to reorder, tap to select,
                            // long-press to open options. A small drag threshold
                            // keeps quick taps as selection. preventStealing keeps
                            // the surrounding ScrollView from hijacking the drag.
                            MouseArea {
                                id: dragMa
                                anchors.fill: parent
                                z: -1
                                drag.target: chipBody
                                drag.threshold: Theme.scaled(8)
                                preventStealing: true

                                property int _startIndex: -1
                                property bool _held: false

                                onPressed: {
                                    _startIndex = chipDelegate.liveIndex
                                    _held = false
                                }
                                onPositionChanged: {
                                    if (drag.active) root._dragging = true
                                }
                                onClicked: {
                                    if (!drag.active && !_held) root.itemTapped(modelData.id)
                                }
                                onPressAndHold: {
                                    if (!drag.active && chipDelegate.hasOptions) {
                                        _held = true
                                        root.editCustomRequested(modelData.id, root.zoneName)
                                    }
                                }
                                onReleased: {
                                    if (root._dragging) {
                                        var endIndex = chipDelegate.liveIndex
                                        if (_startIndex >= 0 && endIndex !== _startIndex)
                                            root.reorder(_startIndex, endIndex)
                                    }
                                    root._dragging = false
                                    _startIndex = -1
                                }
                                onCanceled: {
                                    // Roll back any live swaps so the DelegateModel
                                    // order matches the unchanged backing list.
                                    if (root._dragging) {
                                        var cur = chipDelegate.liveIndex
                                        if (_startIndex >= 0 && cur !== _startIndex)
                                            visualModel.items.move(cur, _startIndex, 1)
                                    }
                                    root._dragging = false
                                    _startIndex = -1
                                }
                            }
                        }

                        // Live swap: when the dragged chip enters this slot,
                        // shuffle the DelegateModel so chips animate out of the way.
                        DropArea {
                            anchors.fill: parent
                            onEntered: function(drag) {
                                var src = drag.source
                                if (!src || src === chipDelegate) return
                                var from = src.liveIndex
                                var to = chipDelegate.liveIndex
                                if (from !== to) visualModel.items.move(from, to, 1)
                            }
                        }
                    }
                }
            }

            // Add widget button
            StyledIconButton {
                id: addButton
                implicitWidth: Theme.scaled(36)
                implicitHeight: Theme.scaled(36)
                icon.source: "qrc:/icons/plus.svg"
                inactiveColor: Theme.primaryColor
                accessibleName: TranslationManager.translate("layoutEditor.addWidgetTo", "Add widget to %1").arg(root.zoneLabel)
                onClicked: addPopup.open()

                Dialog {
                    id: addPopup
                    modal: true
                    parent: Overlay.overlay
                    anchors.centerIn: parent
                    padding: Theme.scaled(4)
                    closePolicy: Dialog.CloseOnPressOutside | Dialog.CloseOnEscape
                    onOpened: widgetFilter.text = ""

                    background: Rectangle {
                        color: Theme.surfaceColor
                        radius: Theme.cardRadius
                        border.color: Theme.borderColor
                        border.width: 1
                    }

                    contentItem: ColumnLayout {
                        spacing: Theme.scaled(4)

                        StyledTextField {
                            id: widgetFilter
                            Layout.fillWidth: true
                            Layout.preferredWidth: Theme.scaled(220)
                            placeholderText: TranslationManager.translate("layoutEditor.filterWidgets", "Filter widgets…")
                        }

                        ListView {
                            id: addListView
                            Layout.fillWidth: true
                            Layout.preferredWidth: Theme.scaled(220)
                            implicitHeight: Math.min(contentHeight, Theme.scaled(380))
                            boundsBehavior: Flickable.StopAtBounds
                            clip: true

                            // Category labels indexed by the `cat` field below.
                            readonly property var catNames: [
                                TranslationManager.translate("layoutEditor.catActions", "Actions"),
                                TranslationManager.translate("layoutEditor.catReadouts", "Readouts"),
                                TranslationManager.translate("layoutEditor.catUtility", "Utility"),
                                TranslationManager.translate("layoutEditor.catScreensavers", "Screensavers")
                            ]

                            // Full widget catalog: type, category index, label.
                            // Same grouping/order the web picker uses.
                            readonly property var catalog: [
                                // Actions (0)
                                { type: "espresso", cat: 0, label: TranslationManager.translate("layoutEditor.widgetEspresso", "Espresso") },
                                { type: "steam", cat: 0, label: TranslationManager.translate("layoutEditor.widgetSteam", "Steam") },
                                { type: "hotwater", cat: 0, label: TranslationManager.translate("layoutEditor.widgetHotWater", "Hot Water") },
                                { type: "flush", cat: 0, label: TranslationManager.translate("layoutEditor.widgetFlush", "Flush") },
                                { type: "sleep", cat: 0, label: TranslationManager.translate("layoutEditor.widgetSleep", "Sleep") },
                                { type: "settings", cat: 0, label: TranslationManager.translate("layoutEditor.widgetSettings", "Settings") },
                                { type: "quit", cat: 0, label: TranslationManager.translate("layoutEditor.widgetQuit", "Quit") },
                                { type: "history", cat: 0, label: TranslationManager.translate("layoutEditor.widgetHistory", "History") },
                                { type: "beans", cat: 0, label: TranslationManager.translate("layoutEditor.widgetBeans", "Beans") },
                                { type: "equipment", cat: 0, label: TranslationManager.translate("layoutEditor.widgetEquipment", "Equipment") },
                                { type: "autofavorites", cat: 0, label: TranslationManager.translate("layoutEditor.widgetFavorites", "Favorites") },
                                { type: "discuss", cat: 0, label: TranslationManager.translate("layoutEditor.widgetDiscuss", "Discuss") },
                                { type: "ghcSimulator", cat: 0, label: TranslationManager.translate("layoutEditor.widgetGHCSimulator", "Mini GHC") },
                                // Readouts (1)
                                { type: "machineStatus", cat: 1, label: TranslationManager.translate("layoutEditor.widgetMachineStatus", "Machine Status") },
                                { type: "scaleWeight", cat: 1, label: TranslationManager.translate("layoutEditor.widgetScaleWeight", "Scale Weight") },
                                { type: "temperature", cat: 1, label: TranslationManager.translate("layoutEditor.widgetTemperature", "Temperature") },
                                { type: "steamTemperature", cat: 1, label: TranslationManager.translate("layoutEditor.widgetSteamTemp", "Steam Temp") },
                                { type: "batteryLevel", cat: 1, label: TranslationManager.translate("layoutEditor.widgetBatteryLevel", "Battery Level") },
                                { type: "scaleBattery", cat: 1, label: TranslationManager.translate("layoutEditor.widgetScaleBattery", "Scale Battery") },
                                { type: "waterLevel", cat: 1, label: TranslationManager.translate("layoutEditor.widgetWaterLevel", "Water Level") },
                                { type: "profileName", cat: 1, label: TranslationManager.translate("layoutEditor.widgetProfileName", "Profile Name") },
                                { type: "doseWeight", cat: 1, label: TranslationManager.translate("layoutEditor.widgetDoseWeight", "Dose Weight") },
                                { type: "milkWeight", cat: 1, label: TranslationManager.translate("layoutEditor.widgetMilkWeight", "Milk Weight") },
                                { type: "ratioQuickSelect", cat: 1, label: TranslationManager.translate("layoutEditor.widgetRatioQuickSelect", "Ratio Quick-Select") },
                                { type: "shotPlan", cat: 1, label: TranslationManager.translate("layoutEditor.widgetShotPlan", "Shot Plan") },
                                { type: "clock", cat: 1, label: TranslationManager.translate("layoutEditor.widgetClock", "Clock") },
                                // Utility (2)
                                { type: "custom", cat: 2, label: TranslationManager.translate("layoutEditor.widgetCustom", "Custom") },
                                { type: "pageTitle", cat: 2, label: TranslationManager.translate("layoutEditor.widgetPageTitle", "Page Title") },
                                { type: "separator", cat: 2, label: TranslationManager.translate("layoutEditor.widgetSeparator", "Separator") },
                                { type: "spacer", cat: 2, label: TranslationManager.translate("layoutEditor.widgetSpacer", "Spacer") },
                                { type: "weather", cat: 2, label: TranslationManager.translate("layoutEditor.widgetWeather", "Weather") },
                                // Screensavers (3)
                                { type: "screensaverPipes", cat: 3, label: TranslationManager.translate("layoutEditor.widget3DPipes", "3D Pipes") },
                                { type: "screensaverAttractor", cat: 3, label: TranslationManager.translate("layoutEditor.widgetAttractors", "Attractors") },
                                { type: "screensaverFlipClock", cat: 3, label: TranslationManager.translate("layoutEditor.widgetFlipClock", "Flip Clock") },
                                { type: "lastShot", cat: 3, label: TranslationManager.translate("layoutEditor.widgetLastShot", "Last Shot") },
                                { type: "screensaverShotMap", cat: 3, label: TranslationManager.translate("layoutEditor.widgetShotMap", "Shot Map") }
                            ]

                            // Filtered + sorted (by category, then label) model,
                            // with explicit header rows so grouping works with a
                            // plain JS-array model (section.property needs roles).
                            model: {
                                var f = widgetFilter.text.trim().toLowerCase()
                                var list = []
                                for (var i = 0; i < catalog.length; i++) {
                                    var e = catalog[i]
                                    if (f === "" || e.label.toLowerCase().indexOf(f) >= 0)
                                        list.push(e)
                                }
                                list.sort(function(a, b) {
                                    return a.cat !== b.cat ? a.cat - b.cat : a.label.localeCompare(b.label)
                                })
                                var out = []
                                var lastCat = -1
                                for (var j = 0; j < list.length; j++) {
                                    if (list[j].cat !== lastCat) {
                                        out.push({ isHeader: true, label: catNames[list[j].cat], type: "", cat: list[j].cat })
                                        lastCat = list[j].cat
                                    }
                                    out.push({ isHeader: false, label: list[j].label, type: list[j].type, cat: list[j].cat })
                                }
                                return out
                            }

                            delegate: Rectangle {
                                width: addListView.width
                                height: modelData.isHeader ? Theme.scaled(24) : Theme.scaled(36)
                                color: (!modelData.isHeader && delegateMa.containsMouse) ? Qt.rgba(Theme.primaryColor.r, Theme.primaryColor.g, Theme.primaryColor.b, 0.12) : "transparent"
                                radius: Theme.scaled(4)

                                Accessible.role: modelData.isHeader ? Accessible.StaticText : Accessible.MenuItem
                                Accessible.name: modelData.isHeader
                                    ? modelData.label
                                    : TranslationManager.translate("layoutEditor.addWidget", "Add %1").arg(modelData.label)
                                Accessible.focusable: !modelData.isHeader
                                Accessible.onPressAction: {
                                    if (modelData.isHeader) return
                                    root.addItemRequested(modelData.type)
                                    addPopup.close()
                                }

                                // Category header
                                Text {
                                    visible: modelData.isHeader
                                    anchors.left: parent.left
                                    anchors.leftMargin: Theme.scaled(10)
                                    anchors.bottom: parent.bottom
                                    text: modelData.label
                                    color: Theme.textSecondaryColor
                                    font: Theme.captionFont
                                }

                                // Widget row
                                Text {
                                    visible: !modelData.isHeader
                                    anchors.left: parent.left
                                    anchors.leftMargin: Theme.scaled(12)
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: modelData.label
                                    color: (modelData.type.startsWith("screensaver") || modelData.type === "lastShot") ? "#64B5F6"
                                        : (modelData.type === "spacer" || modelData.type === "separator" || modelData.type === "custom" || modelData.type === "weather") ? "orange" : Theme.textColor
                                    font: Theme.bodyFont
                                }

                                MouseArea {
                                    id: delegateMa
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    enabled: !modelData.isHeader
                                    onClicked: {
                                        root.addItemRequested(modelData.type)
                                        addPopup.close()
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Tap on zone background to select as target or move item here
    MouseArea {
        anchors.fill: parent
        z: -1
        onClicked: root.zoneTapped()
    }

    // Overlay that hosts a chip while it is being dragged, so it renders above
    // its siblings and can move outside the Flow's layout flow.
    Item {
        id: dragLayer
        anchors.fill: parent
        z: 100
    }

    // Build a short label for a text item chip from its content/action
    function getTextChipLabel(item) {
        var content = item.content || ""

        // Strip HTML tags to get plain text
        var plain = content.replace(/<[^>]*>/g, "").trim()

        // Replace variables with short readable labels
        var varLabels = {
            "%TEMP%": "92\u00B0", "%STEAM_TEMP%": "155\u00B0",
            "%PRESSURE%": "9bar", "%FLOW%": "2.1ml",
            "%WATER%": "78%", "%WATER_ML%": "850ml",
            "%WEIGHT%": "36g", "%SHOT_TIME%": "28s",
            "%TARGET_WEIGHT%": "36g", "%VOLUME%": "42ml",
            "%PROFILE%": "Profile", "%STATE%": "Idle",
            "%TARGET_TEMP%": "93\u00B0", "%SCALE%": "Scale",
            "%TIME%": "14:30", "%DATE%": "2025-01",
            "%RATIO%": "2.0", "%DOSE%": "18g",
            "%CONNECTED%": "Online", "%CONNECTED_COLOR%": "",
            "%DEVICES%": "Devices"
        }
        for (var token in varLabels) {
            if (plain.indexOf(token) >= 0)
                plain = plain.replace(new RegExp(token.replace(/%/g, "\\%"), "g"), varLabels[token])
        }

        if (plain && plain !== "Text" && plain !== "Custom") {
            return plain.length > 14 ? plain.substring(0, 12) + ".." : plain
        }

        // Fall back to action target if content is just "Text"
        var action = item.action || ""
        if (action) {
            var actionLabels = {
                "navigate:settings": "Settings", "navigate:history": "History",
                "navigate:profiles": "Profiles", "navigate:autofavorites": "Favorites",
                "navigate:visualizer": "Visualizer", "navigate:recipes": "Recipes",
                "command:sleep": "Sleep", "command:quit": "Quit",
                "command:startEspresso": "Espresso", "command:startSteam": "Steam",
                "command:startHotWater": "Hot Water", "command:startFlush": "Flush",
                "command:tare": "Tare", "command:idle": "Stop"
            }
            if (actionLabels[action]) return actionLabels[action]
        }

        return "Custom"
    }

    function getItemDisplayName(type) {
        var names = {
            "espresso": TranslationManager.translate("layoutEditor.chipEspresso", "Espresso"),
            "steam": TranslationManager.translate("layoutEditor.chipSteam", "Steam"),
            "hotwater": TranslationManager.translate("layoutEditor.chipHotWater", "Hot Water"),
            "flush": TranslationManager.translate("layoutEditor.chipFlush", "Flush"),
            "beans": TranslationManager.translate("layoutEditor.chipBeans", "Beans"),
            "equipment": TranslationManager.translate("layoutEditor.chipEquipment", "Equipment"),
            "history": TranslationManager.translate("layoutEditor.chipHistory", "History"),
            "autofavorites": TranslationManager.translate("layoutEditor.chipFavorites", "Favorites"),
            "sleep": TranslationManager.translate("layoutEditor.chipSleep", "Sleep"),
            "settings": TranslationManager.translate("layoutEditor.chipSettings", "Settings"),
            "temperature": TranslationManager.translate("layoutEditor.chipTemp", "Temp"),
            "steamTemperature": TranslationManager.translate("layoutEditor.chipSteamTemp", "Steam Temp"),
            "batteryLevel": TranslationManager.translate("layoutEditor.chipBattery", "Battery"),
            "waterLevel": TranslationManager.translate("layoutEditor.chipWater", "Water"),
            "connectionStatus": TranslationManager.translate("layoutEditor.chipMachine", "Machine"),
            "machineStatus": TranslationManager.translate("layoutEditor.chipMachine", "Machine"),
            "scaleWeight": TranslationManager.translate("layoutEditor.chipScale", "Scale"),
            "profileName": TranslationManager.translate("layoutEditor.chipProfileName", "Profile"),
            "doseWeight": TranslationManager.translate("layoutEditor.chipDoseWeight", "Dose"),
            "milkWeight": TranslationManager.translate("layoutEditor.chipMilkWeight", "Milk"),
            "ratioQuickSelect": TranslationManager.translate("layoutEditor.chipRatioQuick", "Ratio"),
            "scaleBattery": TranslationManager.translate("layoutEditor.chipScaleBat", "Scale Bat"),
            "ghcSimulator": TranslationManager.translate("layoutEditor.chipGHCSim", "Mini GHC"),
            "shotPlan": TranslationManager.translate("layoutEditor.chipShotPlan", "Shot Plan"),
            "pageTitle": TranslationManager.translate("layoutEditor.chipPageTitle", "Page Title"),
            "spacer": TranslationManager.translate("layoutEditor.chipSpacer", "Spacer"),
            "separator": TranslationManager.translate("layoutEditor.chipSep", "Sep"),
            "custom": TranslationManager.translate("layoutEditor.chipCustom", "Custom"),
            "weather": TranslationManager.translate("layoutEditor.chipWeather", "Weather"),
            "lastShot": TranslationManager.translate("layoutEditor.chipLastShot", "Last Shot"),
            "screensaverFlipClock": TranslationManager.translate("layoutEditor.chipClock", "Clock"),
            "screensaverPipes": TranslationManager.translate("layoutEditor.chipPipes", "Pipes"),
            "screensaverAttractor": TranslationManager.translate("layoutEditor.chipAttractor", "Attractor"),
            "screensaverShotMap": TranslationManager.translate("layoutEditor.chipMap", "Map"),
            "quit": TranslationManager.translate("layoutEditor.chipQuit", "Quit"),
            "discuss": TranslationManager.translate("layoutEditor.chipDiscuss", "Discuss"),
            "clock": TranslationManager.translate("layoutEditor.chipClock2", "Clock")
        }
        return names[type] || type
    }

}
