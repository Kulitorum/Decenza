import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQml.Models
import Decenza
import ".."
import "ShotPlanConfig.js" as ShotPlanConfig

Dialog {
    id: popup

    property string itemId: ""
    property string zoneName: ""
    property string itemType: ""
    property real clockScale: 1.0  // 0.0 = small (fit width), 1.0 = large (fit height)
    property real mapScale: 1.0    // 1.0 = standard width, 1.7 = wide
    property string mapTexture: "" // "" = use global, "dark", "bright", "satellite"
    property real shotScale: 1.0   // 1.0 = standard width, 2.5 = wide
    property bool shotShowLabels: false  // Show axis labels on graph
    property bool shotShowPhaseLabels: true  // Show frame transition labels
    // Shot Plan working copy — edited by the chip bar, written on Save only
    // (Cancel discards). shotPlanItems is the ordered display-item list;
    // legacy shotPlanShow* booleans are only read (via ShotPlanConfig.itemsFor)
    // to seed it for configs saved before the chip editor.
    property var shotPlanItems: []
    property bool shotPlanSentence: true
    property bool shotPlanStacked: false
    property bool shotPlanYieldTargetOnly: false
    property bool shotPlanShowSteamPlan: true
    // Screen-reader-only reorder fallback (drag has no assistive-tech equivalent).
    readonly property bool _a11yEnabled: typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled
    // True while a chip drag is in progress — gates the reorder commit on
    // release and the DelegateModel rollback on cancel (styling binds to the
    // MouseArea's drag.active directly).
    property bool _planDragging: false

    readonly property var _planAvailable: {
        var avail = []
        for (var i = 0; i < ShotPlanConfig.allKeys.length; i++) {
            if (shotPlanItems.indexOf(ShotPlanConfig.allKeys[i]) === -1)
                avail.push(ShotPlanConfig.allKeys[i])
        }
        return avail
    }

    function planItemLabel(key) {
        var _ = TranslationManager.translationVersion
        switch (key) {
        // Profile & temperature are independent items now — new keys so stale
        // translations of the old combined label can't mislabel them.
        case "profile":     return TranslationManager.translate("shotPlanEditor.itemProfile", "Profile")
        case "temperature": return TranslationManager.translate("shotPlanEditor.itemTemperature", "Temperature")
        case "roaster":     return TranslationManager.translate("shotPlanEditor.showRoaster", "Roaster")
        case "coffee":      return TranslationManager.translate("shotPlanEditor.showCoffee", "Coffee")
        case "grind":       return TranslationManager.translate("shotPlanEditor.showGrindRpm", "Grind")
        case "roastDate":   return TranslationManager.translate("shotPlanEditor.showRoastDate", "Roast date")
        case "doseYield":   return TranslationManager.translate("shotPlanEditor.showDoseYield", "Dose & yield")
        }
        return key
    }

    function planMoveItem(fromIndex, toIndex) {
        if (fromIndex === toIndex || fromIndex < 0 || toIndex < 0
                || fromIndex >= shotPlanItems.length || toIndex >= shotPlanItems.length)
            return
        var items = shotPlanItems.slice()
        items.splice(toIndex, 0, items.splice(fromIndex, 1)[0])
        shotPlanItems = items
    }

    function planRemoveItem(key) {
        shotPlanItems = shotPlanItems.filter(function(k) { return k !== key })
    }

    function planAddItem(key) {
        if (shotPlanItems.indexOf(key) === -1)
            shotPlanItems = shotPlanItems.concat([key])
    }

    readonly property bool hasSettings: itemType === "screensaverFlipClock" || itemType === "screensaverShotMap" || itemType === "lastShot" || itemType === "shotPlan"

    signal saved()

    function openForItem(id, zone, props) {
        itemId = id
        zoneName = zone
        itemType = props.type || ""
        // Migrate from old fitMode string to numeric scale
        if (typeof props.clockScale === "number") {
            clockScale = props.clockScale
        } else if (props.fitMode === "width") {
            clockScale = 0.0
        } else {
            clockScale = 1.0
        }
        mapScale = typeof props.mapScale === "number" ? props.mapScale : 1.0
        mapTexture = typeof props.mapTexture === "string" ? props.mapTexture : ""
        shotScale = typeof props.shotScale === "number" ? props.shotScale : 1.0
        shotShowLabels = typeof props.shotShowLabels === "boolean" ? props.shotShowLabels : false
        shotShowPhaseLabels = typeof props.shotShowPhaseLabels === "boolean" ? props.shotShowPhaseLabels : true
        shotPlanItems = ShotPlanConfig.itemsFor(props)
        shotPlanSentence = typeof props.shotPlanSentence === "boolean" ? props.shotPlanSentence : true
        shotPlanStacked = props.shotPlanStacked === true
        shotPlanYieldTargetOnly = props.shotPlanYieldTargetOnly === true
        shotPlanShowSteamPlan = typeof props.shotPlanShowSteamPlan === "boolean" ? props.shotPlanShowSteamPlan : true
        open()
    }

    modal: true
    closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside
    padding: Theme.spacingMedium

    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    // The shot plan's chip bar + preview need more room than the toggle lists.
    width: Math.min(Theme.scaled(itemType === "shotPlan" ? 560 : 320), parent.width - Theme.spacingSmall * 2)
    // Cap to the window so Save/Cancel stay reachable; the settings sections
    // scroll inside settingsFlick when they don't fit (the shot plan editor is
    // the tall one). Same pattern as CustomEditorPopup / ReadoutOptionsPopup.
    height: Math.min(content.implicitHeight + padding * 2, parent.height * 0.85)

    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.color: Theme.borderColor
        border.width: 1
    }

    function save() {
        if (itemType === "screensaverFlipClock")
            Settings.network.setItemProperty(itemId, "clockScale", clockScale)
        if (itemType === "screensaverShotMap") {
            Settings.network.setItemProperty(itemId, "mapScale", mapScale)
            Settings.network.setItemProperty(itemId, "mapTexture", mapTexture)
        }
        if (itemType === "lastShot") {
            Settings.network.setItemProperty(itemId, "shotScale", shotScale)
            Settings.network.setItemProperty(itemId, "shotShowLabels", shotShowLabels)
            Settings.network.setItemProperty(itemId, "shotShowPhaseLabels", shotShowPhaseLabels)
        }
        if (itemType === "shotPlan") {
            // New keys only — the six legacy shotPlanShow* item booleans are
            // read-time migration input and are never written back
            // (shotPlanShowSteamPlan is a live key, not one of them).
            // Typed call: a JS array through the generic QVariant setter arrives
            // as a QJSValue and would be stored as null (see settings_network.h).
            var ok = Settings.network.setItemPropertyList(itemId, "shotPlanItems", shotPlanItems)
            ok = Settings.network.setItemProperty(itemId, "shotPlanSentence", shotPlanSentence) && ok
            ok = Settings.network.setItemProperty(itemId, "shotPlanStacked", shotPlanStacked) && ok
            ok = Settings.network.setItemProperty(itemId, "shotPlanYieldTargetOnly", shotPlanYieldTargetOnly) && ok
            ok = Settings.network.setItemProperty(itemId, "shotPlanShowSteamPlan", shotPlanShowSteamPlan) && ok
            if (!ok)
                console.warn("ScreensaverEditorPopup: shot plan save failed (item deleted?)", itemId)
        }
        saved()
        close()
    }

    // Overlay that hosts a shot-plan chip while it is being dragged, so it
    // renders above its siblings and can move outside the Flow's layout flow.
    Item {
        id: planDragLayer
        anchors.fill: parent
        z: 100
    }

    ColumnLayout {
        id: content
        anchors.fill: parent
        spacing: Theme.spacingMedium

        // Title
        Text {
            text: {
                switch (popup.itemType) {
                    case "screensaverFlipClock": return TranslationManager.translate("screensaverEditor.title.flipClock", "Flip Clock Settings")
                    case "screensaverPipes": return TranslationManager.translate("screensaverEditor.title.pipes", "3D Pipes Settings")
                    case "screensaverAttractor": return TranslationManager.translate("screensaverEditor.title.attractor", "Attractors Settings")
                    case "screensaverShotMap": return TranslationManager.translate("screensaverEditor.title.shotMap", "Shot Map Settings")
                    case "lastShot": return TranslationManager.translate("screensaverEditor.title.lastShot", "Last Shot Settings")
                    case "shotPlan": return TranslationManager.translate("screensaverEditor.title.shotPlan", "Shot Plan Settings")
                    default: return TranslationManager.translate("screensaverEditor.title.default", "Screensaver Settings")
                }
            }
            font.family: Theme.titleFont.family
            font.pixelSize: Theme.titleFont.pixelSize
            font.bold: true
            color: Theme.textColor
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
        }

        // Separator
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.borderColor
        }

        Flickable {
            id: settingsFlick
            Layout.fillWidth: true
            Layout.fillHeight: true
            implicitHeight: settingsCol.implicitHeight
            contentWidth: width
            contentHeight: settingsCol.implicitHeight
            clip: true
            flickableDirection: Flickable.VerticalFlick
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: ScrollBar {
                policy: settingsFlick.contentHeight > settingsFlick.height
                    ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
            }

            ColumnLayout {
                id: settingsCol
                width: settingsFlick.width
                spacing: Theme.spacingMedium

                // Size slider (only for flip clock)
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSmall
                    visible: popup.itemType === "screensaverFlipClock"

                    Text {
                        text: TranslationManager.translate("screensaverEditor.label.size", "Size")
                        font: Theme.labelFont
                        color: Theme.textSecondaryColor
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSmall

                        Text {
                            text: TranslationManager.translate("screensaverEditor.size.small", "Small")
                            font: Theme.captionFont
                            color: Theme.textSecondaryColor
                        }

                        Slider {
                            id: sizeSlider
                            Layout.fillWidth: true
                            from: 0.0
                            to: 1.0
                            stepSize: 0.05
                            value: popup.clockScale
                            onMoved: popup.clockScale = value
                        }

                        Text {
                            text: TranslationManager.translate("screensaverEditor.size.large", "Large")
                            font: Theme.captionFont
                            color: Theme.textSecondaryColor
                        }
                    }
                }

                // Width slider (only for shot map)
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSmall
                    visible: popup.itemType === "screensaverShotMap"

                    Text {
                        text: TranslationManager.translate("screensaverEditor.label.width", "Width")
                        font: Theme.labelFont
                        color: Theme.textSecondaryColor
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSmall

                        Text {
                            text: TranslationManager.translate("screensaverEditor.width.narrow", "Narrow")
                            font: Theme.captionFont
                            color: Theme.textSecondaryColor
                        }

                        Slider {
                            id: mapWidthSlider
                            Layout.fillWidth: true
                            from: 1.0
                            to: 1.7
                            stepSize: 0.05
                            value: popup.mapScale
                            onMoved: popup.mapScale = value
                        }

                        Text {
                            text: TranslationManager.translate("screensaverEditor.width.wide", "Wide")
                            font: Theme.captionFont
                            color: Theme.textSecondaryColor
                        }
                    }
                }

                // Map texture picker (only for shot map)
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSmall
                    visible: popup.itemType === "screensaverShotMap"

                    Text {
                        text: TranslationManager.translate("screensaverEditor.label.background", "Background")
                        font: Theme.labelFont
                        color: Theme.textSecondaryColor
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSmall

                        Repeater {
                            model: [
                                { value: "",          label: TranslationManager.translate("screensaverEditor.textureGlobal", "Global") },
                                { value: "dark",      label: TranslationManager.translate("screensaverEditor.textureDark", "Dark") },
                                { value: "bright",    label: TranslationManager.translate("screensaverEditor.textureBright", "Bright") },
                                { value: "satellite", label: TranslationManager.translate("screensaverEditor.textureSatellite", "Satellite") }
                            ]

                            Rectangle {
                                Layout.fillWidth: true
                                height: Theme.scaled(32)
                                radius: Theme.scaled(6)
                                color: popup.mapTexture === modelData.value
                                    ? Theme.primaryColor
                                    : "transparent"
                                border.color: popup.mapTexture === modelData.value
                                    ? Theme.primaryColor
                                    : Theme.borderColor
                                border.width: 1

                                Accessible.role: Accessible.Button
                                Accessible.name: modelData.label + (popup.mapTexture === modelData.value ? ", selected" : "")
                                Accessible.focusable: true
                                Accessible.onPressAction: mapTextureArea.clicked(null)

                                Text {
                                    anchors.centerIn: parent
                                    text: modelData.label
                                    font: Theme.captionFont
                                    color: popup.mapTexture === modelData.value
                                        ? Theme.primaryContrastColor
                                        : Theme.textColor
                                    Accessible.ignored: true
                                }

                                MouseArea {
                                    id: mapTextureArea
                                    anchors.fill: parent
                                    onClicked: popup.mapTexture = modelData.value
                                }
                            }
                        }
                    }
                }

                // Width slider (only for last shot)
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSmall
                    visible: popup.itemType === "lastShot"

                    Text {
                        text: TranslationManager.translate("screensaverEditor.label.width", "Width")
                        font: Theme.labelFont
                        color: Theme.textSecondaryColor
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSmall

                        Text {
                            text: "1x"
                            font: Theme.captionFont
                            color: Theme.textSecondaryColor
                        }

                        Slider {
                            id: shotWidthSlider
                            Layout.fillWidth: true
                            from: 1.0
                            to: 2.5
                            stepSize: 0.1
                            value: popup.shotScale
                            onMoved: popup.shotScale = value
                        }

                        Text {
                            text: "2.5x"
                            font: Theme.captionFont
                            color: Theme.textSecondaryColor
                        }
                    }
                }

                // Labels toggle (only for last shot)
                StyledSwitch {
                    visible: popup.itemType === "lastShot"
                    text: TranslationManager.translate("screensaverEditor.label.showAxisLabels", "Show axis labels")
                    checked: popup.shotShowLabels
                    onToggled: popup.shotShowLabels = checked
                }

                // Frame labels toggle (only for last shot)
                StyledSwitch {
                    visible: popup.itemType === "lastShot"
                    text: TranslationManager.translate("screensaverEditor.label.showFrameLabels", "Show frame labels")
                    checked: popup.shotShowPhaseLabels
                    onToggled: popup.shotShowPhaseLabels = checked
                }

                // Shot plan item chips: an ordered "Shown" row (drag to reorder, ✕ to
                // remove) and an "Available" row (tap to add) — the layout-page chip
                // interaction, applied to the plan's display items. All edits hit the
                // popup's working copy only; save() persists, Cancel discards.
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSmall
                    visible: popup.itemType === "shotPlan"

                    Text {
                        text: TranslationManager.translate("shotPlanEditor.shownItems", "Shown (drag to reorder)")
                        font: Theme.labelFont
                        color: Theme.textSecondaryColor
                    }

                    Flow {
                        id: shownFlow
                        Layout.fillWidth: true
                        spacing: Theme.spacingSmall

                        // Animate chips sliding out of the way during a drag reorder.
                        move: Transition {
                            NumberAnimation { properties: "x,y"; duration: 180; easing.type: Easing.OutQuad }
                        }

                        Repeater {
                            model: DelegateModel {
                                id: planVisualModel
                                model: popup.shotPlanItems

                                delegate: Item {
                                    id: planChip
                                    width: planChipBody.width
                                    height: planChipBody.height

                                    readonly property int liveIndex: DelegateModel.itemsIndex
                                    readonly property string itemKey: modelData

                                    Rectangle {
                                        id: planChipBody
                                        width: planChipRow.implicitWidth + Theme.scaled(16)
                                        height: Theme.scaled(36)
                                        radius: Theme.scaled(8)
                                        color: Theme.backgroundColor
                                        border.color: planDragMa.drag.active ? Theme.primaryColor : Theme.borderColor
                                        border.width: planDragMa.drag.active ? 2 : 1
                                        scale: planDragMa.drag.active ? 1.05 : 1.0
                                        opacity: planDragMa.drag.active ? 0.95 : 1.0
                                        z: planDragMa.drag.active ? 100 : 1

                                        Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutQuad } }

                                        Drag.active: planDragMa.drag.active
                                        Drag.source: planChip
                                        Drag.hotSpot.x: width / 2
                                        Drag.hotSpot.y: height / 2

                                        // Lift out to the drag layer while dragging so the
                                        // chip renders above siblings and moves freely.
                                        states: State {
                                            when: planDragMa.drag.active
                                            ParentChange { target: planChipBody; parent: planDragLayer }
                                        }

                                        // The chip container itself has no tap action — its
                                        // actions are the child buttons (a11y move + remove) —
                                        // so it's a labeled static element, not a Button.
                                        Accessible.role: Accessible.StaticText
                                        Accessible.name: TranslationManager.translate("shotPlanEditor.shownChip", "%1, shown, position %2 of %3")
                                            .arg(popup.planItemLabel(planChip.itemKey))
                                            .arg(planChip.liveIndex + 1)
                                            .arg(popup.shotPlanItems.length)
                                        Accessible.focusable: true

                                        RowLayout {
                                            id: planChipRow
                                            anchors.centerIn: parent
                                            spacing: Theme.scaled(4)

                                            // Accessible-only reorder fallback (drag has no
                                            // screen-reader equivalent). Hidden in normal use.
                                            StyledIconButton {
                                                visible: popup._a11yEnabled && planChip.liveIndex > 0
                                                implicitWidth: Theme.scaled(28)
                                                implicitHeight: Theme.scaled(28)
                                                icon.source: "qrc:/icons/ArrowLeft.svg"
                                                icon.width: Theme.scaled(14)
                                                icon.height: Theme.scaled(14)
                                                accessibleName: TranslationManager.translate("layoutEditor.moveToStart", "Move toward start")
                                                onClicked: popup.planMoveItem(planChip.liveIndex, planChip.liveIndex - 1)
                                            }

                                            Text {
                                                text: popup.planItemLabel(planChip.itemKey)
                                                color: Theme.textColor
                                                font: Theme.bodyFont
                                                Accessible.ignored: true
                                            }

                                            // Accessible-only reorder fallback (toward end).
                                            StyledIconButton {
                                                visible: popup._a11yEnabled && planChip.liveIndex < popup.shotPlanItems.length - 1
                                                implicitWidth: Theme.scaled(28)
                                                implicitHeight: Theme.scaled(28)
                                                icon.source: "qrc:/icons/ArrowLeft.svg"
                                                icon.width: Theme.scaled(14)
                                                icon.height: Theme.scaled(14)
                                                rotation: 180
                                                accessibleName: TranslationManager.translate("layoutEditor.moveToEnd", "Move toward end")
                                                onClicked: popup.planMoveItem(planChip.liveIndex, planChip.liveIndex + 1)
                                            }

                                            // Remove — sends the item to the Available row.
                                            StyledIconButton {
                                                implicitWidth: Theme.scaled(28)
                                                implicitHeight: Theme.scaled(28)
                                                icon.source: "qrc:/icons/cross.svg"
                                                icon.width: Theme.scaled(14)
                                                icon.height: Theme.scaled(14)
                                                inactiveColor: Theme.errorColor
                                                activeColor: Theme.errorColor
                                                active: true
                                                opacity: planChipHover.hovered ? 1.0 : 0.4
                                                accessibleName: TranslationManager.translate("shotPlanEditor.removeItem", "Hide %1").arg(popup.planItemLabel(planChip.itemKey))
                                                onClicked: popup.planRemoveItem(planChip.itemKey)
                                                Behavior on opacity { NumberAnimation { duration: 100 } }
                                            }
                                        }

                                        HoverHandler { id: planChipHover }

                                        // Drag to reorder. preventStealing keeps any
                                        // enclosing flickable from hijacking the drag.
                                        MouseArea {
                                            id: planDragMa
                                            anchors.fill: parent
                                            z: -1
                                            drag.target: planChipBody
                                            drag.threshold: Theme.scaled(8)
                                            preventStealing: true

                                            property int _startIndex: -1

                                            onPressed: _startIndex = planChip.liveIndex
                                            onPositionChanged: {
                                                if (drag.active) popup._planDragging = true
                                            }
                                            onReleased: {
                                                if (popup._planDragging) {
                                                    var endIndex = planChip.liveIndex
                                                    if (_startIndex >= 0 && endIndex !== _startIndex)
                                                        popup.planMoveItem(_startIndex, endIndex)
                                                }
                                                popup._planDragging = false
                                                _startIndex = -1
                                            }
                                            onCanceled: {
                                                // Roll back any live swaps so the DelegateModel
                                                // order matches the unchanged working list.
                                                if (popup._planDragging) {
                                                    var cur = planChip.liveIndex
                                                    if (_startIndex >= 0 && cur !== _startIndex)
                                                        planVisualModel.items.move(cur, _startIndex, 1)
                                                }
                                                popup._planDragging = false
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
                                            if (!src || src === planChip) return
                                            var from = src.liveIndex
                                            var to = planChip.liveIndex
                                            if (from !== to) planVisualModel.items.move(from, to, 1)
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Text {
                        visible: popup._planAvailable.length > 0
                        text: TranslationManager.translate("shotPlanEditor.availableItems", "Available")
                        font: Theme.labelFont
                        color: Theme.textSecondaryColor
                    }

                    Flow {
                        Layout.fillWidth: true
                        spacing: Theme.spacingSmall
                        visible: popup._planAvailable.length > 0

                        Repeater {
                            model: popup._planAvailable

                            Rectangle {
                                width: availChipRow.implicitWidth + Theme.scaled(16)
                                height: Theme.scaled(36)
                                radius: Theme.scaled(8)
                                color: "transparent"
                                border.color: Theme.borderColor
                                border.width: 1

                                Accessible.role: Accessible.Button
                                Accessible.name: TranslationManager.translate("shotPlanEditor.addItem", "Show %1").arg(popup.planItemLabel(modelData))
                                Accessible.focusable: true
                                Accessible.onPressAction: popup.planAddItem(modelData)

                                RowLayout {
                                    id: availChipRow
                                    anchors.centerIn: parent
                                    spacing: Theme.scaled(4)

                                    Text {
                                        text: popup.planItemLabel(modelData)
                                        color: Theme.textSecondaryColor
                                        font: Theme.bodyFont
                                        Accessible.ignored: true
                                    }
                                    ColoredIcon {
                                        source: "qrc:/icons/plus.svg"
                                        iconWidth: Theme.scaled(14)
                                        iconHeight: Theme.scaled(14)
                                        iconColor: Theme.primaryColor
                                        Accessible.ignored: true
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: popup.planAddItem(modelData)
                                }
                            }
                        }
                    }

                    StyledSwitch {
                        // ON, profile anchor available (Profile shown with a profile loaded):
                        // the "Brew … of Espresso, using … at …" scaffold (its own word order)
                        // with the remaining items trailing in chip order. ON, no profile anchor
                        // (Profile removed, or none loaded): the profile-less "recipe" sentence —
                        // "Brew 40.0g of Espresso at 92°C from 18.0g of <Roaster> <Bean>" —
                        // consuming dose/temperature/roaster/coffee. OFF: all items as separator-
                        // joined fragments in chip order. The preview reflects each case live.
                        text: TranslationManager.translate("shotPlanEditor.sentenceStyle", "Sentence style")
                        checked: popup.shotPlanSentence
                        onToggled: popup.shotPlanSentence = checked
                    }
                    StyledSwitch {
                        // Sentence mode only: the detail tail moves to its own line(s)
                        // below the sentence. Meaningless for fragments (there is no
                        // sentence/tail split), hence disabled when Sentence is off.
                        // Compact bar placements ignore it at render time.
                        text: TranslationManager.translate("shotPlanEditor.stackedDetails", "Stacked details")
                        enabled: popup.shotPlanSentence
                        checked: popup.shotPlanStacked
                        onToggled: popup.shotPlanStacked = checked
                    }
                    StyledSwitch {
                        // Yield display: ON shows only the effective target yield (e.g. "40.0g");
                        // OFF keeps the "profileDefault → target" arrow (e.g. "36.0 → 40.0g").
                        // Only affects the Dose & yield item, so disabled when it isn't shown —
                        // and only visibly differs while a deliberate yield override is active.
                        text: TranslationManager.translate("shotPlanEditor.yieldTargetOnly", "Final yield only (hide profile default)")
                        enabled: popup.shotPlanItems.indexOf("doseYield") !== -1
                        checked: popup.shotPlanYieldTargetOnly
                        onToggled: popup.shotPlanYieldTargetOnly = checked
                    }
                    StyledSwitch {
                        // Page-aware mode: while steaming (or steam selected) the widget swaps to the
                        // steam sentence. The steam side has no further options.
                        text: TranslationManager.translate("shotPlanEditor.showSteamPlan", "Steam plan (while steaming)")
                        checked: popup.shotPlanShowSteamPlan
                        onToggled: popup.shotPlanShowSteamPlan = checked
                    }

                    Text {
                        text: TranslationManager.translate("shotPlanEditor.preview", "Preview")
                        font: Theme.labelFont
                        color: Theme.textSecondaryColor
                    }

                    // Live preview of the plan as configured in this dialog (working
                    // copy, not the saved state). Read-only: its clicked() is unconnected.
                    Item {
                        Layout.fillWidth: true
                        implicitHeight: planPreview.implicitHeight + Theme.spacingSmall * 2

                        Rectangle {
                            anchors.fill: parent
                            color: Theme.backgroundColor
                            radius: Theme.scaled(8)
                            border.color: Theme.borderColor
                            border.width: 1
                        }

                        ShotPlanText {
                            id: planPreview
                            anchors.centerIn: parent
                            width: Math.min(implicitWidth, parent.width - Theme.spacingSmall * 2)
                            itemOrder: popup.shotPlanItems
                            sentence: popup.shotPlanSentence
                            yieldTargetOnly: popup.shotPlanYieldTargetOnly
                            stacked: popup.shotPlanStacked
                            // Same sentence gating as ShotPlanItem: no 3-line budget
                            // for fragment mode with a stale stacked flag.
                            maxLines: popup.shotPlanStacked && popup.shotPlanSentence ? 3 : 2
                            Accessible.ignored: true
                        }
                    }
                }

                // No settings message for screensavers without options
                Text {
                    visible: !popup.hasSettings
                    text: TranslationManager.translate("screensaverEditor.noSettings", "No additional settings for this screensaver.")
                    font: Theme.bodyFont
                    color: Theme.textSecondaryColor
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }

        // Buttons stay outside the scroll area so they are always visible.
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall

            Item { Layout.fillWidth: true }

            Rectangle {
                width: Theme.scaled(80)
                height: Theme.scaled(36)
                radius: Theme.cardRadius
                color: Theme.surfaceColor
                border.color: Theme.borderColor
                border.width: 1

                Accessible.role: Accessible.Button
                Accessible.name: TranslationManager.translate("common.button.cancel", "Cancel")
                Accessible.focusable: true
                Accessible.onPressAction: cancelArea.clicked(null)

                Text {
                    anchors.centerIn: parent
                    text: TranslationManager.translate("common.button.cancel", "Cancel")
                    font: Theme.bodyFont
                    color: Theme.textColor
                    Accessible.ignored: true
                }
                MouseArea {
                    id: cancelArea
                    anchors.fill: parent
                    onClicked: popup.close()
                }
            }

            Rectangle {
                width: Theme.scaled(80)
                height: Theme.scaled(36)
                radius: Theme.cardRadius
                color: Theme.primaryColor
                visible: popup.hasSettings

                Accessible.role: Accessible.Button
                Accessible.name: TranslationManager.translate("common.button.save", "Save")
                Accessible.focusable: true
                Accessible.onPressAction: saveArea.clicked(null)

                Text {
                    anchors.centerIn: parent
                    text: TranslationManager.translate("common.button.save", "Save")
                    font: Theme.bodyFont
                    color: Theme.primaryContrastColor
                    Accessible.ignored: true
                }
                MouseArea {
                    id: saveArea
                    anchors.fill: parent
                    onClicked: popup.save()
                }
            }
        }
    }
}
