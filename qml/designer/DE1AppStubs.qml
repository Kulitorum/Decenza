// Design-time stubs for C++ backend types
// These provide placeholder values for Qt Design Studio

import QtQuick

QtObject {
    id: backend

    // DE1Device stub
    property QtObject de1Device: QtObject {
        property bool connected: true
        property double pressure: 9.0
        property double flow: 2.5
        property double temperature: 93.0
        property double waterLevel: 75.0
        property string firmwareVersion: "1.0.0"
        property int state: 0
        function stopOperation() {}
        function connectToDevice(addr) {}
        function startEspresso() {}
        function startFlush() {}
    }

    // ScaleDevice stub
    property QtObject scaleDevice: QtObject {
        property bool connected: true
        property double weight: 36.0
        property double flowRate: 2.0
        function tare() {}
    }

    // MachineState stub
    property QtObject machineState: QtObject {
        property int phase: 0
        property double shotTime: 25.0
        property bool isFlowing: false
        property bool isReady: true
    }

    // MainController stub
    property QtObject mainController: QtObject {
        property string currentProfileName: "Default Profile"
        property double targetWeight: 36.0
        property var availableProfiles: [
            { name: "default", title: "Default" },
            { name: "blooming_espresso", title: "Blooming Espresso" },
            { name: "turbo_shot", title: "Turbo" }
        ]
        function loadProfile(name) {}
        function applySteamSettings() {}
        function setHotWaterFlowRateImmediate(flow) {}
        function setSteamFlowImmediate(flow) {}
        function setSteamTemperatureImmediate(temp) {}
        function uploadCurrentProfile() {}
        function uploadProfile(profile) {}
        function getCurrentProfile() {
            return {
                title: "Default Profile",
                steps: [
                    { name: "Preinfusion", temperature: 93, sensor: "coffee", pump: "pressure",
                      transition: "fast", pressure: 2, flow: 2, seconds: 10,
                      exit_if: true, exit_type: "pressure_over", exit_pressure_over: 4 },
                    { name: "Ramp Up", temperature: 93, sensor: "coffee", pump: "pressure",
                      transition: "smooth", pressure: 9, flow: 2, seconds: 5 },
                    { name: "Hold", temperature: 93, sensor: "coffee", pump: "pressure",
                      transition: "fast", pressure: 9, flow: 2, seconds: 30 },
                    { name: "Decline", temperature: 93, sensor: "coffee", pump: "pressure",
                      transition: "smooth", pressure: 6, flow: 2, seconds: 15 }
                ],
                target_weight: 36,
                espresso_temperature: 93,
                mode: "frame_based"
            }
        }
    }

    // BLEManager stub
    property QtObject bleManager: QtObject {
        property bool scanning: false
        property var discoveredDevices: []
        property var discoveredScales: []
        function startScan() {}
        function stopScan() {}
    }

    // Settings stub
    property QtObject settings: QtObject {
        property int steamTimeout: 30
        property int steamFlow: 150
        property int steamTemperature: 160
        property int selectedSteamPitcher: 0
        property var steamPitcherPresets: [
            { name: "Small", duration: 20, flow: 100 },
            { name: "Medium", duration: 30, flow: 150 },
            { name: "Large", duration: 45, flow: 200 }
        ]
        function getSteamPitcherPreset(idx) { return steamPitcherPresets[idx] }
        function updateSteamPitcherPreset(idx, name, dur, flow) {}
        function addSteamPitcherPreset(name, dur, flow) {}
        function removeSteamPitcherPreset(idx) {}
        function moveSteamPitcherPreset(from, to) {}
    }

    // ShotDataModel stub
    property QtObject shotDataModel: QtObject {
        property var pressureData: []
        property var flowData: []
        property var temperatureData: []
        property var weightData: []
        property var pressureGoalData: []
        property var flowGoalData: []
        property var temperatureGoalData: []
        property var phaseMarkers: []
        property double extractionStartTime: -1
    }
}
