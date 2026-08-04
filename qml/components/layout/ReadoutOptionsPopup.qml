// The data-mode and display-mode Repeater delegates read this file's `popup` id; Bound
// makes it statically resolvable. Both already declare their one injected role,
// `modelData`, required, so Bound cannot break role injection here.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza

// Unified per-instance editor for readout widgets. The sections shown are
// exactly the option keys the widget type declares in the readout capability
// schema (Settings.network.optionKeysForType): dataMode, displayMode,
// showRatio, color. Persists via Settings.network.setItemProperty.
DecenzaDialog {
    id: popup

    property string itemId: ""
    property string widgetType: ""
    property var optionKeys: []
    property string dataMode: "gross"
    property string displayMode: "text"
    property bool showRatio: true
    // Gesture overrides (layout-widget-gesture-overrides). Empty = no override,
    // which is what keeps an untouched widget behaving exactly as before.
    property string longPressAction: ""
    property string doubleclickAction: ""
    // The action this type's RESERVED gesture performs, "" when both are free.
    // Read from the C++ table — the single declaration of every destination.
    readonly property string reservedAction: Settings.network.gestureReservedActionForType(popup.widgetType)
    // A one-slot widget reserves whichever gesture the user has NOT filled. Once
    // one carries an override the other is locked, so the page stays reachable;
    // clearing the override frees both again.
    // LOCKED is not the same as WHAT IT DOES. An empty gesture on a one-slot
    // widget already opens the page; it only becomes un-editable once the OTHER
    // gesture carries an override, because then it is the last way in.
    readonly property bool longPressLocked: popup.reservedAction !== "" && popup.longPressAction === ""
                                            && popup.doubleclickAction !== ""
    readonly property bool doubleclickLocked: popup.reservedAction !== "" && popup.doubleclickAction === ""
                                              && popup.longPressAction !== ""

    // An absent stored displayMode always means "today's rendering"; the
    // per-type default is declared once in the capability schema.
    function defaultDisplayMode(type) {
        return Settings.network.defaultDisplayModeForType(type)
    }

    function openForItem(id, props) {
        popup.itemId = id
        popup.widgetType = props.type || ""
        popup.optionKeys = Settings.network.optionKeysForType(popup.widgetType)
        popup.dataMode = props.dataMode || "gross"
        popup.displayMode = props.displayMode || defaultDisplayMode(popup.widgetType)
        popup.showRatio = props.showRatio !== undefined ? props.showRatio : true
        popup.longPressAction = props.longPressAction || ""
        popup.doubleclickAction = props.doubleclickAction || ""
        colorPicker.colorChoice = props.color || "default"
        popup.open()
    }

    function hasOption(key) {
        return popup.optionKeys.indexOf(key) >= 0
    }

    function pickDataMode(mode) {
        popup.dataMode = mode
        Settings.network.setItemProperty(popup.itemId, "dataMode", mode)
    }

    function pickDisplay(mode) {
        popup.displayMode = mode
        Settings.network.setItemProperty(popup.itemId, "displayMode", mode)
    }

    // The label a gesture shows: the user's action, the reserved destination, or
    // "None". All three resolve through layoutActionLabels(), so no destination
    // name is written out here.
    // What a gesture row SHOWS. Never "None": an empty slot is not "nothing
    // happens", it is "unchanged". On a widget that reserves a destination an
    // empty gesture already OPENS THAT PAGE — the row says which one, because
    // that is its behaviour today. Elsewhere "Default" says stock behaviour
    // without claiming the gesture is dead.
    function gestureLabel(actionId) {
        if (actionId) {
            var entry = Settings.network.layoutActionLabels()[actionId]
            if (entry === undefined) return actionId
            return TranslationManager.translate(entry.key, entry.fallback)
        }
        if (popup.reservedAction === "")
            return TranslationManager.translate("gesturerow.default", "Default")
        var r = Settings.network.layoutActionLabels()[popup.reservedAction]
        var rLabel = r === undefined ? popup.reservedAction
                                     : TranslationManager.translate(r.key, r.fallback)
        // "Opens Recipes", not "Opens Go to Recipes": the catalog label is written
        // for a picker row and reads as a double verb inside a sentence. The
        // prefix is translated, so strip the translated form rather than "Go to".
        var goTo = TranslationManager.translate("customaction.prefix.goTo", "Go to ")
        if (rLabel.indexOf(goTo) === 0)
            rLabel = rLabel.substring(goTo.length)
        return TranslationManager.translate("gesturerow.opens", "Opens %1").arg(rLabel)
    }

    function setGesture(key, actionId) {
        if (key === "longPressAction") popup.longPressAction = actionId
        else popup.doubleclickAction = actionId
        Settings.network.setItemProperty(popup.itemId, key, actionId)
    }

    function setShowRatio(v) {
        popup.showRatio = v
        Settings.network.setItemProperty(popup.itemId, "showRatio", v)
    }

    readonly property var displayChoices: [
        { value: "text", label: TranslationManager.translate("layoutEditor.displayValueOnly", "Value only") },
        { value: "icon", label: TranslationManager.translate("layoutEditor.displayIcon", "Icon + value") }
    ]

    readonly property var dataModeChoices: [
        { value: "gross",        label: TranslationManager.translate("layoutEditor.scaleGross", "Gross weight") },
        { value: "netBeans",     label: TranslationManager.translate("layoutEditor.scaleNetBeans", "Net beans (minus dose tare)") },
        { value: "netMilk",      label: TranslationManager.translate("layoutEditor.scaleNetMilk", "Net milk (minus pitcher)") },
        { value: "contextAware", label: TranslationManager.translate("layoutEditor.scaleContext", "Context-aware (milk while steaming, else beans)") },
        { value: "expectedYield", label: TranslationManager.translate("layoutEditor.scaleExpectedYield", "Expected output (target weight)") }
    ]

    modal: true
    closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside
    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    width: Math.min(Theme.scaled(520), parent.width - Theme.spacingLarge * 2)
    // Cap to the window so the Done button stays reachable; the option
    // sections scroll inside optionsFlick when they don't fit (the scale
    // weight instance stacks four sections). Same pattern as CustomEditorPopup.
    height: Math.min(implicitHeight, parent.height * 0.85)
    padding: Theme.spacingMedium

    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.color: Theme.borderColor
        border.width: 1
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingMedium

        Flickable {
            id: optionsFlick
            Layout.fillWidth: true
            Layout.fillHeight: true
            implicitHeight: optionsCol.implicitHeight
            contentWidth: width
            contentHeight: optionsCol.implicitHeight
            clip: true
            flickableDirection: Flickable.VerticalFlick
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: ScrollBar {
                policy: optionsFlick.contentHeight > optionsFlick.height
                    ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
            }

            ColumnLayout {
                id: optionsCol
                width: optionsFlick.width
                spacing: Theme.spacingMedium

                Text {
                    visible: popup.hasOption("dataMode")
                    text: TranslationManager.translate("layoutEditor.scaleDataMode", "Scale data mode")
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(20)
                    font.bold: true
                }

                Repeater {
                    model: popup.hasOption("dataMode") ? popup.dataModeChoices : []
                    delegate: Rectangle {
                        id: dataModeCell
                        required property var modelData
                        readonly property bool sel: popup.dataMode === modelData.value
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.scaled(44)
                        radius: Theme.buttonRadius
                        color: sel ? Theme.primaryColor : "transparent"
                        border.color: sel ? Theme.primaryColor : Theme.borderColor
                        border.width: 1
                        Accessible.role: Accessible.Button
                        Accessible.name: modelData.label
                        Accessible.focusable: true
                        Accessible.onPressAction: dataMa.clicked(null)
                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: Theme.spacingMedium
                            anchors.right: parent.right
                            anchors.rightMargin: Theme.spacingMedium
                            anchors.verticalCenter: parent.verticalCenter
                            text: dataModeCell.modelData.label
                            color: dataModeCell.sel ? Theme.primaryContrastColor : Theme.textColor
                            font: Theme.labelFont
                            elide: Text.ElideRight
                        }
                        MouseArea { id: dataMa; anchors.fill: parent; onClicked: popup.pickDataMode(dataModeCell.modelData.value) }
                    }
                }

                Text {
                    visible: popup.hasOption("displayMode")
                    text: TranslationManager.translate("layoutEditor.displayMode", "Display")
                    color: Theme.textColor
                    font.pixelSize: Theme.scaled(20)
                    font.bold: true
                }

                RowLayout {
                    visible: popup.hasOption("displayMode")
                    Layout.fillWidth: true
                    spacing: Theme.spacingSmall
                    Repeater {
                        model: popup.displayChoices
                        delegate: Rectangle {
                            id: displayModeCell
                            required property var modelData
                            readonly property bool sel: popup.displayMode === modelData.value
                            Layout.fillWidth: true
                            Layout.preferredHeight: Theme.scaled(44)
                            radius: Theme.buttonRadius
                            color: sel ? Theme.primaryColor : "transparent"
                            border.color: sel ? Theme.primaryColor : Theme.borderColor
                            border.width: 1
                            Accessible.role: Accessible.Button
                            Accessible.name: modelData.label
                            Accessible.focusable: true
                            Accessible.onPressAction: dispMa.clicked(null)
                            Text {
                                anchors.centerIn: parent
                                text: displayModeCell.modelData.label
                                color: displayModeCell.sel ? Theme.primaryContrastColor : Theme.textColor
                                font: Theme.labelFont
                            }
                            MouseArea { id: dispMa; anchors.fill: parent; onClicked: popup.pickDisplay(displayModeCell.modelData.value) }
                        }
                    }
                }

                RowLayout {
                    visible: popup.hasOption("showRatio")
                    Layout.fillWidth: true
                    spacing: Theme.spacingMedium
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            text: TranslationManager.translate("layoutEditor.scaleShowRatio", "Show ratio")
                            color: Theme.textColor
                            font: Theme.bodyFont
                        }
                        Text {
                            text: TranslationManager.translate("layoutEditor.scaleShowRatioHint", "Off = weight only, no 1:X.X suffix")
                            color: Theme.textSecondaryColor
                            font: Theme.captionFont
                        }
                    }
                    StyledSwitch {
                        accessibleName: TranslationManager.translate("layoutEditor.scaleShowRatio", "Show ratio")
                        checked: popup.showRatio
                        onToggled: popup.setShowRatio(checked)
                    }
                }

                // Gestures (layout-widget-gesture-overrides). Same rows as the
                // Custom widget editor — one GestureActionRow component — and the
                // same action picker, so an action means the same thing wherever
                // it is assigned.
                ColumnLayout {
                    visible: popup.hasOption("longPressAction")
                    Layout.fillWidth: true
                    spacing: Theme.scaled(6)

                    Text {
                        text: TranslationManager.translate("layoutEditor.gestures", "Gestures")
                        color: Theme.textColor
                        font: Theme.bodyFont
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: popup.reservedAction !== ""
                        text: TranslationManager.translate("layoutEditor.gesturesHint",
                                  "One gesture stays reserved so this widget's page is still reachable.")
                        color: Theme.textSecondaryColor
                        font: Theme.captionFont
                        wrapMode: Text.WordWrap
                    }

                    GestureActionRow {
                        gestureLabel: TranslationManager.translate("customeditor.gesture.long", "Long:")
                        accessibleLabel: TranslationManager.translate("customeditor.accessible.longPressAction", "Long press action")
                        actionId: popup.longPressAction
                        actionLabel: popup.gestureLabel(popup.longPressAction)
                        reserved: popup.longPressLocked
                        reservedLabel: popup.gestureLabel("")
                        onPicked: gesturePicker.openFor("longPressAction")
                    }

                    GestureActionRow {
                        gestureLabel: TranslationManager.translate("customeditor.gesture.dblclick", "DblClk:")
                        accessibleLabel: TranslationManager.translate("customeditor.accessible.dblClickAction", "Double click action")
                        actionId: popup.doubleclickAction
                        actionLabel: popup.gestureLabel(popup.doubleclickAction)
                        reserved: popup.doubleclickLocked
                        reservedLabel: popup.gestureLabel("")
                        onPicked: gesturePicker.openFor("doubleclickAction")
                    }
                }

                WidgetColorPicker {
                    id: colorPicker
                    visible: popup.hasOption("color")
                    Layout.fillWidth: true
                    itemId: popup.itemId
                }
            }
        }

        // Done stays outside the scroll area so it is always visible.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.scaled(44)
            radius: Theme.buttonRadius
            color: doneMa.pressed ? Qt.darker(Theme.primaryColor, 1.15) : Theme.primaryColor
            Accessible.role: Accessible.Button
            Accessible.name: TranslationManager.translate("common.button.done", "Done")
            Accessible.focusable: true
            Accessible.onPressAction: doneMa.clicked(null)
            Text {
                anchors.centerIn: parent
                text: TranslationManager.translate("common.button.done", "Done")
                color: Theme.primaryContrastColor
                font: Theme.bodyFont
            }
            MouseArea { id: doneMa; anchors.fill: parent; onClicked: popup.close() }
        }
    }

    // === Gesture action picker ===
    // Same list the Custom widget editor offers, from the same singleton — one
    // implementation of "read the catalog, filter by context, translate".
    SelectionDialog {
        id: gesturePicker
        property string gestureKey: "longPressAction"
        property var _items: []

        function openFor(key) {
            gesturePicker.gestureKey = key
            gesturePicker.open()
        }

        title: gestureKey === "longPressAction"
            ? TranslationManager.translate("customeditor.dialog.longPressAction", "Long Press Action")
            : TranslationManager.translate("customeditor.dialog.doubleClickAction", "Double Click Action")
        options: _items.map(function(i) { return i.label })
        currentIndex: {
            var cur = gesturePicker.gestureKey === "longPressAction" ? popup.longPressAction
                                                                    : popup.doubleclickAction
            for (var i = 0; i < gesturePicker._items.length; i++)
                if (gesturePicker._items[i].id === cur) return i
            return 0
        }

        // Idle context: these widgets live on the idle screen.
        // excludeSubmenu: this popup has no profile sub-picker, so a parameterized
        // action would be stored as a bare id the dispatch rejects.
        onAboutToShow: _items = LayoutActions.pickerItems("idle", true)

        onSelected: function(index, value) {
            popup.setGesture(gesturePicker.gestureKey, gesturePicker._items[index].id)
        }
    }
}
