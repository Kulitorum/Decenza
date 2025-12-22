import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import DE1App

ApplicationWindow {
    id: root
    visible: true
    width: 1280
    height: 800
    title: "DE1 Controller"
    color: Theme.backgroundColor

    // Page stack for navigation
    StackView {
        id: pageStack
        anchors.fill: parent
        initialItem: idlePage

        Component {
            id: idlePage
            IdlePage {}
        }

        Component {
            id: espressoPage
            EspressoPage {}
        }

        Component {
            id: steamPage
            SteamPage {}
        }

        Component {
            id: hotWaterPage
            HotWaterPage {}
        }

        Component {
            id: settingsPage
            SettingsPage {}
        }
    }

    // Status bar overlay
    StatusBar {
        id: statusBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 50
        z: 100
    }

    // Connection state handler - auto navigate based on machine state
    Connections {
        target: MachineState

        function onPhaseChanged() {
            let phase = MachineState.phase

            // Only auto-navigate during active operations
            if (phase === MachineStateType.Phase.Preinfusion ||
                phase === MachineStateType.Phase.Pouring ||
                phase === MachineStateType.Phase.Ending) {
                if (pageStack.currentItem.objectName !== "espressoPage") {
                    pageStack.replace(espressoPage)
                }
            } else if (phase === MachineStateType.Phase.Steaming) {
                if (pageStack.currentItem.objectName !== "steamPage") {
                    pageStack.replace(steamPage)
                }
            } else if (phase === MachineStateType.Phase.HotWater ||
                       phase === MachineStateType.Phase.Flushing) {
                if (pageStack.currentItem.objectName !== "hotWaterPage") {
                    pageStack.replace(hotWaterPage)
                }
            }
        }
    }

    // Helper functions for navigation
    function goToIdle() {
        pageStack.replace(idlePage)
    }

    function goToEspresso() {
        pageStack.replace(espressoPage)
    }

    function goToSteam() {
        pageStack.replace(steamPage)
    }

    function goToHotWater() {
        pageStack.replace(hotWaterPage)
    }

    function goToSettings() {
        pageStack.push(settingsPage)
    }

    function goBack() {
        if (pageStack.depth > 1) {
            pageStack.pop()
        }
    }
}
