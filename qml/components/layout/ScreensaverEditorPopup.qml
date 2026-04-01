import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Decenza
import ".."

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
    property bool shotPlanShowProfile: true
    property bool shotPlanShowRoaster: true
    property bool shotPlanShowGrind: true
    property bool shotPlanShowRoastDate: false
    property bool shotPlanShowDoseYield: true

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
        shotPlanShowProfile = typeof props.shotPlanShowProfile === "boolean" ? props.shotPlanShowProfile : true
        shotPlanShowRoaster = typeof props.shotPlanShowRoaster === "boolean" ? props.shotPlanShowRoaster : true
        shotPlanShowGrind = typeof props.shotPlanShowGrind === "boolean" ? props.shotPlanShowGrind : true
        shotPlanShowRoastDate = typeof props.shotPlanShowRoastDate === "boolean" ? props.shotPlanShowRoastDate : false
        shotPlanShowDoseYield = typeof props.shotPlanShowDoseYield === "boolean" ? props.shotPlanShowDoseYield : true
        open()
    }

    modal: true
    closePolicy: Dialog.CloseOnEscape | Dialog.CloseOnPressOutside
    padding: Theme.spacingMedium

    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    width: Math.min(Theme.scaled(320), parent.width - Theme.spacingSmall * 2)
    height: content.implicitHeight + padding * 2

    background: Rectangle {
        color: Theme.surfaceColor
        radius: Theme.cardRadius
        border.color: Theme.borderColor
        border.width: 1
    }

    function save() {
        if (itemType === "screensaverFlipClock")
            Settings.setItemProperty(itemId, "clockScale", clockScale)
        if (itemType === "screensaverShotMap") {
            Settings.setItemProperty(itemId, "mapScale", mapScale)
            Settings.setItemProperty(itemId, "mapTexture", mapTexture)
        }
        if (itemType === "lastShot") {
            Settings.setItemProperty(itemId, "shotScale", shotScale)
            Settings.setItemProperty(itemId, "shotShowLabels", shotShowLabels)
            Settings.setItemProperty(itemId, "shotShowPhaseLabels", shotShowPhaseLabels)
        }
        if (itemType === "shotPlan") {
            Settings.setItemProperty(itemId, "shotPlanShowProfile", shotPlanShowProfile)
            Settings.setItemProperty(itemId, "shotPlanShowRoaster", shotPlanShowRoaster)
            Settings.setItemProperty(itemId, "shotPlanShowGrind", shotPlanShowGrind)
            Settings.setItemProperty(itemId, "shotPlanShowRoastDate", shotPlanShowRoastDate)
            Settings.setItemProperty(itemId, "shotPlanShowDoseYield", shotPlanShowDoseYield)
        }
        saved()
        close()
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
                                ? "white"
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
        RowLayout {
            Layout.fillWidth: true
            visible: popup.itemType === "lastShot"

            Text {
                text: TranslationManager.translate("screensaverEditor.label.showAxisLabels", "Show axis labels")
                font: Theme.labelFont
                color: Theme.textSecondaryColor
                Layout.fillWidth: true
            }

            Switch {
                checked: popup.shotShowLabels
                onToggled: popup.shotShowLabels = checked
            }
        }

        // Frame labels toggle (only for last shot)
        RowLayout {
            Layout.fillWidth: true
            visible: popup.itemType === "lastShot"

            Text {
                text: TranslationManager.translate("screensaverEditor.label.showFrameLabels", "Show frame labels")
                font: Theme.labelFont
                color: Theme.textSecondaryColor
                Layout.fillWidth: true
            }

            Switch {
                checked: popup.shotShowPhaseLabels
                onToggled: popup.shotShowPhaseLabels = checked
            }
        }

        // Shot plan visibility toggles
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall
            visible: popup.itemType === "shotPlan"

            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: TranslationManager.translate("shotPlanEditor.showProfile", "Profile & temperature")
                    font: Theme.labelFont
                    color: Theme.textSecondaryColor
                    Layout.fillWidth: true
                }
                Switch {
                    checked: popup.shotPlanShowProfile
                    onToggled: popup.shotPlanShowProfile = checked
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: TranslationManager.translate("shotPlanEditor.showRoaster", "Roaster")
                    font: Theme.labelFont
                    color: Theme.textSecondaryColor
                    Layout.fillWidth: true
                }
                Switch {
                    checked: popup.shotPlanShowRoaster
                    onToggled: popup.shotPlanShowRoaster = checked
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: TranslationManager.translate("shotPlanEditor.showGrind", "Coffee (grind)")
                    font: Theme.labelFont
                    color: Theme.textSecondaryColor
                    Layout.fillWidth: true
                }
                Switch {
                    checked: popup.shotPlanShowGrind
                    onToggled: popup.shotPlanShowGrind = checked
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: TranslationManager.translate("shotPlanEditor.showRoastDate", "Roast date")
                    font: Theme.labelFont
                    color: Theme.textSecondaryColor
                    Layout.fillWidth: true
                }
                Switch {
                    checked: popup.shotPlanShowRoastDate
                    onToggled: popup.shotPlanShowRoastDate = checked
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: TranslationManager.translate("shotPlanEditor.showDoseYield", "Dose & yield")
                    font: Theme.labelFont
                    color: Theme.textSecondaryColor
                    Layout.fillWidth: true
                }
                Switch {
                    checked: popup.shotPlanShowDoseYield
                    onToggled: popup.shotPlanShowDoseYield = checked
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

        // Buttons
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
                    color: "white"
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
