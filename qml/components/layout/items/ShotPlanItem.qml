import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import Decenza
import "../.."

Item {
    id: root
    property bool isCompact: false
    property string itemId: ""
    property var modelData: ({})

    readonly property bool showProfile: modelData.shotPlanShowProfile !== false
    readonly property bool showRoaster: modelData.shotPlanShowRoaster !== false
    readonly property bool showGrind: modelData.shotPlanShowGrind !== false
    readonly property bool showRoastDate: modelData.shotPlanShowRoastDate === true
    readonly property bool showDoseYield: modelData.shotPlanShowDoseYield !== false

    // Simplified-home (opt-in) turns full mode into setup buttons; default full mode
    // keeps the shot-plan summary text so existing layouts are unchanged.
    readonly property bool simplifiedHome: Settings.theme.simplifiedHome

    // Open the single global Brew Settings dialog (hosted at the app root) via the
    // window, so this works wherever the tile is placed — including the persistent
    // status bar, which is not a descendant of IdlePage.
    function openBrewSettings() {
        var win = root.Window.window
        if (win && win.openBrewSettings) win.openBrewSettings()
    }

    implicitWidth: isCompact ? compactContent.implicitWidth : fullContent.implicitWidth
    implicitHeight: isCompact ? compactContent.implicitHeight : fullContent.implicitHeight

    // Whole-widget button when it's the tappable summary (compact, or default full
    // mode). In simplified full mode the individual setup buttons are accessible.
    readonly property bool summaryButton: root.isCompact || !root.simplifiedHome
    Accessible.role: summaryButton ? Accessible.Button : Accessible.NoRole
    Accessible.name: {
        if (!summaryButton) return ""
        var plan = (root.isCompact ? compactShotPlan.text : fullShotPlan.text) || ""
        return plan ? "Shot plan: " + plan + ". Tap to edit" : "Shot plan"
    }
    Accessible.focusable: summaryButton

    // --- COMPACT MODE ---
    Item {
        id: compactContent
        visible: root.isCompact
        anchors.fill: parent
        implicitWidth: compactShotPlan.implicitWidth
        implicitHeight: compactShotPlan.implicitHeight

        ShotPlanText {
            id: compactShotPlan
            anchors.centerIn: parent
            visible: text !== ""
            showProfile: root.showProfile
            showRoaster: root.showRoaster
            showGrind: root.showGrind
            showRoastDate: root.showRoastDate
            showDoseYield: root.showDoseYield
            onClicked: root.openBrewSettings()
        }
    }

    // --- FULL MODE ---
    Item {
        id: fullContent
        visible: !root.isCompact
        anchors.fill: parent
        implicitWidth: root.simplifiedHome
            ? (espressoSetupBtn.implicitWidth + profileChooseBtn.implicitWidth + steamSetupBtn.implicitWidth + Theme.spacingLarge * 2)
            : fullShotPlan.implicitWidth
        implicitHeight: root.simplifiedHome
            ? Math.max(espressoSetupBtn.implicitHeight, profileChooseBtn.implicitHeight, steamSetupBtn.implicitHeight)
            : fullShotPlan.implicitHeight

        // Default layout: the shot-plan summary text (unchanged behaviour).
        ShotPlanText {
            id: fullShotPlan
            anchors.centerIn: parent
            visible: !root.simplifiedHome && text !== ""
            showProfile: root.showProfile
            showRoaster: root.showRoaster
            showGrind: root.showGrind
            showRoastDate: root.showRoastDate
            showDoseYield: root.showDoseYield
            onClicked: root.openBrewSettings()
        }

        // Simplified home: three setup buttons, each centred in its third. Outlined
        // (surface fill, accent border) so they stay readable on any theme. Left
        // opens Espresso Setup, centre the profile selector, right the Steam page.

        // Espresso-Shot Setup -> left third; opens the brew/dose dialog
        Rectangle {
            id: espressoSetupBtn
            visible: root.simplifiedHome
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.horizontalCenterOffset: -parent.width / 3
            anchors.verticalCenter: parent.verticalCenter
            implicitWidth: espressoNameTr.implicitWidth + Theme.spacingLarge * 1.5
            implicitHeight: espressoNameTr.implicitHeight + espressoSubText.implicitHeight + Theme.spacingMedium
            radius: Theme.buttonRadius
            color: espressoMa.pressed ? Qt.darker(Theme.surfaceColor, 1.08) : Theme.surfaceColor
            border.width: 1
            border.color: Theme.primaryColor

            Accessible.role: Accessible.Button
            Accessible.name: TranslationManager.translate("idle.button.espressoSetup", "Espresso-Shot Setup")
            Accessible.focusable: root.simplifiedHome
            Accessible.onPressAction: espressoMa.clicked(null)
            activeFocusOnTab: root.simplifiedHome
            KeyNavigation.tab: profileChooseBtn
            Keys.onReturnPressed: espressoMa.clicked(null)
            Keys.onSpacePressed: espressoMa.clicked(null)

            Column {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.right: parent.right
                spacing: 0
                Tr {
                    id: espressoNameTr
                    key: "idle.button.espressoSetup"; fallback: "Espresso-Shot Setup"
                    color: Theme.textColor; font: Theme.bodyFont
                    width: parent.width; horizontalAlignment: Text.AlignHCenter
                }
                Text {
                    id: espressoSubText
                    width: parent.width; horizontalAlignment: Text.AlignHCenter
                    text: {
                        var d = Settings.dye.dyeBeanWeight
                        var t = ProfileManager.targetWeight
                        return (d > 0 && t > 0) ? d.toFixed(1) + " → " + t.toFixed(1) + " g" : ""
                    }
                    visible: text !== ""
                    color: Theme.textSecondaryColor; font: Theme.labelFont
                }
            }
            MouseArea { id: espressoMa; anchors.fill: parent; onClicked: root.openBrewSettings() }
        }

        // Choose Espresso Profile -> centre; opens the profile selector
        Rectangle {
            id: profileChooseBtn
            visible: root.simplifiedHome
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            implicitWidth: profileNameTr.implicitWidth + Theme.spacingLarge * 1.5
            implicitHeight: profileNameTr.implicitHeight + profileSubText.implicitHeight + Theme.spacingMedium
            radius: Theme.buttonRadius
            color: profileMa.pressed ? Qt.darker(Theme.surfaceColor, 1.08) : Theme.surfaceColor
            border.width: 1
            border.color: Theme.primaryColor

            Accessible.role: Accessible.Button
            Accessible.name: TranslationManager.translate("idle.button.chooseProfile", "Choose Espresso Profile")
            Accessible.focusable: root.simplifiedHome
            Accessible.onPressAction: profileMa.clicked(null)
            activeFocusOnTab: root.simplifiedHome
            KeyNavigation.tab: steamSetupBtn
            Keys.onReturnPressed: profileMa.clicked(null)
            Keys.onSpacePressed: profileMa.clicked(null)

            Column {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.right: parent.right
                spacing: 0
                Tr {
                    id: profileNameTr
                    key: "idle.button.chooseProfile"; fallback: "Choose Espresso Profile"
                    color: Theme.textColor; font: Theme.bodyFont
                    width: parent.width; horizontalAlignment: Text.AlignHCenter
                }
                Text {
                    id: profileSubText
                    width: parent.width; horizontalAlignment: Text.AlignHCenter
                    text: ProfileManager.currentProfileName
                    visible: text !== ""
                    color: Theme.textSecondaryColor; font: Theme.labelFont
                    elide: Text.ElideRight
                }
            }
            MouseArea { id: profileMa; anchors.fill: parent; onClicked: root.openProfileChooser() }
        }

        // Milk-Steaming Setup -> right third; opens the steam page (no long-press)
        Rectangle {
            id: steamSetupBtn
            visible: root.simplifiedHome
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.horizontalCenterOffset: parent.width / 3
            anchors.verticalCenter: parent.verticalCenter
            implicitWidth: steamNameTr.implicitWidth + Theme.spacingLarge * 1.5
            implicitHeight: steamNameTr.implicitHeight + steamSubText.implicitHeight + Theme.spacingMedium
            radius: Theme.buttonRadius
            color: steamMa.pressed ? Qt.darker(Theme.surfaceColor, 1.08) : Theme.surfaceColor
            border.width: 1
            border.color: Theme.primaryColor

            Accessible.role: Accessible.Button
            Accessible.name: TranslationManager.translate("idle.button.steamSetup", "Milk-Steaming Setup")
            Accessible.focusable: root.simplifiedHome
            Accessible.onPressAction: steamMa.clicked(null)
            activeFocusOnTab: root.simplifiedHome
            Keys.onReturnPressed: steamMa.clicked(null)
            Keys.onSpacePressed: steamMa.clicked(null)

            Column {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.right: parent.right
                spacing: 0
                Tr {
                    id: steamNameTr
                    key: "idle.button.steamSetup"; fallback: "Milk-Steaming Setup"
                    color: Theme.textColor; font: Theme.bodyFont
                    width: parent.width; horizontalAlignment: Text.AlignHCenter
                }
                Text {
                    id: steamSubText
                    width: parent.width; horizontalAlignment: Text.AlignHCenter
                    text: Math.round(Settings.brew.steamTemperature) + "°C"
                    color: Theme.textSecondaryColor; font: Theme.labelFont
                }
            }
            MouseArea { id: steamMa; anchors.fill: parent; onClicked: root.openSteamSetup() }
        }
    }

    // Open the steam page (same target as long-pressing a steam pitcher pill).
    function openSteamSetup() {
        if (typeof pageStack !== "undefined" && pageStack)
            pageStack.push(Qt.resolvedUrl("../../../pages/SteamPage.qml"))
    }
    // Open the profile selector to change the espresso profile.
    function openProfileChooser() {
        if (typeof pageStack !== "undefined" && pageStack)
            pageStack.push(Qt.resolvedUrl("../../../pages/ProfileSelectorPage.qml"))
    }
}
