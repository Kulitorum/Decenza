import QtQuick

// Presentation values supplied by each host; this type has no app singleton
// dependencies so the graph view can be built by the Wasm target unchanged.
QtObject {
    property real scale: 1
    property bool darkMode: true
    property bool fahrenheit: false
    property font captionFont
    property color textSecondaryColor: "#c0c5e3"
    property color surfaceColor: "#20262c"
    property color pressureColor: "#18c37e"
    property color pressureGoalColor: "#69fdb3"
    property color flowColor: "#4e85f4"
    property color flowGoalColor: "#7aaaff"
    property color temperatureColor: "#e73249"
    property color temperatureGoalColor: "#ffa5a6"
    property color weightColor: "#a2693d"
    property color weightFlowColor: "#d4a574"
    property color resistanceColor: "#eae83d"
    property color conductanceColor: "#00d2d3"
    property color darcyResistanceColor: "#f0a500"
    property color temperatureMixColor: "#ce93d8"
    property color temperatureMixGoalColor: "#a678b8"
    property color accentColor: "#e94560"
    property color stopMarkerColor: "#ff8c42"
    property color frameMarkerColor: "#ffffff"
    property color portalEcColor: "#26d7bb"
    property color portalTemperatureColor: "#dd83ff"
    property string timeAxisTitle: "Time (s)"
    property string weightAxisAnnouncement: "Right axis: Weight. Tap for Temperature"
    property string flowAxisAnnouncement: "Right axis: Flow. Tap for Weight"
    property string temperatureAxisAnnouncement: "Right axis: Temperature. Tap for Flow"
    property string flowUnit: "mL/s"
    property string temperatureUnit: "°C"

    function scaled(value: real): int { return Math.round(value * scale) }
    function cToDisplay(value: real): real { return fahrenheit ? value * 9 / 5 + 32 : value }
    function tempUnitSuffix(): string { return temperatureUnit }
}
