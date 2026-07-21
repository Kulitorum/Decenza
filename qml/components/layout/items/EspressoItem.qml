import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
import QtQuick.Window
import Decenza
import "../.."
import "../PillFit.js" as PillFit

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

    // Live two-row fit for the favorite-profile pills (descriptive-recipe-names,
    // mirrors RecipesItem/BeansItem): pack the favorites into pages of AT MOST
    // TWO ROWS at the pill row's real available width instead of wrapping
    // unbounded. The selected pill may carry a modified marker ("*"/" (modified)")
    // that widens it, so its measurement includes that marker. Widths MIRROR
    // PresetPillRow's pill metrics (font 16 bold, padding 40, spacing 12);
    // profile pills carry no icon. Keep in sync with PresetPillRow.qml/PillFit.js.
    property int profilePageIndex: 0
    readonly property real _pillFitAvail: profilesPillRow ? profilesPillRow.effectiveMaxWidth : Theme.scaled(600)
    // FontMetrics.advanceWidth() (not a mutated TextMetrics.text/.width) so
    // measuring inside a reactive binding doesn't self-trigger a binding loop.
    FontMetrics { id: profilePillMetrics; font.pixelSize: Theme.scaled(16); font.bold: true }
    function _profilePillWidths() {
        var favs = Settings.app.favoriteProfiles
        var sel = Settings.app.selectedFavoriteProfile
        var out = []
        for (var i = 0; i < favs.length; ++i) {
            var name = (favs[i] && favs[i].name) || ""
            // Match PresetPillRow.pillLayoutName's modified marker on the selected pill.
            if (ProfileManager.profileModified && i === sel)
                name = ProfileManager.isCurrentProfileReadOnly
                    ? name + " " + TranslationManager.translate("presets.modified", "(modified)")
                    : "*" + name
            out.push(profilePillMetrics.advanceWidth(name) + Theme.scaled(40))
        }
        return out
    }
    readonly property var _profilePageSizes: {
        var _m = ProfileManager.profileModified  // re-measure when the marker toggles
        var widths = _profilePillWidths()
        var sizes = PillFit.packPageSizes(widths, Theme.scaled(12), _pillFitAvail, 2)
        if (sizes.length <= 1)
            return sizes
        return PillFit.packPageSizes(widths, Theme.scaled(12),
                                     Math.max(0, _pillFitAvail - 2 * Theme.scaled(48)), 2)
    }
    readonly property int profilePageCount: Math.max(1, _profilePageSizes.length)
    // Absolute index of the first favorite on the current page.
    readonly property int _profilePageStart: {
        var idx = Math.max(0, Math.min(profilePageIndex, _profilePageSizes.length - 1))
        var start = 0
        for (var p = 0; p < idx; ++p)
            start += _profilePageSizes[p]
        return start
    }
    readonly property var visibleProfiles: {
        var favs = Settings.app.favoriteProfiles
        if (!favs || favs.length === 0)
            return []
        var idx = Math.max(0, Math.min(profilePageIndex, _profilePageSizes.length - 1))
        return favs.slice(_profilePageStart, _profilePageStart + (_profilePageSizes[idx] || 0))
    }
    // Keep the current page in range when favorites change.
    Connections {
        target: Settings.app
        function onFavoriteProfilesChanged() {
            root.profilePageIndex = Math.max(0, Math.min(root.profilePageIndex, root.profilePageCount - 1))
        }
    }

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
    // Dialog (not Popup) so TalkBack/VoiceOver trap focus inside the pill list,
    // matching BeansItem/EquipmentItem/RecipesItem. modal traps focus; dim:false
    // keeps the dropdown look; header/footer null strip the Dialog chrome.
    Dialog {
        id: presetPopup
        modal: true
        dim: false
        header: null
        footer: null
        padding: Theme.spacingMedium
        closePolicy: Popup.CloseOnPressOutside

        // Reopen on the first (most-recent) page, matching RecipesItem/BeansItem.
        onAboutToShow: root.profilePageIndex = 0

        // Slide the OTHER idle content clear of this popup (up for a lower-half
        // placement, down for an upper-half one); the button/popup stay put.
        onOpened: {
            if (root.idlePage) {
                var rootTopInPage = root.mapToItem(root.idlePage, 0, 0).y
                root.idlePage.requestPanelClearance(rootTopInPage + presetPopup.y, presetPopup.height)
            }
        }
        onClosed: { if (root.idlePage) root.idlePage.releasePanelClearance() }

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
            // Over a custom background image, float the pills directly (matching
            // the center inline preset rows) instead of showing a panel; keep the
            // opaque surface panel when no background image is set.
            readonly property bool hasBackgroundImage: Settings.theme.backgroundImagePath.length > 0
            color: hasBackgroundImage ? "transparent" : Theme.surfaceColor
            radius: Theme.cardRadius
            border.color: Theme.borderColor
            border.width: hasBackgroundImage ? 0 : 1
        }

        contentItem: Column {
            width: implicitWidth
            spacing: Theme.scaled(8)

            PresetPillRow {
                id: profilesPillRow
                maxWidth: Theme.scaled(600)
                // Windowed to the current two-row page; selection and taps map
                // back to absolute favorite indices via _profilePageStart.
                presets: root.visibleProfiles
                selectedIndex: {
                    var sel = Settings.app.selectedFavoriteProfile
                    if (sel < 0) return -1
                    var rel = sel - root._profilePageStart
                    return (rel >= 0 && rel < root.visibleProfiles.length) ? rel : -1
                }
                supportLongPress: true
                modified: ProfileManager.profileModified
                modifiedIsReadOnly: ProfileManager.isCurrentProfileReadOnly

                pageCount: root.profilePageCount
                pageIndex: root.profilePageIndex
                prevPageAccessibleName: TranslationManager.translate("idle.pagination.previousProfiles", "Previous profiles")
                nextPageAccessibleName: TranslationManager.translate("idle.pagination.nextProfiles", "Next profiles")
                onPageChangeRequested: function(delta) {
                    root.profilePageIndex = Math.max(0, Math.min(root.profilePageIndex + delta, root.profilePageCount - 1))
                }

                onPresetSelected: function(index) {
                    var absIndex = root._profilePageStart + index
                    var wasAlreadySelected = (absIndex === Settings.app.selectedFavoriteProfile)
                    Settings.app.selectedFavoriteProfile = absIndex
                    var preset = Settings.app.getFavoriteProfile(absIndex)

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
