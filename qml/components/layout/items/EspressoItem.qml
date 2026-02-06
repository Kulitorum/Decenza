import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import DecenzaDE1
import "../.."

Item {
    id: root
    property bool isCompact: false
    property string itemId: ""

    // Access the IdlePage's activePresetFunction via the page
    property var idlePage: {
        var p = root.parent
        while (p) {
            if (p.objectName === "idlePage") return p
            p = p.parent
        }
        return null
    }

    implicitWidth: isCompact ? compactContent.implicitWidth : fullContent.implicitWidth
    implicitHeight: isCompact ? compactContent.implicitHeight : fullContent.implicitHeight

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
                source: "qrc:/icons/espresso.svg"
                sourceSize.height: Theme.scaled(20)
                fillMode: Image.PreserveAspectFit
                opacity: DE1Device.guiEnabled ? 1.0 : 0.5
                Accessible.ignored: true
            }
            Tr {
                key: "idle.button.espresso"
                fallback: "Espresso"
                font: Theme.bodyFont
                color: DE1Device.guiEnabled ? Theme.textColor : Theme.textSecondaryColor
                Accessible.ignored: true
            }
        }

        AccessibleTapHandler {
            anchors.fill: parent
            enabled: DE1Device.guiEnabled
            supportLongPress: true
            supportDoubleClick: true
            accessibleName: TranslationManager.translate("idle.button.espresso", "Espresso")
            onAccessibleClicked: root.togglePresets()
            onAccessibleDoubleClicked: root.goToProfileSelector()
            onAccessibleLongPressed: root.goToProfileSelector()
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
            translationKey: "idle.button.espresso"
            translationFallback: "Espresso"
            iconSource: "qrc:/icons/espresso.svg"
            enabled: DE1Device.guiEnabled
            backgroundColor: Settings.selectedFavoriteProfile === -1 ? Theme.highlightColor : Theme.primaryColor
            onClicked: root.togglePresets()
            onPressAndHold: root.goToProfileSelector()
            onDoubleClicked: root.goToProfileSelector()

            Accessible.description: TranslationManager.translate("idle.accessible.espresso.description", "Start espresso. Double-tap to select profile. Long-press for settings.")
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
                presets: Settings.favoriteProfiles
                selectedIndex: Settings.selectedFavoriteProfile
                supportLongPress: true

                onPresetSelected: function(index) {
                    var wasAlreadySelected = (index === Settings.selectedFavoriteProfile)
                    Settings.selectedFavoriteProfile = index
                    var preset = Settings.getFavoriteProfile(index)

                    if (wasAlreadySelected) {
                        if (MachineState.isReady) {
                            DE1Device.startEspresso()
                        }
                    } else {
                        if (preset && preset.filename) {
                            MainController.loadProfile(preset.filename)
                        }
                    }
                    presetPopup.close()
                }
            }

            // Non-favorite profile pill
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: Settings.selectedFavoriteProfile === -1
                spacing: Theme.scaled(8)

                Rectangle {
                    width: nonFavText.implicitWidth + Theme.scaled(40)
                    height: Theme.scaled(50)
                    radius: Theme.scaled(10)
                    color: Theme.successColor

                    Text {
                        id: nonFavText
                        anchors.centerIn: parent
                        text: MainController.currentProfileName || ""
                        color: "white"
                        font.pixelSize: Theme.scaled(16)
                        font.bold: true
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            if (MachineState.isReady) DE1Device.startEspresso()
                            presetPopup.close()
                        }
                    }
                }
            }
        }
    }
}
