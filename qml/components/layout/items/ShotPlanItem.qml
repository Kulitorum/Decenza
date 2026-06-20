import QtQuick
import QtQuick.Layouts
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

    implicitWidth: isCompact ? compactContent.implicitWidth : fullContent.implicitWidth
    implicitHeight: isCompact ? compactContent.implicitHeight : fullContent.implicitHeight

    // In full mode the Profile/Dose buttons below are individually accessible, so
    // only expose this wrapper as a button in compact (bar) mode.
    Accessible.role: root.isCompact ? Accessible.Button : Accessible.NoRole
    Accessible.name: {
        if (!root.isCompact) return ""
        var plan = compactShotPlan.text || ""
        return plan ? "Shot plan: " + plan + ". Tap to edit" : "Shot plan"
    }
    Accessible.focusable: root.isCompact

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
            onClicked: brewDialog.open()
        }
    }

    // --- FULL MODE: two setup buttons, each centered in its half. Left opens the
    // Espresso-Shot setup (brew/dose dialog), right opens the Milk-Steaming setup
    // (steam page) directly — no long-press needed. White-fill, blue-text, rounded. ---
    Item {
        id: fullContent
        visible: !root.isCompact
        anchors.fill: parent
        implicitWidth: espressoSetupBtn.implicitWidth + profileChooseBtn.implicitWidth + steamSetupBtn.implicitWidth + Theme.spacingLarge * 2
        implicitHeight: Math.max(espressoSetupBtn.implicitHeight, profileChooseBtn.implicitHeight, steamSetupBtn.implicitHeight)

        // Espresso-Shot Setup -> left third; opens the brew/dose dialog
        Rectangle {
            id: espressoSetupBtn
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.horizontalCenterOffset: -parent.width / 3
            anchors.verticalCenter: parent.verticalCenter
            implicitWidth: espressoNameTr.implicitWidth + Theme.spacingLarge * 1.5
            implicitHeight: espressoNameTr.implicitHeight + espressoSubText.implicitHeight + Theme.spacingMedium
            radius: Theme.buttonRadius
            color: espressoMa.pressed ? Qt.darker(Theme.primaryContrastColor, 1.08) : Theme.primaryContrastColor
            border.width: 1
            border.color: Theme.primaryColor

            Accessible.role: Accessible.Button
            Accessible.name: TranslationManager.translate("idle.button.espressoSetup", "Espresso-Shot Setup")
            Accessible.focusable: true
            Accessible.onPressAction: espressoMa.clicked(null)

            Column {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.right: parent.right
                spacing: 0
                Tr {
                    id: espressoNameTr
                    key: "idle.button.espressoSetup"; fallback: "Espresso-Shot Setup"
                    color: Theme.primaryColor; font: Theme.bodyFont
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
            MouseArea { id: espressoMa; anchors.fill: parent; onClicked: brewDialog.open() }
        }

        // Choose Espresso Profile -> center; opens the profile selector
        Rectangle {
            id: profileChooseBtn
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            implicitWidth: profileNameTr.implicitWidth + Theme.spacingLarge * 1.5
            implicitHeight: profileNameTr.implicitHeight + profileSubText.implicitHeight + Theme.spacingMedium
            radius: Theme.buttonRadius
            color: profileMa.pressed ? Qt.darker(Theme.primaryContrastColor, 1.08) : Theme.primaryContrastColor
            border.width: 1
            border.color: Theme.primaryColor

            Accessible.role: Accessible.Button
            Accessible.name: TranslationManager.translate("idle.button.chooseProfile", "Choose Espresso Profile")
            Accessible.focusable: true
            Accessible.onPressAction: profileMa.clicked(null)

            Column {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.right: parent.right
                spacing: 0
                Tr {
                    id: profileNameTr
                    key: "idle.button.chooseProfile"; fallback: "Choose Espresso Profile"
                    color: Theme.primaryColor; font: Theme.bodyFont
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
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.horizontalCenterOffset: parent.width / 3
            anchors.verticalCenter: parent.verticalCenter
            implicitWidth: steamNameTr.implicitWidth + Theme.spacingLarge * 1.5
            implicitHeight: steamNameTr.implicitHeight + steamSubText.implicitHeight + Theme.spacingMedium
            radius: Theme.buttonRadius
            color: steamMa.pressed ? Qt.darker(Theme.primaryContrastColor, 1.08) : Theme.primaryContrastColor
            border.width: 1
            border.color: Theme.primaryColor

            Accessible.role: Accessible.Button
            Accessible.name: TranslationManager.translate("idle.button.steamSetup", "Milk-Steaming Setup")
            Accessible.focusable: true
            Accessible.onPressAction: steamMa.clicked(null)

            Column {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.right: parent.right
                spacing: 0
                Tr {
                    id: steamNameTr
                    key: "idle.button.steamSetup"; fallback: "Milk-Steaming Setup"
                    color: Theme.primaryColor; font: Theme.bodyFont
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

    BrewDialog {
        id: brewDialog
    }
}
