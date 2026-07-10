import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import QtQuick.Window
import Decenza
import "../.."

Item {
    id: root
    property bool isCompact: false
    property string itemId: ""

    // True when the app is allowed to start machine operations on-screen.
    // The hardware Group Head Controller (GHC), when present and active, takes
    // exclusive control of starting shots/steam/etc., so on-screen start calls
    // are only valid in headless (no/inactive GHC) or simulation mode.
    readonly property bool canStartOperations: DE1Device.isHeadless || DE1Device.simulationMode

    // Access the IdlePage's activePresetFunction via the page
    property var idlePage: {
        var p = root.parent
        while (p) {
            if (p.objectName === "idlePage") return p
            p = p.parent
        }
        return null
    }

    // Highlight this button while its mode is selected on the home screen (the
    // centre preset row is expanded), or — in compact mode, where tapping opens
    // presetPopup instead of setting activePresetFunction — while its popup is open.
    readonly property bool isActive:
        (idlePage ? idlePage.activePresetFunction : "") === "espresso" || presetPopup.visible

    // Compact (bar) rendering only: full-size placements of this type compile to
    // CustomItem in LayoutItemDelegate (isCompiled), so this item never loads
    // non-compact and carries no full-mode rendering.
    implicitWidth: compactContent.implicitWidth
    implicitHeight: compactContent.implicitHeight

    function togglePresets() {
        if (root.isCompact) {
            presetPopup.visible ? presetPopup.close() : presetPopup.open()
        } else if (root.idlePage) {
            root.idlePage.activePresetFunction =
                (root.idlePage.activePresetFunction === "espresso") ? "" : "espresso"
        }
    }

    function goToProfileSelector() {
        if (typeof pageStack !== "undefined") {
            pageStack.push(Qt.resolvedUrl("../../../pages/ProfileSelectorPage.qml"))
        }
    }

    // --- COMPACT MODE ---
    Item {
        id: compactContent
        visible: root.isCompact
        anchors.fill: parent
        implicitWidth: compactEspressoRow.implicitWidth + Theme.scaled(16)
        implicitHeight: Theme.bottomBarHeight

        RowLayout {
            id: compactEspressoRow
            anchors.centerIn: parent
            spacing: Theme.spacingSmall

            Image {
                source: "qrc:/icons/profile.svg"
                sourceSize.height: Theme.scaled(20)
                fillMode: Image.PreserveAspectFit
                opacity: DE1Device.guiEnabled ? 1.0 : 0.5
                Accessible.ignored: true
                layer.enabled: true
                layer.smooth: true
                layer.effect: MultiEffect {
                    colorization: 1.0
                    colorizationColor: root.isActive ? Theme.accentColor : Theme.textColor
                }
            }
            Tr {
                key: "idle.button.profiles"
                fallback: "Profiles"
                font: Theme.bodyFont
                color: !DE1Device.guiEnabled ? Theme.textSecondaryColor
                       : (root.isActive ? Theme.accentColor : Theme.textColor)
                Accessible.ignored: true
            }
        }

        AccessibleTapHandler {
            anchors.fill: parent
            enabled: DE1Device.guiEnabled
            supportLongPress: true
            supportDoubleClick: true
            accessibleName: TranslationManager.translate("idle.button.profiles", "Profiles")
                            + (root.isActive ? ", " + TranslationManager.translate("accessibility.selected", "selected") : "")
            accessibleDescription: TranslationManager.translate("idle.accessible.espresso.hint", "Tap to toggle presets. Double-tap or long-press to select profile.")
            onAccessibleClicked: root.togglePresets()
            onAccessibleDoubleClicked: root.goToProfileSelector()
            onAccessibleLongPressed: root.goToProfileSelector()
        }
    }

    // --- PRESET POPUP (compact mode) ---
    Popup {
        id: presetPopup
        modal: false
        padding: Theme.spacingMedium
        closePolicy: Popup.CloseOnPressOutside

        width: {
            var win = root.Window.window
            var w = Theme.scaled(600) + 2 * padding
            return win ? Math.min(w, win.width) : w
        }

        y: {
            var _v = visible // Force re-evaluation when popup opens (mapToItem is not reactive)
            var win = root.Window.window
            if (win) {
                var globalY = root.mapToItem(null, 0, 0).y
                var spaceBelow = win.height - globalY - root.height - Theme.spacingSmall
                var spaceAbove = globalY - Theme.spacingSmall
                if (height > spaceBelow && spaceAbove > spaceBelow)
                    return -height - Theme.spacingSmall
            }
            return parent.height + Theme.spacingSmall
        }

        x: {
            var _v = visible // Force re-evaluation when popup opens (mapToItem is not reactive)
            var win = root.Window.window
            if (win) {
                var globalX = root.mapToItem(null, 0, 0).x
                var centered = -width / 2 + parent.width / 2
                // Clamp right
                if (globalX + centered + width > win.width)
                    centered = win.width - globalX - width
                // Clamp left
                if (globalX + centered < 0)
                    centered = -globalX
                return centered
            }
            return -width / 2 + parent.width / 2
        }

        background: Rectangle {
            color: Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.borderColor
            border.width: 1
        }

        contentItem: Column {
            width: implicitWidth
            spacing: Theme.scaled(8)

            PresetPillRow {
                maxWidth: Theme.scaled(600)
                presets: Settings.app.favoriteProfiles
                selectedIndex: Settings.app.selectedFavoriteProfile
                supportLongPress: true
                modified: ProfileManager.profileModified
                modifiedIsReadOnly: ProfileManager.isCurrentProfileReadOnly

                onPresetSelected: function(index) {
                    var wasAlreadySelected = (index === Settings.app.selectedFavoriteProfile)
                    Settings.app.selectedFavoriteProfile = index
                    var preset = Settings.app.getFavoriteProfile(index)

                    if (wasAlreadySelected) {
                        if (MachineState.isReady && root.canStartOperations) {
                            DE1Device.startEspresso()
                        } else {
                            console.log("Cannot start espresso - machine not ready, phase:", MachineState.phase)
                            if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled)
                                AccessibilityManager.announce(TranslationManager.translate("machine.notReady", "Machine is not ready"))
                        }
                    } else {
                        if (preset && preset.filename) {
                            ProfileManager.loadProfile(preset.filename)
                        }
                    }
                    presetPopup.close()
                }
            }

            // Non-favorite profile pill
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: Settings.app.selectedFavoriteProfile === -1 && root.canStartOperations
                spacing: Theme.scaled(8)

                Rectangle {
                    width: nonFavText.implicitWidth + Theme.scaled(40)
                    height: Theme.scaled(50)
                    radius: Theme.scaled(10)
                    color: Theme.successColor

                    Accessible.role: Accessible.Button
                    Accessible.name: (ProfileManager.currentProfileName || "") + " " + TranslationManager.translate("espressoitem.accessible.startespresso", "Start espresso")
                    Accessible.focusable: true
                    Accessible.onPressAction: nonFavMouseArea.clicked(null)

                    Text {
                        id: nonFavText
                        anchors.centerIn: parent
                        text: ProfileManager.currentProfileName || ""
                        color: Theme.primaryContrastColor
                        font.pixelSize: Theme.scaled(16)
                        font.bold: true
                        Accessible.ignored: true
                    }

                    MouseArea {
                        id: nonFavMouseArea
                        anchors.fill: parent
                        onClicked: {
                            if (MachineState.isReady && root.canStartOperations) {
                                DE1Device.startEspresso()
                            } else {
                                console.log("Cannot start espresso - machine not ready, phase:", MachineState.phase)
                                if (typeof AccessibilityManager !== "undefined" && AccessibilityManager.enabled)
                                    AccessibilityManager.announce(TranslationManager.translate("machine.notReady", "Machine is not ready"))
                            }
                            presetPopup.close()
                        }
                    }
                }
            }
        }
    }
}
